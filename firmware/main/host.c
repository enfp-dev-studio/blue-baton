/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>


#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "cfg_store.h"
#include "gap_name.h"
#include "ble_pair.h"
#include "host.h"
#include "platform.h"
#include "host_report.h"
#include "json_mini.h"
#include "router.h"
#include "version.h"

static const char *TAG = "host";

#define HOST_LINK_MAX 4          /* 3 hosts + 1 candidate being paired */
#define WINDOW_MS 180000         /* Host pairing window: HOGP 1.0 5.1 Limited Discoverable limit */
#define FAST_ADV_MS 30000        /* How long fast advertising is kept */
#define CANDIDATE_MS 60000       /* Time within which a candidate connection must finish pairing */
#define PARAM_RETRY_MS 2000      /* Delay before re-requesting connection parameters after a collision with the peer's procedure */
#define PARAM_RETRY_MAX 2
/*
 * Connection interval request (units of 1.25 ms): the active host is asked for 7.5 ms first, and for 15 ms if the
 * board's controller or the host refuses; idle hosts get 15 ms.
 * - 2026-10-14, Windows host active, 2 hosts + 2 input devices: 7.5, 8.75, 10 and 11.25 ms were all taken (a
 *   7.5-15 ms range was answered with 15). Reports completed within 15 ms of input: 7.5 ms 78% (longest 31 ms),
 *   10 ms 64%, 8.75 ms 28% (the input devices' interval: the two schedules keep colliding). At 15 ms a host gets at
 *   most one report batch per 15 ms, so the cursor moves at most ~66 times a second.
 * - The same day the controller refused 7.5 and 11.25 ms for a Mac link (HCI 0x12) and took 15 ms.
 * - macOS gives 15 ms at the least (Apple ADG), and takes a 15 ms request at once.
 * - A 7.5-15 ms range (as ZMK asks) was refused by the board's own controller before it reached the host
 *   (HCI 0x12 on LE Connection Update) for 3 of 6 requests with an input device connected; the host then kept its
 *   own parameters (seen: latency 0 with a 720 ms supervision timeout). A 15 ms request was never refused (~110).
 * - An 11.25 ms request was answered with status 0 while macOS 26 kept 30 ms, then moved to 15 ms by itself.
 */
#define ITVL_HOST_MIN 12
#define ITVL_HOST_MAX 12
#define ITVL_ACTIVE 6
#define SEC_REQ_MS 1000          /* Send a Security Request if a bonded host has not started encryption within this time */
/* Advertising interval (units of 0.625 ms): Apple recommends 20 ms, then 152.5 ms (Accessory Design Guidelines R31 58.5); the window uses 30 ms (Microsoft Swift Pair) */
#define ADV_ITVL_RECONNECT_FAST 32
#define ADV_ITVL_WINDOW_FAST 48
#define ADV_ITVL_SLOW 244
#define DEVICE_NAME "Blue Baton"   /* GAP name and advertising name (at most 18 bytes fit the advertising packet) */
#define MANUFACTURER "Blue Baton (DIY)"
#define MODEL "Blue Baton ESP32-S3"
#define APPEARANCE_KEYBOARD 0x03C1
#define HCI_USER_TERM 0x13

/* ---------- GATT table (bonded hosts cache the handles: after any change bump GATT_LAYOUT so they are told, see host_on_sync) ---------- */

enum {
    A_MANUF = 1, A_MODEL, A_FWREV, A_PNP, A_BATTERY,
    A_HID_INFO, A_REPORT_MAP, A_CONTROL_POINT, A_PROTOCOL_MODE,
    A_REPORT_KB, A_REPORT_MOUSE, A_REPORT_CONSUMER, A_REPORT_SYSTEM, A_REPORT_LED,
    A_BOOT_KB_IN, A_BOOT_KB_OUT, A_BOOT_MOUSE_IN,
};

/* Characteristics that send notifications (per-connection subscription bit numbers) */
enum { N_KB, N_MOUSE, N_CONSUMER, N_SYSTEM, N_BOOT_KB, N_BOOT_MOUSE, N_COUNT };
static uint16_t s_notify_handle[N_COUNT];

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ref_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Protection: HID, battery and device information values need an authenticated (MITM) bond to read or write (HOGP 6.1 Level 3).
 * Service and characteristic declarations (discovery) are not protected (Apple Accessory Design Guidelines R31 58.8) */
#define R_AUTH (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN)
#define W_AUTH (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define WNR_AUTH (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define N_AUTH (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN)
#define REF(id, type) ((void *)(uintptr_t)((id) | ((type) << 8)))
#define REF_DSC(id, type)                                                                                   \
    (struct ble_gatt_dsc_def[])                                                                             \
    {                                                                                                       \
        {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,             \
         .access_cb = ref_access, .arg = REF(id, type)},                                                    \
        {0}                                                                                                 \
    }

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        /* Device information (DIS, exactly one: HOGP 1.0 2.2). HOGP makes PnP ID mandatory */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = chr_access, .arg = (void *)A_MANUF, .flags = R_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A24), .access_cb = chr_access, .arg = (void *)A_MODEL, .flags = R_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A26), .access_cb = chr_access, .arg = (void *)A_FWREV, .flags = R_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = chr_access, .arg = (void *)A_PNP, .flags = R_AUTH},
            {0},
        },
    },
    {
        /* Battery (always 100 on USB power). Notify is optional, so it is omitted */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = chr_access, .arg = (void *)A_BATTERY, .flags = R_AUTH},
            {0},
        },
    },
    {
        /* HID service (HIDS 1.0 Table 2.1) */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = chr_access, .arg = (void *)A_HID_INFO, .flags = R_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = chr_access, .arg = (void *)A_REPORT_MAP, .flags = R_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = chr_access, .arg = (void *)A_CONTROL_POINT,
             .flags = WNR_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = chr_access, .arg = (void *)A_PROTOCOL_MODE,
             .flags = R_AUTH | WNR_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access, .arg = (void *)A_REPORT_KB,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_KB], .descriptors = REF_DSC(HOST_RID_KEYBOARD, 1)},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access, .arg = (void *)A_REPORT_MOUSE,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_MOUSE], .descriptors = REF_DSC(HOST_RID_MOUSE, 1)},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access, .arg = (void *)A_REPORT_CONSUMER,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_CONSUMER],
             .descriptors = REF_DSC(HOST_RID_CONSUMER, 1)},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access, .arg = (void *)A_REPORT_SYSTEM,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_SYSTEM],
             .descriptors = REF_DSC(HOST_RID_SYSTEM, 1)},
            {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_access, .arg = (void *)A_REPORT_LED,
             .flags = R_AUTH | W_AUTH | WNR_AUTH, .descriptors = REF_DSC(HOST_RID_KEYBOARD, 2)},
            {.uuid = BLE_UUID16_DECLARE(0x2A22), .access_cb = chr_access, .arg = (void *)A_BOOT_KB_IN,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_BOOT_KB]},
            {.uuid = BLE_UUID16_DECLARE(0x2A32), .access_cb = chr_access, .arg = (void *)A_BOOT_KB_OUT,
             .flags = R_AUTH | W_AUTH | WNR_AUTH},
            {.uuid = BLE_UUID16_DECLARE(0x2A33), .access_cb = chr_access, .arg = (void *)A_BOOT_MOUSE_IN,
             .flags = R_AUTH | N_AUTH, .val_handle = &s_notify_handle[N_BOOT_MOUSE]},
            {0},
        },
    },
    {0},
};

/* ---------- Host connection state ---------- */

typedef enum { HL_CANDIDATE, HL_CONNECTED, HL_SECURED, HL_READY } hl_state_t;

/*
 * Report delivery (see "Sending"). TX_PIPELINE reports per host may be in the controller, not yet reported completed;
 * changes beyond that wait in a queue of CHANGE_Q.
 */
#define TX_PIPELINE 2
#define TX_RING 8
#define CHANGE_Q 8

#if CONFIG_KM_DIAG
/* Developer builds: per host link, since the last stats */
typedef struct {
    uint16_t nobuf_mouse;        /* Reports the stack could not take */
    uint16_t nobuf_other;
    uint32_t mouse_reports;      /* Mouse reports handed to the stack */
    uint32_t ack_hist[8];        /* From the input reaching the board to the controller reporting the report completed: <15, 15-30, 30-45, 45-60, 60-90, 90-150, 150-300, >=300 ms */
    uint32_t ack_max_ms;
    uint16_t superseded;         /* Queued changes dropped for a later one of the same report (queue full) */
} link_diag_t;
#endif

typedef struct {
    uint8_t n;                   /* N_*: which report */
    uint8_t data[HOST_KB_LEN];   /* The report (mouse: buttons only; motion is added when it goes out) */
    int64_t t;                   /* When it was queued: when the board got the input */
} change_t;

typedef struct {
    bool used;
    uint16_t conn;
    hl_state_t state;
    ble_addr_t id;
    char id_str[18];
    int index;                   /* Host number (from 0). -1 for a candidate */
    uint8_t sub;                 /* Subscription bits (N_*) */
    uint8_t protocol_mode;       /* 1 Report, 0 Boot. Every connection starts in Report (HIDS 1.0 2.4.1.1) */
    bool suspended;
    uint8_t led;
    /* Report delivery (see "Sending"): what the host has received, what it will have once the queue drains */
    host_sent_t sent;
    uint8_t boot_kb[HOST_KB_LEN];
    uint8_t boot_buttons;
    host_sent_t want;
    uint8_t want_boot_kb[HOST_KB_LEN];
    uint8_t want_boot_buttons;
    change_t q[CHANGE_Q];        /* State changes not yet handed to the stack, oldest first */
    uint8_t q_head;
    uint8_t q_len;
    host_motion_t motion;        /* Mouse motion not yet sent */
    int64_t motion_t;            /* When the oldest unsent motion came in (0: none) */
    int64_t tx_t[TX_RING];       /* Reports handed to the stack and not yet completed: when their oldest input came in, oldest first */
    uint8_t tx_head;
    uint8_t tx_count;
    int64_t tx_progress_us;      /* Last submit or completion on this link */
    uint16_t itvl;               /* Connection interval (units of 1.25 ms) */
    uint16_t latency;
    bool pk_wait;                /* Waiting for passkey entry */
    int64_t deadline_us;         /* Candidate: must finish pairing by this time */
    int64_t param_retry_us;      /* If nonzero, re-request connection parameters at this time */
    int64_t sec_req_us;          /* If nonzero, send a Security Request at this time */
    uint8_t param_retries;
    bool short_asked;            /* The last request was the active host's short interval */
    bool short_refused;          /* It was refused on this connection: ask 15 ms instead */
    bool repaired;               /* A bonded host is pairing again inside the window */
#if CONFIG_KM_DIAG
    link_diag_t diag;
#endif
} host_link_t;

static host_link_t s_links[HOST_LINK_MAX];

/* The interval asked of the active host (1.25 ms units). Developer builds can change it at run time (host_itvl) */
static uint16_t s_active_itvl_min = ITVL_ACTIVE;
static uint16_t s_active_itvl_max = ITVL_ACTIVE;
static host_event_fn s_emit;
static bool s_synced;
static bool s_window;
static int64_t s_window_until_us;
static int64_t s_fast_until_us;
static int s_adv_hold;
static int s_adv_mode;           /* 0 off, 1 reconnect, 2 window */
static int s_adv_itvl;
static struct ble_npl_callout s_tick;
static struct ble_npl_callout s_retry;   /* Pumps again shortly after a notify found no buffer and nothing is in flight */

/* Statistics since the last stats read: mouse reports sent, and the time inside ble_gatts_notify_custom */
static uint32_t s_mouse_sent;
static int64_t s_notify_sum_us;
static int64_t s_notify_max_us;

static int host_gap_event(struct ble_gap_event *event, void *arg);
static void adv_update(void);
static void request_conn_params(host_link_t *l);
static host_link_t *link_ensure(uint16_t conn);
static void security_request(host_link_t *l);

static void emit(const char *json)
{
    if (s_emit) {
        s_emit(json);
    }
}

static int64_t now_us(void)
{
    return plat_now_us();
}

static void val_to_str(const uint8_t val[6], char *out)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", val[5], val[4], val[3], val[2], val[1], val[0]);
}

static host_link_t *link_by_conn(uint16_t conn)
{
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        if (s_links[i].used && s_links[i].conn == conn) {
            return &s_links[i];
        }
    }
    return NULL;
}

static host_link_t *link_by_index(int index)
{
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        if (s_links[i].used && s_links[i].index == index && index >= 0) {
            return &s_links[i];
        }
    }
    return NULL;
}

bool host_online(int index)
{
    host_link_t *l = link_by_index(index);
    return l && l->state == HL_READY;
}

/* ---------- Metadata (host bonds) ---------- */

static bool meta_find(const ble_addr_t *id, cfg_paired_t *out)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (recs[i].type == (id->type & 1) && memcmp(recs[i].val, id->val, 6) == 0) {
            *out = recs[i];
            return true;
        }
    }
    return false;
}

static bool bond_exists(const ble_addr_t *id)
{
    ble_addr_t list[CFG_PAIRED_MAX];
    int n = 0;
    if (ble_store_util_bonded_peers(list, &n, CFG_PAIRED_MAX) != 0) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if ((list[i].type & 1) == (id->type & 1) && memcmp(list[i].val, id->val, 6) == 0) {
            return true;
        }
    }
    return false;
}

/* Host number of a bonded host, otherwise -1 */
static int host_index_of(const ble_addr_t *id)
{
    cfg_paired_t m;
    if (meta_find(id, &m) && m.role == CFG_ROLE_HOST && m.host_index >= 1 && bond_exists(id)) {
        return m.host_index - 1;
    }
    return -1;
}

static int host_count(void)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    int c = 0;
    for (int i = 0; i < n; i++) {
        c += recs[i].role == CFG_ROLE_HOST;
    }
    return c;
}

static int free_host_index(void)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    for (int idx = 1; idx <= ROUTER_HOST_MAX; idx++) {
        bool used = false;
        for (int i = 0; i < n; i++) {
            if (recs[i].role == CFG_ROLE_HOST && recs[i].host_index == idx) {
                used = true;
            }
        }
        if (!used) {
            return idx - 1;
        }
    }
    return -1;
}

/* Is any bonded host disconnected (is reconnect advertising needed) */
static bool any_host_offline(void)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (recs[i].role == CFG_ROLE_HOST && recs[i].host_index >= 1 && !link_by_index(recs[i].host_index - 1)) {
            return true;
        }
    }
    return false;
}

/* ---------- GATT access ---------- */

static int append(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len)
{
    return os_mbuf_append(ctxt->om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int read_byte(struct ble_gatt_access_ctxt *ctxt, uint8_t *out)
{
    uint8_t buf[4];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0 || len < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    *out = buf[0];
    return 0;
}

static void set_led(host_link_t *l, uint8_t led)
{
    l->led = led;
    if (l->index >= 0) {
        router_set_host_led(l->index, led);
        if (l->index == router_active_host()) {
            ble_pair_leds_update();
        }
    }
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int which = (int)(uintptr_t)arg;
    host_link_t *l = link_ensure(conn);   /* A host may access attributes before CONNECT is delivered */
    bool write = ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR;
    static const uint8_t zeros[HOST_MOUSE_LEN] = {0};

    switch (which) {
    case A_MANUF:
        return append(ctxt, MANUFACTURER, strlen(MANUFACTURER));
    case A_MODEL:
        return append(ctxt, MODEL, strlen(MODEL));
    case A_FWREV:
        return append(ctxt, KM_FW_VERSION, strlen(KM_FW_VERSION));
    case A_PNP: {
        /* Vendor ID Source 2 (USB-IF), VID 0x303A, PID 0xA001, version 0x0100. Temporary values for personal use (design section 6 Q1) */
        static const uint8_t pnp[7] = {0x02, 0x3A, 0x30, 0x01, 0xA0, 0x00, 0x01};
        return append(ctxt, pnp, sizeof(pnp));
    }
    case A_BATTERY: {
        static const uint8_t full = 100;
        return append(ctxt, &full, 1);
    }
    case A_HID_INFO: {
        /* bcdHID 1.11, country 0, Flags: RemoteWake | NormallyConnectable */
        static const uint8_t info[4] = {0x11, 0x01, 0x00, 0x03};
        return append(ctxt, info, sizeof(info));
    }
    case A_REPORT_MAP:
        return append(ctxt, HOST_REPORT_MAP, HOST_REPORT_MAP_LEN);
    case A_CONTROL_POINT: {
        uint8_t v;
        int rc = read_byte(ctxt, &v);
        if (rc == 0 && l) {
            l->suspended = v == 0;   /* 0 Suspend, 1 Exit Suspend. The spec does not define device-side behavior */
        }
        return rc;
    }
    case A_PROTOCOL_MODE:
        if (write) {
            uint8_t v;
            int rc = read_byte(ctxt, &v);
            if (rc == 0 && l && v <= 1) {
                l->protocol_mode = v;
            }
            return rc;
        }
        return append(ctxt, l ? &l->protocol_mode : (const uint8_t *)"\x01", 1);
    case A_REPORT_KB:
        return append(ctxt, l ? l->sent.kb : zeros, HOST_KB_LEN);
    case A_REPORT_MOUSE: {
        uint8_t m[HOST_MOUSE_LEN] = {l ? l->sent.buttons : 0};
        return append(ctxt, m, sizeof(m));
    }
    case A_REPORT_CONSUMER:
        return append(ctxt, l ? l->sent.consumer : zeros, HOST_CONSUMER_LEN);
    case A_REPORT_SYSTEM:
        return append(ctxt, l ? &l->sent.system : zeros, HOST_SYSTEM_LEN);
    case A_REPORT_LED:
    case A_BOOT_KB_OUT:
        if (write) {
            uint8_t v;
            int rc = read_byte(ctxt, &v);
            if (rc == 0 && l) {
                set_led(l, v);
            }
            return rc;
        }
        return append(ctxt, l ? &l->led : zeros, 1);
    case A_BOOT_KB_IN:
        return append(ctxt, l ? l->boot_kb : zeros, HOST_KB_LEN);
    case A_BOOT_MOUSE_IN: {
        uint8_t m[HOST_BOOT_MOUSE_LEN] = {l ? l->boot_buttons : 0};
        return append(ctxt, m, sizeof(m));
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int ref_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uintptr_t v = (uintptr_t)arg;
    uint8_t ref[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    return append(ctxt, ref, sizeof(ref));
}

/* ---------- Diagnostics (developer builds: CONFIG_KM_DIAG; compiled out otherwise) ---------- */

#if CONFIG_KM_DIAG
static int s_msys_free_min = 1 << 30;   /* Fewest free mbufs seen at a notify since the last stats */

static void diag_ack(host_link_t *l, int64_t ms)
{
    static const int EDGE[7] = {15, 30, 45, 60, 90, 150, 300};
    int b = 0;
    while (b < 7 && ms >= EDGE[b]) {
        b++;
    }
    l->diag.ack_hist[b]++;
    if (ms > l->diag.ack_max_ms) {
        l->diag.ack_max_ms = (uint32_t)ms;
    }
}

static void diag_msys(void)
{
    int free_now = os_msys_num_free();
    if (free_now < s_msys_free_min) {
        s_msys_free_min = free_now;
    }
}

static void diag_nobuf(host_link_t *l, bool mouse)
{
    if (mouse) {
        l->diag.nobuf_mouse++;
    } else {
        l->diag.nobuf_other++;
    }
}

static void diag_mouse(host_link_t *l)
{
    l->diag.mouse_reports++;
}

static void diag_superseded(host_link_t *l)
{
    l->diag.superseded++;
}

/* Global fields of stats, then reset */
static size_t diag_stats_json(char *buf, size_t len)
{
    uint32_t nvs_writes, nvs_max_us, usb_lines, usb_total_us, usb_max_us, usb_dropped;
    cfg_diag_take(&nvs_writes, &nvs_max_us);
    plat_ctl_diag_take(&usb_lines, &usb_total_us, &usb_max_us, &usb_dropped);
    size_t w = snprintf(buf, len,
                        ",\"mbufs\":%d,\"mbufs_free_min\":%d,\"nvs_writes\":%lu,\"nvs_max_us\":%lu,\"usb_lines\":%lu,"
                        "\"usb_avg_us\":%lu,\"usb_max_us\":%lu,\"usb_dropped\":%lu",
                        os_msys_count(), s_msys_free_min == (1 << 30) ? os_msys_num_free() : s_msys_free_min,
                        (unsigned long)nvs_writes, (unsigned long)nvs_max_us, (unsigned long)usb_lines,
                        (unsigned long)(usb_lines ? usb_total_us / usb_lines : 0), (unsigned long)usb_max_us,
                        (unsigned long)usb_dropped);
    s_msys_free_min = 1 << 30;
    return w < len ? w : len;
}

/* Per-link fields of stats, then reset */
static size_t diag_link_json(host_link_t *l, char *buf, size_t len)
{
    int8_t rssi = 0;
    if (l->state == HL_READY && ble_gap_conn_rssi(l->conn, &rssi) != 0) {
        rssi = 0;
    }
    const link_diag_t *d = &l->diag;
    size_t w = snprintf(buf, len,
                        ",\"rssi\":%d,\"mouse_reports\":%lu,\"nobuf_mouse\":%u,\"nobuf_other\":%u,\"superseded\":%u,"
                        "\"ack_ms\":[%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu],\"ack_max_ms\":%lu",
                        rssi, (unsigned long)d->mouse_reports, d->nobuf_mouse, d->nobuf_other, d->superseded,
                        (unsigned long)d->ack_hist[0], (unsigned long)d->ack_hist[1], (unsigned long)d->ack_hist[2],
                        (unsigned long)d->ack_hist[3], (unsigned long)d->ack_hist[4], (unsigned long)d->ack_hist[5],
                        (unsigned long)d->ack_hist[6], (unsigned long)d->ack_hist[7], (unsigned long)d->ack_max_ms);
    memset(&l->diag, 0, sizeof(l->diag));
    return w < len ? w : len;
}
#else
static inline void diag_ack(host_link_t *l, int64_t ms) { (void)l; (void)ms; }
static inline void diag_msys(void) {}
static inline void diag_nobuf(host_link_t *l, bool mouse) { (void)l; (void)mouse; }
static inline void diag_mouse(host_link_t *l) { (void)l; }
static inline void diag_superseded(host_link_t *l) { (void)l; }
static inline size_t diag_stats_json(char *buf, size_t len) { (void)buf; (void)len; return 0; }
static inline size_t diag_link_json(host_link_t *l, char *buf, size_t len) { (void)l; (void)buf; (void)len; return 0; }
#endif

/* ---------- Sending ---------- */

/*
 * Report delivery, as HID devices do it on a slow link (Apple Accessory Design Guidelines R31 20.4.3-20.4.4: queue a
 * new report only after the previous one is acknowledged, then send the most recent data; Nordic nRF Desktop keeps 2
 * reports in flight per host).
 * - At most TX_PIPELINE reports per host are in the controller at once. When the controller reports one as
 *   completed (HCI Number Of Completed Packets), the next one goes out. Two, as nRF Desktop does: if completion
 *   waits for the peer's link-layer acknowledgement, the last packet of a connection event completes only in the
 *   next event.
 * - Every press and release goes out (ADG 55.1: one report per user action): state changes wait in a queue, in
 *   order. Only if the queue fills does an older change give way to a later one of the same report.
 * - Mouse motion adds up and goes out with the next mouse report: the cursor goes to where the mouse is instead of
 *   replaying old motion.
 * So a host that is slow to take reports delays only its own input, a key-up is never lost, and it holds at most
 * TX_PIPELINE of the controller's 12 shared buffers.
 */
static void pump(host_link_t *l);

/*
 * NimBLE passes every HCI event through ble_hs_hci_evt_process(); the linker wraps it (CMakeLists.txt) to see
 * Number Of Completed Packets per connection (NimBLE has no public "sent" event; NOTIFY_TX fires when a notification
 * is queued). The ESP32-S3 controller sends one per packet. Other ATT packets on a host link complete through the
 * same event, so the count may run slightly ahead, never behind.
 */
int __real_ble_hs_hci_evt_process(struct ble_hci_ev *ev);
int __wrap_ble_hs_hci_evt_process(struct ble_hci_ev *ev)
{
    uint16_t done[HOST_LINK_MAX] = {0};
    uint8_t ndone = 0;
    if (ev->opcode == BLE_HCI_EVCODE_NUM_COMP_PKTS && ev->length >= 1) {
        int64_t now = plat_now_us();
        uint8_t count = ev->data[0];
        for (int i = 0; i < count && 1 + i * 4 + 4 <= ev->length; i++) {
            const uint8_t *p = &ev->data[1 + i * 4];
            uint16_t handle = (uint16_t)(p[0] | (p[1] << 8));
            uint16_t pkts = (uint16_t)(p[2] | (p[3] << 8));
            host_link_t *l = link_by_conn(handle);
            if (l && l->tx_count > 0 && ndone < HOST_LINK_MAX) {
                done[ndone++] = handle;
            }
            if (l && pkts > 0) {
                l->tx_progress_us = now;
            }
            for (; l && pkts > 0 && l->tx_count > 0; pkts--) {
                diag_ack(l, (now - l->tx_t[l->tx_head]) / 1000);
                l->tx_head = (uint8_t)((l->tx_head + 1) % TX_RING);
                l->tx_count--;
            }
        }
    }
    int rc = __real_ble_hs_hci_evt_process(ev);   /* Returns the controller buffers to NimBLE first */
    for (int i = 0; i < ndone; i++) {
        host_link_t *l = link_by_conn(done[i]);
        if (l) {
            pump(l);
        }
    }
    return rc;
}

static bool boot_mode(const host_link_t *l)
{
    return l->protocol_mode == 0;
}

/* true once the stack has taken the report; it then counts as in flight until completed. since: when its oldest input came in */
static bool notify(host_link_t *l, int n, const uint8_t *data, uint16_t len, int64_t since)
{
    bool mouse = n == N_MOUSE || n == N_BOOT_MOUSE;
    int64_t t = now_us();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    diag_msys();
    if (!om) {
        diag_nobuf(l, mouse);
        ESP_LOGW(TAG, "notify: no buffer (host %d)", l->index + 1);
        return false;
    }
    int rc = ble_gatts_notify_custom(l->conn, s_notify_handle[n], om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d", rc);
        return false;
    }
    if (l->tx_count < TX_RING) {
        l->tx_t[(l->tx_head + l->tx_count) % TX_RING] = since ? since : t;
        l->tx_count++;
        l->tx_progress_us = t;
    }
    if (mouse) {
        int64_t in_notify = now_us() - t;
        diag_mouse(l);
        s_mouse_sent++;
        s_notify_sum_us += in_notify;
        if (in_notify > s_notify_max_us) {
            s_notify_max_us = in_notify;
        }
    }
    return true;
}

/* What the host has once this report is delivered */
static void delivered(host_link_t *l, int n, const uint8_t *data)
{
    switch (n) {
    case N_KB:
        memcpy(l->sent.kb, data, HOST_KB_LEN);
        break;
    case N_BOOT_KB:
        memcpy(l->boot_kb, data, HOST_KB_LEN);
        break;
    case N_CONSUMER:
        memcpy(l->sent.consumer, data, HOST_CONSUMER_LEN);
        break;
    case N_SYSTEM:
        l->sent.system = data[0];
        break;
    case N_MOUSE:
        l->sent.buttons = data[0];
        break;
    case N_BOOT_MOUSE:
        l->boot_buttons = data[0] & 0x07;
        break;
    default:
        break;
    }
}

/* Hand reports to the stack while fewer than TX_PIPELINE are not yet completed: queued changes first, then motion */
static void pump(host_link_t *l)
{
    while (l->state == HL_READY && l->tx_count < TX_PIPELINE) {
        uint8_t buf[HOST_KB_LEN];
        int n;
        uint16_t len;
        change_t *c = l->q_len ? &l->q[l->q_head] : NULL;
        if (c) {
            n = c->n;
            if (!(l->sub & (1u << n))) {   /* The host does not take this report */
                l->q_head = (uint8_t)((l->q_head + 1) % CHANGE_Q);
                l->q_len--;
                continue;
            }
        } else if (!host_motion_empty(&l->motion)) {
            n = boot_mode(l) ? N_BOOT_MOUSE : N_MOUSE;
            if (!(l->sub & (1u << n))) {
                memset(&l->motion, 0, sizeof(l->motion));
                return;
            }
        } else {
            return;
        }
        host_motion_t motion = l->motion;
        bool mouse = n == N_MOUSE || n == N_BOOT_MOUSE;
        int64_t since = c ? c->t : 0;
        if (mouse && l->motion_t && (!since || l->motion_t < since)) {
            since = l->motion_t;
        }
        if (n == N_MOUSE) {
            host_mouse_report(&motion, c ? c->data[0] : l->want.buttons, buf);
            len = HOST_MOUSE_LEN;
        } else if (n == N_BOOT_MOUSE) {
            host_boot_mouse_report(&motion, c ? c->data[0] : l->want_boot_buttons, buf);
            len = HOST_BOOT_MOUSE_LEN;
        } else {
            memcpy(buf, c->data, sizeof(buf));
            len = n == N_CONSUMER ? HOST_CONSUMER_LEN : n == N_SYSTEM ? HOST_SYSTEM_LEN : HOST_KB_LEN;
        }
        if (!notify(l, n, buf, len, since)) {
            /* No buffer: tried again at the next completion, or after one tick (10 ms) if none is in flight */
            if (l->tx_count == 0 && !ble_npl_callout_is_active(&s_retry)) {
                ble_npl_callout_reset(&s_retry, 1);
            }
            return;
        }
        if (mouse) {
            l->motion = motion;   /* What did not fit in one report stays for the next */
            if (host_motion_empty(&l->motion)) {
                l->motion_t = 0;
            }
        }
        delivered(l, n, buf);
        if (c) {
            l->q_head = (uint8_t)((l->q_head + 1) % CHANGE_Q);
            l->q_len--;
        }
    }
}

/* Queue a state change. A full queue makes room by dropping the oldest change that a later one of the same report supersedes */
static void enqueue(host_link_t *l, int n, const uint8_t *data, uint8_t len)
{
    if (l->q_len == CHANGE_Q) {
        for (int i = 0; i < l->q_len; i++) {
            int a = (l->q_head + i) % CHANGE_Q;
            bool superseded = false;
            for (int j = i + 1; j < l->q_len && !superseded; j++) {
                superseded = l->q[(l->q_head + j) % CHANGE_Q].n == l->q[a].n;
            }
            if (superseded) {
                diag_superseded(l);
                for (int j = i; j < l->q_len - 1; j++) {
                    l->q[(l->q_head + j) % CHANGE_Q] = l->q[(l->q_head + j + 1) % CHANGE_Q];
                }
                l->q_len--;
                break;
            }
        }
    }
    change_t *c = &l->q[(l->q_head + l->q_len) % CHANGE_Q];
    l->q_len++;
    c->n = (uint8_t)n;
    c->t = now_us();
    memset(c->data, 0, sizeof(c->data));
    memcpy(c->data, data, len);
}

static void add_motion(host_link_t *l, int dx, int dy, int wheel, int pan)
{
    if (host_motion_empty(&l->motion)) {
        l->motion_t = now_us();
    }
    host_motion_add(&l->motion, dx, dy, wheel, pan);
}

/* Queue a report if it changes what the host will have */
static void queue_state(host_link_t *l, int n, uint8_t *want, const uint8_t *now, uint8_t len)
{
    if (memcmp(want, now, len) != 0) {
        memcpy(want, now, len);
        enqueue(l, n, now, len);
    }
}

/* Everything released on this connection (the previous host on a switch); changes still queued for it are dropped */
static void release_all(host_link_t *l)
{
    if (!l || l->state != HL_READY) {
        return;
    }
    static const uint8_t zeros[HOST_KB_LEN] = {0};
    l->q_len = 0;
    l->want = l->sent;
    memcpy(l->want_boot_kb, l->boot_kb, HOST_KB_LEN);
    l->want_boot_buttons = l->boot_buttons;
    memset(&l->motion, 0, sizeof(l->motion));
    l->motion_t = 0;
    if (boot_mode(l)) {
        queue_state(l, N_BOOT_KB, l->want_boot_kb, zeros, HOST_KB_LEN);
        queue_state(l, N_BOOT_MOUSE, &l->want_boot_buttons, zeros, 1);
    } else {
        queue_state(l, N_KB, l->want.kb, zeros, HOST_KB_LEN);
        queue_state(l, N_MOUSE, &l->want.buttons, zeros, 1);
        queue_state(l, N_CONSUMER, l->want.consumer, zeros, HOST_CONSUMER_LEN);
        queue_state(l, N_SYSTEM, &l->want.system, zeros, HOST_SYSTEM_LEN);
    }
    pump(l);
}

static void queue_keyboard(host_link_t *l, const uint8_t keys[32])
{
    uint8_t kb[HOST_KB_LEN];
    host_kb_report(keys, kb);
    if (boot_mode(l)) {
        queue_state(l, N_BOOT_KB, l->want_boot_kb, kb, HOST_KB_LEN);
    } else {
        queue_state(l, N_KB, l->want.kb, kb, HOST_KB_LEN);
    }
}

void host_send(const router_out_t *out, int switched_from)
{
    if (switched_from >= 0) {
        /* Release on the previous host's own connection (ZMK sends it after switching, so it never reaches the previous host: zmk issue #2202) */
        release_all(link_by_index(switched_from));
        host_link_t *old = link_by_index(switched_from);
        if (old) {
            old->param_retries = 0;
            request_conn_params(old);   /* Now idle: long peripheral latency */
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"event\":\"host_online\",\"host\":%d,\"online\":%s}",
                 router_active_host() + 1, host_online(router_active_host()) ? "true" : "false");
        emit(buf);
    }
    host_link_t *l = link_by_index(router_active_host());
    if (!l || l->state != HL_READY) {
        return;   /* Drop input for a disconnected host (key input replayed later is dangerous) */
    }
    if (switched_from >= 0) {
        release_all(l);           /* Zero anything left over on the new host */
        l->param_retries = 0;
        request_conn_params(l);   /* Now active: short peripheral latency */
    }
    if (out->pre_valid) {
        queue_keyboard(l, out->pre_keys);
    }
    if (out->keys_changed || out->pre_valid) {
        queue_keyboard(l, out->keys);
    }
    if (!boot_mode(l)) {   /* A Boot host ignores Report Protocol reports */
        if (out->consumer_changed) {
            uint8_t c[HOST_CONSUMER_LEN];
            host_consumer_report(out->consumer, out->consumer_count, c);
            queue_state(l, N_CONSUMER, l->want.consumer, c, HOST_CONSUMER_LEN);
        }
        if (out->system_changed) {
            uint8_t v = out->system & 0x07;
            queue_state(l, N_SYSTEM, &l->want.system, &v, HOST_SYSTEM_LEN);
        }
    }
    if (out->buttons_changed) {
        uint8_t b = out->buttons;
        if (boot_mode(l)) {
            b &= 0x07;
            queue_state(l, N_BOOT_MOUSE, &l->want_boot_buttons, &b, 1);
        } else {
            queue_state(l, N_MOUSE, &l->want.buttons, &b, 1);
        }
    }
    if (out->motion) {
        add_motion(l, out->dx, out->dy, out->wheel, out->pan);
    }
    pump(l);
}

#if CONFIG_KM_DIAG
#if CONFIG_KM_DIAG
esp_err_t host_set_active_itvl(uint16_t min, uint16_t max)
{
    if (min < 6 || max < min || max > 3200) {
        return ESP_ERR_INVALID_ARG;
    }
    s_active_itvl_min = min;
    s_active_itvl_max = max;
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        s_links[i].short_refused = false;   /* Try the new value on every host again */
    }
    host_link_t *l = link_by_index(router_active_host());
    if (l && l->state == HL_READY) {
        l->param_retries = 0;
        request_conn_params(l);
    }
    return ESP_OK;
}
#endif

/*
 * Self-test (developer builds): mouse reports to one host at the input interval (every 8.75 ms), 16 counts right
 * then 16 left so the cursor stays within a few pixels, through the same delivery path as real input, while stats
 * measure it. Meant for a host not in use: it stops when that host becomes the active one, when its link drops, or
 * when the time is up.
 */
#define SELFTEST_PERIOD_US 8750
static int s_selftest_host = -1;
static int64_t s_selftest_until_us;
static uint32_t s_selftest_n;
static plat_timer_t *s_selftest_timer;
static struct ble_npl_event s_selftest_ev;

static void selftest_stop(const char *why)
{
    if (s_selftest_host < 0) {
        return;
    }
    plat_timer_stop(s_selftest_timer);
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"selftest_stopped\",\"host\":%d,\"reason\":\"%s\",\"reports\":%lu}",
             s_selftest_host + 1, why, (unsigned long)s_selftest_n);
    emit(buf);
    s_selftest_host = -1;
}

static void selftest_ev_cb(struct ble_npl_event *ev)
{
    if (s_selftest_host < 0) {
        return;
    }
    host_link_t *l = link_by_index(s_selftest_host);
    if (!l || l->state != HL_READY) {
        selftest_stop("link");
    } else if (s_selftest_host == router_active_host()) {
        selftest_stop("active");
    } else if (now_us() >= s_selftest_until_us) {
        selftest_stop("done");
    } else {
        add_motion(l, (s_selftest_n++ / 16) % 2 ? -1 : 1, 0, 0, 0);
        pump(l);
    }
}

static void selftest_timer_cb(void *arg)
{
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_selftest_ev);
}

esp_err_t host_selftest(int host, int minutes)
{
    if (host < 0) {
        selftest_stop("stopped");
        return ESP_OK;
    }
    host_link_t *l = link_by_index(host);
    if (!l || l->state != HL_READY || host == router_active_host() || minutes <= 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_selftest_timer) {
        ble_npl_event_init(&s_selftest_ev, selftest_ev_cb, NULL);
        s_selftest_timer = plat_timer_create(selftest_timer_cb, NULL);
        if (!s_selftest_timer) {
            return ESP_ERR_NO_MEM;
        }
    }
    selftest_stop("restarted");
    s_selftest_host = host;
    s_selftest_n = 0;
    s_selftest_until_us = now_us() + (int64_t)minutes * 60 * 1000000;
    plat_timer_start_periodic(s_selftest_timer, SELFTEST_PERIOD_US);
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"event\":\"selftest_started\",\"host\":%d,\"minutes\":%d}", host + 1, minutes);
    emit(buf);
    return ESP_OK;
}
#endif

/* ---------- Advertising ---------- */

static void adv_stop(void)
{
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    s_adv_mode = 0;
}

static void adv_start(int mode, int itvl)
{
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof(f));
    /* Window: Limited Discoverable. Reconnect: non-discoverable (the flags only affect discovery; bonded hosts still connect) */
    f.flags = BLE_HS_ADV_F_BREDR_UNSUP | (mode == 2 ? BLE_HS_ADV_F_DISC_LTD : 0);
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    f.uuids16 = &hid_uuid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.appearance = APPEARANCE_KEYBOARD;
    f.appearance_is_present = 1;
    f.name = (const uint8_t *)DEVICE_NAME;
    f.name_len = strlen(DEVICE_NAME);
    f.name_is_complete = 1;   /* In the advertising packet itself, so a scanner sees the name without a scan request */
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields rc=%d", rc);
        return;
    }
    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.tx_pwr_lvl_is_present = 1;
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof(p));
    p.conn_mode = BLE_GAP_CONN_MODE_UND;   /* Directed advertising is not used (Apple Accessory Design Guidelines R31 58) */
    p.disc_mode = mode == 2 ? BLE_GAP_DISC_MODE_LTD : BLE_GAP_DISC_MODE_NON;
    p.itvl_min = itvl;
    p.itvl_max = itvl;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &p, host_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv start rc=%d", rc);   /* ENOMEM if no connection slot is free (ble_gap.c). Re-decided once a slot frees up */
        return;
    }
    s_adv_mode = mode;
    s_adv_itvl = itvl;
}

/* Decide the advertising state: window > reconnect for disconnected bonded hosts > off */
static void adv_update(void)
{
    if (!s_synced || s_adv_hold > 0) {
        adv_stop();
        return;
    }
    int64_t now = now_us();
    if (s_window && now >= s_window_until_us) {
        host_pair_window(false);
        return;
    }
    int mode = 0;
    int itvl = ADV_ITVL_SLOW;
    if (s_window && host_count() < ROUTER_HOST_MAX) {
        mode = 2;
        itvl = now < s_fast_until_us ? ADV_ITVL_WINDOW_FAST : ADV_ITVL_SLOW;
    } else if (any_host_offline()) {
        mode = 1;
        itvl = now < s_fast_until_us ? ADV_ITVL_RECONNECT_FAST : ADV_ITVL_SLOW;
    }
    if (mode == s_adv_mode && itvl == s_adv_itvl && (mode == 0 || ble_gap_adv_active())) {
        return;
    }
    adv_stop();
    if (mode) {
        adv_start(mode, itvl);
    }
}

static void retry_cb(struct ble_npl_event *ev)
{
    (void)ev;
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        if (s_links[i].used && s_links[i].state == HL_READY) {
            pump(&s_links[i]);
        }
    }
}

/* Every second: check the window time, fast advertising time and candidate connection time */
static void tick_cb(struct ble_npl_event *ev)
{
    int64_t now = now_us();
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        host_link_t *l = &s_links[i];
        if (l->used && l->index < 0 && now >= l->deadline_us) {
            ble_gap_terminate(l->conn, HCI_USER_TERM);
        }
        if (l->used && l->param_retry_us && now >= l->param_retry_us) {
            l->param_retry_us = 0;
            request_conn_params(l);
        }
        if (l->used && l->sec_req_us && now >= l->sec_req_us) {
            security_request(l);
        }
        if (l->used && l->state == HL_READY) {
            if (l->tx_count > 0 && now - l->tx_progress_us > 2000000) {
                /* A completion never came (the link drops long before this in practice): stop waiting for it */
                ESP_LOGW(TAG, "host %d: %u reports never completed", l->index + 1, l->tx_count);
                l->tx_count = 0;
            }
            pump(l);   /* Anything left after a notify found no buffer */
        }
    }
    adv_update();
    ble_npl_callout_reset(&s_tick, ble_npl_time_ms_to_ticks32(1000));
}

static void fast_adv(void)
{
    s_fast_until_us = now_us() + (int64_t)FAST_ADV_MS * 1000;
}

void host_adv_hold(bool hold)
{
    s_adv_hold += hold ? 1 : -1;
    if (s_adv_hold < 0) {
        s_adv_hold = 0;
    }
    adv_update();
}

/* ---------- Host connection events ---------- */

static void emit_link(const char *event, const host_link_t *l, const char *extra)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"host\":%d,\"addr\":\"%s\"%s}", event, l->index + 1, l->id_str,
             extra ? extra : "");
    emit(buf);
}

/*
 * Peripheral latency: how many connection events the board may skip on a host link while it has nothing to send. It
 * never delays a report (the board answers at the next event whenever it has data). Idle hosts only need to stay
 * connected, so they get Apple's maximum and leave the board's radio to the active host and the input devices; the
 * active host gets 4, so its LED writes wait at most 75 ms. Switching is decided on the board, which re-requests both.
 * Apple ADG R31 58.6: latency <= 30, interval x (latency + 1) <= 6 s (15 ms x 31 = 465 ms), supervision timeout
 * > 3 x that (6 s).
 */
#define LATENCY_ACTIVE 4
#define LATENCY_IDLE 30

static void request_conn_params(host_link_t *l)
{
    /* Latency 4 or 30, supervision 6 s (Apple ADG R31 58.6; Microsoft Bluetooth Accessory Guidelines 4.4) */
    bool active = l->index == router_active_host();
    l->short_asked = active && !l->short_refused && s_active_itvl_min < ITVL_HOST_MIN;
    struct ble_gap_upd_params p = {
        .itvl_min = l->short_asked ? s_active_itvl_min : ITVL_HOST_MIN,
        .itvl_max = l->short_asked ? s_active_itvl_max : ITVL_HOST_MAX,
        .latency = active ? LATENCY_ACTIVE : LATENCY_IDLE,
        .supervision_timeout = 600, .min_ce_len = 0, .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(l->conn, &p);
    if (rc != 0 && l->short_asked && rc != BLE_HS_ENOMEM && rc != BLE_HS_EALREADY && rc != BLE_HS_EBUSY) {
        /* The controller refused the short interval (HCI 0x12 seen for a Mac link): ask 15 ms now */
        l->short_refused = true;
        request_conn_params(l);
        return;
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "conn param request rc=%d", rc);
        /*
         * ENOMEM/EALREADY/EBUSY: another connection's update (or this one's) is still in progress.
         * Real hardware: two hosts became ready at the same moment and the second request got ENOMEM.
         */
        if ((rc == BLE_HS_ENOMEM || rc == BLE_HS_EALREADY || rc == BLE_HS_EBUSY) && l->param_retries < PARAM_RETRY_MAX) {
            l->param_retries++;
            l->param_retry_us = now_us() + (int64_t)PARAM_RETRY_MS * 1000;
        }
    }
}

/*
 * If a bonded host reconnects but does not start encryption, we send a Security Request
 * (Core v5.4 Vol 3 Part H 2.4.6: when the Peripheral requests security, the Central encrypts with the stored keys).
 * Real hardware: links looked unencrypted for 90+ seconds after a reconnect. The causes turned out to be events
 * arriving before CONNECT (link_ensure) and LTK requests with no free SM slot (firmware/CMakeLists.txt); this
 * stays as the standard fallback for a host that waits for the peripheral. If the SM pool is full (ENOMEM),
 * retry after 1 second.
 */
static void security_request(host_link_t *l)
{
    if (l->state != HL_CONNECTED) {
        l->sec_req_us = 0;
        return;
    }
    int rc = ble_gap_security_initiate(l->conn);
    if (rc == BLE_HS_ENOMEM) {
        l->sec_req_us = now_us() + 1000000;
        return;
    }
    l->sec_req_us = 0;
    char extra[32];
    snprintf(extra, sizeof(extra), ",\"rc\":%d", rc);
    emit_link("host_sec_request", l, extra);
}

static void on_host_name(uint16_t conn, const char *name)
{
    host_link_t *l = link_by_conn(conn);
    if (!l || l->index < 0 || !name[0]) {
        return;
    }
    cfg_paired_t m;
    if (meta_find(&l->id, &m)) {
        snprintf(m.name, sizeof(m.name), "%s", name);
        cfg_paired_put(&m);
        char esc[48];
        char extra[80];
        json_escape(name, esc, sizeof(esc));
        snprintf(extra, sizeof(extra), ",\"name\":\"%s\"", esc);
        emit_link("host_name", l, extra);
    }
}

static void link_ready(host_link_t *l)
{
    if (l->state == HL_READY) {
        return;
    }
    l->state = HL_READY;
    emit_link("host_ready", l, NULL);
    request_conn_params(l);
    /* Read the host's GAP name to show in the list (on a Mac, the computer name) */
    gap_name_read(l->conn, on_host_name);
}

/*
 * Create the link state for a host connection, or return the existing one.
 *
 * NimBLE (ESP-IDF 6.1) delivers BLE_GAP_EVENT_CONNECT for a Peripheral connection only after the remote
 * version and feature exchange (ble_gap.c: ble_gap_rx_rd_rem_sup_feat_complete). A bonded host that starts
 * encryption right away can finish it first, and NimBLE then delivers ENC_CHANGE and the restored
 * SUBSCRIBE events before CONNECT. Real hardware: two Macs reconnecting at once; a Mac encrypted 0.3 s
 * after connecting while CONNECT arrived later, and the link stayed "connected" with no reports.
 * So every event handler calls this, and the first event of a connection creates the link.
 */
static host_link_t *link_ensure(uint16_t conn)
{
    host_link_t *l = link_by_conn(conn);
    if (l) {
        return l;
    }
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0 || d.role != BLE_GAP_ROLE_SLAVE) {
        return NULL;   /* Not a host connection (input devices are on the Central side, ble_pair.c) */
    }
    s_adv_mode = 0;   /* Advertising ends once a Peripheral connection forms (ble_gap.c) */
    int index = host_index_of(&d.peer_id_addr);
    for (int i = 0; i < HOST_LINK_MAX && !l; i++) {
        if (!s_links[i].used) {
            l = &s_links[i];
        }
    }
    /* Accept only a bonded host, or a new one while the window is open with a slot left. No advertising filter (bonded hosts may use RPAs), so filter here */
    bool accept = l && (index >= 0 ? !link_by_index(index) : (s_window && host_count() < ROUTER_HOST_MAX));
    if (!accept) {
        ble_gap_terminate(conn, HCI_USER_TERM);
        adv_update();
        return NULL;
    }
    memset(l, 0, sizeof(*l));
    l->used = true;
    l->conn = conn;
    l->id = d.peer_id_addr;
    l->id.type &= 1;
    val_to_str(l->id.val, l->id_str);
    l->index = index;
    l->state = index >= 0 ? HL_CONNECTED : HL_CANDIDATE;
    l->protocol_mode = 1;
    l->itvl = d.conn_itvl;
    l->latency = d.conn_latency;
    l->deadline_us = now_us() + (int64_t)CANDIDATE_MS * 1000;
    if (index >= 0) {
        l->led = router_host_led(index);
        l->sec_req_us = now_us() + (int64_t)SEC_REQ_MS * 1000;
    }
    char extra[96];
    snprintf(extra, sizeof(extra), ",\"bonded\":%s,\"itvl\":%u", index >= 0 ? "true" : "false", d.conn_itvl);
    emit_link("host_connected", l, extra);
    adv_update();
    return l;
}

static void link_drop(host_link_t *l, int reason);

static void on_connect(struct ble_gap_event *event)
{
    if (event->connect.status != 0) {
        /*
         * NimBLE reports a Peripheral connection that drops before its CONNECT event as a failed CONNECT
         * (status BLE_HS_EAGAIN, ble_gap.c ble_gap_rx_disconn_complete), not as DISCONNECT. An earlier event (a
         * bonded host encrypting first) may already have created the link, so free it here too.
         */
        s_adv_mode = 0;
        host_link_t *l = link_by_conn(event->connect.conn_handle);
        if (l) {
            link_drop(l, event->connect.status);
        } else {
            adv_update();
        }
        return;
    }
    if (!link_ensure(event->connect.conn_handle)) {
        adv_update();
    }
}

static void link_drop(host_link_t *l, int reason)
{
    if (l->pk_wait) {
        router_passkey_end();
    }
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"reason\":%d", reason);
    emit_link("host_disconnected", l, extra);
    l->used = false;
    fast_adv();   /* Advertise fast right after a disconnect so the host reconnects (HOGP Table 5.2) */
    adv_update();
}

static void on_disconnect(struct ble_gap_event *event)
{
    host_link_t *l = link_by_conn(event->disconnect.conn.conn_handle);
    if (!l) {
        adv_update();
        return;
    }
    link_drop(l, event->disconnect.reason);
}

static void on_enc_change(struct ble_gap_event *event)
{
    host_link_t *l = link_ensure(event->enc_change.conn_handle);
    if (!l) {
        return;
    }
    if (l->pk_wait) {
        l->pk_wait = false;
        router_passkey_end();
    }
    struct ble_gap_conn_desc d;
    bool ok = event->enc_change.status == 0 && ble_gap_conn_find(l->conn, &d) == 0;
    bool good = ok && d.sec_state.encrypted && d.sec_state.bonded && d.sec_state.authenticated &&
                d.sec_state.key_size == 16;
    if (!good) {
        char extra[96];
        snprintf(extra, sizeof(extra), ",\"status\":%d,\"authenticated\":%s", event->enc_change.status,
                 ok && d.sec_state.authenticated ? "true" : "false");
        emit_link("host_pair_failed", l, extra);
        /* Disconnect. After the security timeout (30 seconds) no more SMP can be sent on that link; it must reconnect (Vol 3 Part H 3.4) */
        if (ok && l->index < 0) {
            /* Do not accept an unauthenticated new bond (HID characteristics need an authenticated link) */
            ble_addr_t id = l->id;
            ble_gap_terminate(l->conn, HCI_USER_TERM);
            ble_store_util_delete_peer(&id);
        } else {
            ble_gap_terminate(l->conn, HCI_USER_TERM);
        }
        return;
    }
    /*
     * Key distribution is done, so peer_id_addr is the identity. The value at connect time may be an RPA
     * (Macs connect with an RPA).
     * The bond store is keyed by identity, so the metadata must be written by identity too for the two to match.
     */
    l->id = d.peer_id_addr;
    l->id.type &= 1;
    val_to_str(l->id.val, l->id_str);
    if (l->index < 0) {
        /* New host: assign the lowest free number */
        int index = free_host_index();
        if (index < 0) {
            ble_gap_unpair(&l->id);
            return;
        }
        cfg_paired_t m;
        memset(&m, 0, sizeof(m));
        m.type = l->id.type & 1;
        memcpy(m.val, l->id.val, 6);
        m.role = CFG_ROLE_HOST;
        m.host_index = (uint8_t)(index + 1);
        cfg_paired_put(&m);
        l->index = index;
        emit_link("host_paired", l, NULL);
        host_pair_window(false);   /* One host paired, so close the window */
    } else if (l->repaired) {
        l->repaired = false;
        emit_link("host_paired", l, NULL);   /* A known host paired again: same number */
        host_pair_window(false);
    }
    l->state = HL_SECURED;
    /* A bonded host has its subscriptions (CCCD) restored from the store. If already on, it is ready right away */
    if (l->sub) {
        link_ready(l);
    }
}

static void on_subscribe(struct ble_gap_event *event)
{
    host_link_t *l = link_ensure(event->subscribe.conn_handle);
    if (!l) {
        return;
    }
    for (int n = 0; n < N_COUNT; n++) {
        if (s_notify_handle[n] == event->subscribe.attr_handle) {
            if (event->subscribe.cur_notify) {
                l->sub |= (uint8_t)(1u << n);
            } else {
                l->sub &= (uint8_t)~(1u << n);
            }
        }
    }
    if (l->sub && l->state == HL_SECURED) {
        link_ready(l);
    }
}

static void on_passkey_action(struct ble_gap_event *event)
{
    host_link_t *l = link_ensure(event->passkey.conn_handle);
    if (!l) {
        return;
    }
    if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        /* Take the 6 digits shown on the host screen. Router keyboard input goes to no host during this */
        l->pk_wait = true;
        release_all(link_by_index(router_active_host()));   /* So nothing stays pressed on the active host */
        router_passkey_begin();
        emit_link("host_passkey_request", l, NULL);
    } else {
        ESP_LOGW(TAG, "unexpected passkey action %u", event->passkey.params.action);
    }
}

static void inject_passkey(host_link_t *l, uint32_t code)
{
    struct ble_sm_io io = {.action = BLE_SM_IOACT_INPUT, .passkey = code};
    int rc = ble_sm_inject_io(l->conn, &io);
    l->pk_wait = false;
    router_passkey_end();
    char extra[32];
    snprintf(extra, sizeof(extra), ",\"ok\":%s", rc == 0 ? "true" : "false");
    emit_link("host_passkey_sent", l, extra);
}

static host_link_t *pk_link(void)
{
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        if (s_links[i].used && s_links[i].pk_wait) {
            return &s_links[i];
        }
    }
    return NULL;
}

void host_passkey_event(const router_out_t *out)
{
    host_link_t *l = pk_link();
    if (!l) {
        return;
    }
    if (out->pk_event == ROUTER_PK_DONE) {
        inject_passkey(l, out->pk_code);
    } else if (out->pk_event == ROUTER_PK_CANCEL) {
        l->pk_wait = false;
        router_passkey_end();
        ble_gap_terminate(l->conn, HCI_USER_TERM);
    } else if (out->pk_event == ROUTER_PK_PROGRESS) {
        char extra[32];
        snprintf(extra, sizeof(extra), ",\"len\":%u", out->pk_len);
        emit_link("host_passkey_progress", l, extra);
    }
}

esp_err_t host_passkey(uint32_t code)
{
    host_link_t *l = pk_link();
    if (!l) {
        return ESP_ERR_INVALID_STATE;
    }
    inject_passkey(l, code);
    return ESP_OK;
}

static int on_repeat_pairing(struct ble_gap_event *event)
{
    host_link_t *l = link_ensure(event->repeat_pairing.conn_handle);
    /*
     * A bonded host is trying to pair again (it removed us on the host side).
     * Only inside the window, and only if the new pairing is authenticated, delete the old keys and accept again.
     * Metadata (number, name) is kept.
     * Outside the window, reject and disconnect (prevents address spoofing that gets a bond deleted before
     * authentication).
     */
    if (s_window && event->repeat_pairing.new_authenticated) {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
            if (l) {
                l->repaired = true;
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
    }
    if (l) {
        emit_link("host_repair_blocked", l, NULL);
        ble_gap_terminate(l->conn, HCI_USER_TERM);
    }
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
}

static int host_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        on_connect(event);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnect(event);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        on_enc_change(event);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        on_subscribe(event);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        on_passkey_action(event);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return on_repeat_pairing(event);
    case BLE_GAP_EVENT_CONN_UPDATE: {
        host_link_t *l = link_by_conn(event->conn_update.conn_handle);
        struct ble_gap_conn_desc d;
        if (l && ble_gap_conn_find(l->conn, &d) == 0) {
            l->itvl = d.conn_itvl;
            l->latency = d.conn_latency;
            /*
             * Our request is rejected if it collides with the peer's (host's) connection parameter procedure
             * (HCI 0x2A Different Transaction Collision, 0x23 LL Procedure Collision). Real hardware: 0x2A right
             * after a Mac reconnect. Request again after a short delay.
             * It is only a request: if the host rejects it or picks other values, those values stand. Rejections
             * other than collisions are not retried.
             */
            int st = event->conn_update.status;
            bool collision = st == BLE_HS_HCI_ERR(BLE_ERR_DIFF_TRANS_COLL) || st == BLE_HS_HCI_ERR(BLE_ERR_LMP_COLLISION);
            if (st == 0) {
                l->param_retries = 0;
            }
            if (collision && l->param_retries < PARAM_RETRY_MAX) {
                l->param_retries++;
                l->param_retry_us = now_us() + (int64_t)PARAM_RETRY_MS * 1000;
            } else if (st != 0 && l->short_asked) {
                /* The host refused the short interval: ask 15 ms */
                l->short_refused = true;
                l->param_retry_us = now_us();
            }
            char extra[96];
            snprintf(extra, sizeof(extra), ",\"status\":%d,\"itvl\":%u,\"latency\":%u,\"timeout\":%u",
                     event->conn_update.status, d.conn_itvl, d.conn_latency, d.supervision_timeout);
            emit_link("host_conn_params", l, extra);
        }
        break;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_adv_mode = 0;
        adv_update();
        break;
    default:
        break;
    }
    return 0;
}

/* ---------- Commands ---------- */

bool host_window_open(void)
{
    return s_window;
}

esp_err_t host_pair_window(bool open)
{
    if (open && host_count() >= ROUTER_HOST_MAX) {
        return ESP_ERR_NO_MEM;   /* All 3 host slots are in use. The GUI picks a host to remove */
    }
    if (open == s_window) {
        return ESP_OK;
    }
    s_window = open;
    /* Present as a keyboard only while the window is open (Passkey Entry: we type the digits shown on the host screen. Core v6.0 Vol 3 Part H 2.3.5.1 table 2.8; NimBLE reads sm_io_cap for each pairing) */
    ble_hs_cfg.sm_io_cap = open ? BLE_HS_IO_KEYBOARD_ONLY : BLE_HS_IO_KEYBOARD_DISPLAY;
    if (open) {
        s_window_until_us = now_us() + (int64_t)WINDOW_MS * 1000;
        fast_adv();
    } else {
        /* Disconnect candidate connections that did not finish pairing */
        for (int i = 0; i < HOST_LINK_MAX; i++) {
            if (s_links[i].used && s_links[i].index < 0) {
                ble_gap_terminate(s_links[i].conn, HCI_USER_TERM);
            }
        }
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"host_window\",\"open\":%s,\"seconds\":%d}", open ? "true" : "false",
             open ? WINDOW_MS / 1000 : 0);
    emit(buf);
    adv_update();
    return ESP_OK;
}

esp_err_t host_remove(int index)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (recs[i].role != CFG_ROLE_HOST || recs[i].host_index != index + 1) {
            continue;
        }
        ble_addr_t id = {.type = recs[i].type};
        memcpy(id.val, recs[i].val, 6);
        host_adv_hold(true);   /* ble_gap_unpair returns EBUSY while advertising */
        int rc = ble_gap_unpair(&id);
        host_adv_hold(false);
        if (rc != 0 && rc != BLE_HS_ENOENT) {
            return ESP_FAIL;
        }
        cfg_paired_remove(recs[i].type, recs[i].val);
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"event\":\"host_removed\",\"host\":%d}", index + 1);
        emit(buf);
        adv_update();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static bool host_record(int index, cfg_paired_t *out)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (recs[i].role == CFG_ROLE_HOST && recs[i].host_index == index + 1) {
            *out = recs[i];
            return true;
        }
    }
    return false;
}

esp_err_t host_move(int index, int to)
{
    cfg_paired_t m;
    if (index < 0 || index >= ROUTER_HOST_MAX || to < 0 || to >= ROUTER_HOST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_record(index, &m)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (index == to) {
        return ESP_OK;
    }
    esp_err_t err = cfg_paired_swap_hosts((uint8_t)(index + 1), (uint8_t)(to + 1));
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        host_link_t *l = &s_links[i];
        if (l->used && (l->index == index || l->index == to)) {
            l->index = l->index == index ? to : index;
        }
    }
#if CONFIG_KM_DIAG
    if (s_selftest_host == index || s_selftest_host == to) {
        s_selftest_host = s_selftest_host == index ? to : index;
    }
#endif
    router_swap_hosts(index, to);
    cfg_active_host_set(router_active_host());
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"event\":\"host_moved\",\"host\":%d,\"to\":%d}", index + 1, to + 1);
    emit(buf);
    return ESP_OK;
}

esp_err_t host_nick(int index, const char *nick)
{
    cfg_paired_t m;
    int chars = utf8_char_count(nick);
    if (chars < 0 || chars > CFG_NICK_CHARS || strlen(nick) > CFG_NICK_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_record(index, &m)) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(m.nick, sizeof(m.nick), "%s", nick);
    esp_err_t err = cfg_paired_put(&m);
    if (err != ESP_OK) {
        return err;
    }
    char esc[2 * CFG_NICK_MAX + 1];
    char buf[128];
    json_escape(nick, esc, sizeof(esc));
    snprintf(buf, sizeof(buf), "{\"event\":\"host_nick\",\"host\":%d,\"nick\":\"%s\"}", index + 1, esc);
    emit(buf);
    return ESP_OK;
}

size_t host_list_json(char *buf, size_t len)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    size_t w = snprintf(buf, len, "[");
    bool first = true;
    for (int idx = 1; idx <= ROUTER_HOST_MAX; idx++) {
        for (int i = 0; i < n; i++) {
            if (recs[i].role != CFG_ROLE_HOST || recs[i].host_index != idx) {
                continue;
            }
            host_link_t *l = link_by_index(idx - 1);
            char id[18];
            char name[48];
            char nick[2 * CFG_NICK_MAX + 1];
            char item[320];
            val_to_str(recs[i].val, id);
            json_escape(recs[i].name, name, sizeof(name));
            json_escape(recs[i].nick, nick, sizeof(nick));
            static const char *names[] = {"pairing", "connected", "secured", "ready"};
            int m = snprintf(item, sizeof(item),
                             "%s{\"host\":%d,\"addr\":\"%s\",\"name\":\"%s\",\"nick\":\"%s\",\"connected\":%s,"
                             "\"state\":\"%s\",\"active\":%s}",
                             first ? "" : ",", idx, id, name, nick, l ? "true" : "false", l ? names[l->state] : "offline",
                             router_active_host() == idx - 1 ? "true" : "false");
            if (m < 0 || w + (size_t)m + 2 > len) {
                break;
            }
            memcpy(buf + w, item, m + 1);
            w += m;
            first = false;
        }
    }
    w += snprintf(buf + w, len - w, "]");
    return w < len ? w : len - 1;
}

/* ---------- Initialization ---------- */

esp_err_t host_init(host_event_fn emit_fn)
{
    s_emit = emit_fn;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt services rc=%d", rc);
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_svc_gap_device_appearance_set(APPEARANCE_KEYBOARD);
    ble_npl_callout_init(&s_tick, nimble_port_get_dflt_eventq(), tick_cb, NULL);
    ble_npl_callout_init(&s_retry, nimble_port_get_dflt_eventq(), retry_cb, NULL);
    return ESP_OK;
}

/*
 * Bonded hosts cache the GATT table (handles, the Report Map) and look again only when told (Service Changed,
 * Core v6.0 Vol 3 Part G 2.5.2 and 7.1). When a firmware update changes the table, the whole range is marked changed:
 * NimBLE indicates it to connected hosts and, for bonded hosts that subscribed, at their next connection
 * (ble_gatts_chr_updated keeps the flag in the bond store). Bump GATT_LAYOUT whenever services or characteristics
 * change; the Report Map is covered by its hash.
 */
#define GATT_LAYOUT 1

static uint32_t gatt_signature(void)
{
    uint32_t h = 2166136261u ^ GATT_LAYOUT;   /* FNV-1a */
    for (size_t i = 0; i < HOST_REPORT_MAP_LEN; i++) {
        h = (h ^ HOST_REPORT_MAP[i]) * 16777619u;
    }
    return h;
}

void host_on_sync(void)
{
    s_synced = true;
    uint32_t sig = gatt_signature();
    if (cfg_gatt_sig_get() != sig) {
        ble_svc_gatt_changed(0x0001, 0xFFFF);
        cfg_gatt_sig_set(sig);
    }
    fast_adv();   /* Right after boot, let disconnected bonded hosts reconnect quickly */
    adv_update();
    ble_npl_callout_reset(&s_tick, ble_npl_time_ms_to_ticks32(1000));
}

void host_on_reset(void)
{
    s_synced = false;
    s_adv_mode = 0;
    memset(s_links, 0, sizeof(s_links));
}

size_t host_stats_json(char *buf, size_t len)
{
    int64_t navg = s_mouse_sent ? s_notify_sum_us / s_mouse_sent : 0;
    size_t w = snprintf(buf, len, "{\"mouse_sent\":%lu,\"notify_avg_us\":%lld,\"notify_max_us\":%lld",
                        (unsigned long)s_mouse_sent, (long long)navg, (long long)s_notify_max_us);
    if (w < len) {
        w += diag_stats_json(buf + w, len - w);
    }
    if (w < len) {
        w += snprintf(buf + w, len - w, ",\"hosts\":[");
    }
    bool first = true;
    for (int i = 0; i < HOST_LINK_MAX && w < len; i++) {
        host_link_t *l = &s_links[i];
        if (!l->used || l->index < 0) {
            continue;
        }
        w += snprintf(buf + w, len - w, "%s{\"host\":%d,\"itvl\":%u,\"latency\":%u", first ? "" : ",",
                      l->index + 1, l->itvl, l->latency);
        if (w < len) {
            w += diag_link_json(l, buf + w, len - w);
        }
        if (w < len) {
            w += snprintf(buf + w, len - w, "}");
        }
        first = false;
    }
    if (w < len) {
        w += snprintf(buf + w, len - w, "]}");
    }
    s_mouse_sent = 0;
    s_notify_sum_us = 0;
    s_notify_max_us = 0;
    return w < len ? w : len - 1;
}
