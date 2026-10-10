#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

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
 * Connection interval request: 15 ms (units of 1.25 ms).
 * - macOS gives 15 ms at the least (Apple ADG), and takes a 15 ms request at once.
 * - A 7.5-15 ms range (as ZMK asks) was refused by the board's own controller before it reached the host
 *   (HCI 0x12 on LE Connection Update) for 3 of 6 requests with an input device connected; the host then kept its
 *   own parameters (seen: latency 0 with a 720 ms supervision timeout). A 15 ms request was never refused (~110).
 * - An 11.25 ms request was answered with status 0 while macOS 26 kept 30 ms, then moved to 15 ms by itself.
 */
#define ITVL_HOST_MIN 12
#define ITVL_HOST_MAX 12
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

/* ---------- GATT table (fixed handles: no Service Changed, so do not reorder. Core v6.0 Vol 3 Part G 2.5.2) ---------- */

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
    host_sent_t sent;
    uint8_t boot_kb[HOST_KB_LEN];
    uint8_t boot_buttons;
    host_motion_t motion;
    uint8_t buttons;
    uint16_t itvl;               /* Connection interval (units of 1.25 ms) */
    uint16_t latency;
    bool pk_wait;                /* Waiting for passkey entry */
    int64_t deadline_us;         /* Candidate: must finish pairing by this time */
    int64_t param_retry_us;      /* If nonzero, re-request connection parameters at this time */
    int64_t sec_req_us;          /* If nonzero, send a Security Request at this time */
    uint8_t param_retries;
} host_link_t;

static host_link_t s_links[HOST_LINK_MAX];
static host_event_fn s_emit;
static bool s_synced;
static bool s_window;
static int64_t s_window_until_us;
static int64_t s_fast_until_us;
static int s_adv_hold;
static int s_adv_mode;           /* 0 off, 1 reconnect, 2 window */
static int s_adv_itvl;
static struct ble_npl_callout s_tick;

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
    return esp_timer_get_time();
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

int host_link_count(void)
{
    int n = 0;
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        n += s_links[i].used;
    }
    return n;
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

/* ---------- Sending ---------- */

/* true once the stack has taken the report (it then goes out at the next connection event) */
static bool notify(host_link_t *l, int n, const uint8_t *data, uint16_t len)
{
    if (!(l->sub & (1u << n)) || l->state != HL_READY) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGW(TAG, "notify: no buffer");
        return false;
    }
    int rc = ble_gatts_notify_custom(l->conn, s_notify_handle[n], om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d", rc);
    }
    return rc == 0;
}

/* Send a report if it differs from what this host last received. last changes only when the stack took it, so a failed report goes out with the next change */
static void send_changed(host_link_t *l, int n, uint8_t *last, const uint8_t *now, uint16_t len)
{
    if (memcmp(last, now, len) != 0 && notify(l, n, now, len)) {
        memcpy(last, now, len);
    }
}

static bool boot_mode(const host_link_t *l)
{
    return l->protocol_mode == 0;
}

static void send_keyboard(host_link_t *l, const uint8_t keys[32])
{
    uint8_t kb[HOST_KB_LEN];
    host_kb_report(keys, kb);
    if (boot_mode(l)) {
        send_changed(l, N_BOOT_KB, l->boot_kb, kb, HOST_KB_LEN);
    } else {
        send_changed(l, N_KB, l->sent.kb, kb, HOST_KB_LEN);
    }
}

static void send_consumer(host_link_t *l, const uint16_t *usages, uint8_t count)
{
    if (boot_mode(l)) {
        return;   /* A Boot host ignores Report Protocol reports */
    }
    uint8_t c[HOST_CONSUMER_LEN];
    host_consumer_report(usages, count, c);
    send_changed(l, N_CONSUMER, l->sent.consumer, c, HOST_CONSUMER_LEN);
}

/* System Control: power down, sleep, wake up (same bit order as router_out_t.system) */
static void send_system(host_link_t *l, uint8_t bits)
{
    if (boot_mode(l)) {
        return;
    }
    uint8_t v = bits & 0x07;
    send_changed(l, N_SYSTEM, &l->sent.system, &v, HOST_SYSTEM_LEN);
}

/*
 * Mouse: each report goes out at once, as a mouse sends at its next connection event; the stack queues it if the
 * link is busy. Motion beyond what one report carries (16-bit, or 8-bit in Boot Protocol) follows in more reports.
 * Button changes go out with the motion so far.
 */
static void send_mouse(host_link_t *l, const router_out_t *o)
{
    if (o->motion) {
        host_motion_add(&l->motion, o->dx, o->dy, o->wheel, o->pan);
    }
    bool button_change = o->buttons != l->buttons;
    l->buttons = o->buttons;
    if (!o->motion && !button_change) {
        return;
    }
    do {
        int64_t t = now_us();
        if (boot_mode(l)) {
            uint8_t b[HOST_BOOT_MOUSE_LEN];
            host_boot_mouse_report(&l->motion, l->buttons, b);
            if (notify(l, N_BOOT_MOUSE, b, sizeof(b))) {
                l->boot_buttons = b[0];
            }
        } else {
            uint8_t m[HOST_MOUSE_LEN];
            host_mouse_report(&l->motion, l->buttons, m);
            if (notify(l, N_MOUSE, m, sizeof(m))) {
                l->sent.buttons = l->buttons;
            }
        }
        int64_t in_notify = now_us() - t;
        s_mouse_sent++;
        s_notify_sum_us += in_notify;
        if (in_notify > s_notify_max_us) {
            s_notify_max_us = in_notify;
        }
    } while (!host_motion_empty(&l->motion));
}

/* Zero any report left nonzero on this connection (to the previous host on switch) */
static void release_all(host_link_t *l)
{
    if (!l || l->state != HL_READY) {
        return;
    }
    uint8_t zeros[HOST_KB_LEN > HOST_MOUSE_LEN ? HOST_KB_LEN : HOST_MOUSE_LEN] = {0};
    if (boot_mode(l)) {
        send_changed(l, N_BOOT_KB, l->boot_kb, zeros, HOST_KB_LEN);
        if (l->boot_buttons && notify(l, N_BOOT_MOUSE, zeros, HOST_BOOT_MOUSE_LEN)) {
            l->boot_buttons = 0;
        }
    } else {
        send_changed(l, N_KB, l->sent.kb, zeros, HOST_KB_LEN);
        if (l->sent.buttons && notify(l, N_MOUSE, zeros, HOST_MOUSE_LEN)) {
            l->sent.buttons = 0;
        }
        send_changed(l, N_CONSUMER, l->sent.consumer, zeros, HOST_CONSUMER_LEN);
        send_changed(l, N_SYSTEM, &l->sent.system, zeros, HOST_SYSTEM_LEN);
    }
    memset(&l->motion, 0, sizeof(l->motion));
    l->buttons = 0;
}

void host_send(const router_out_t *out, int switched_from)
{
    if (switched_from >= 0) {
        /* Send the release directly on the previous host's connection (regardless of the active value. ZMK sends it after switching, so it never reaches the previous host: zmk issue #2202) */
        release_all(link_by_index(switched_from));
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
        release_all(l);   /* Zero anything left over on the new host */
    }
    if (out->pre_valid) {
        send_keyboard(l, out->pre_keys);
    }
    if (out->keys_changed || out->pre_valid) {
        send_keyboard(l, out->keys);
    }
    if (out->consumer_changed) {
        send_consumer(l, out->consumer, out->consumer_count);
    }
    if (out->system_changed) {
        send_system(l, out->system);
    }
    if (out->buttons_changed || out->motion) {
        send_mouse(l, out);
    }
}

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
    f.name_is_complete = 1;   /* Windows does not active-scan, so the name goes in the advertising packet (Microsoft Swift Pair) */
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

static void request_conn_params(host_link_t *l)
{
    /* 15 ms, Latency 4, supervision 6 s (Apple ADG R31 58.6; Microsoft Bluetooth Accessory Guidelines 4.4) */
    struct ble_gap_upd_params p = {
        .itvl_min = ITVL_HOST_MIN, .itvl_max = ITVL_HOST_MAX, .latency = 4, .supervision_timeout = 600,
        .min_ce_len = 0, .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(l->conn, &p);
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
    if (!l || l->index < 0) {
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

static void on_connect(struct ble_gap_event *event)
{
    if (event->connect.status != 0) {
        s_adv_mode = 0;
        adv_update();
        return;
    }
    if (!link_ensure(event->connect.conn_handle)) {
        adv_update();
    }
}

static void on_disconnect(struct ble_gap_event *event)
{
    host_link_t *l = link_by_conn(event->disconnect.conn.conn_handle);
    if (!l) {
        adv_update();
        return;
    }
    if (l->pk_wait) {
        router_passkey_end();
    }
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"reason\":%d", event->disconnect.reason);
    emit_link("host_disconnected", l, extra);
    l->used = false;
    fast_adv();   /* Advertise fast right after a disconnect so the host reconnects (HOGP Table 5.2) */
    adv_update();
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
            if ((st == BLE_HS_HCI_ERR(BLE_ERR_DIFF_TRANS_COLL) || st == BLE_HS_HCI_ERR(BLE_ERR_LMP_COLLISION)) &&
                l->param_retries < PARAM_RETRY_MAX) {
                l->param_retries++;
                l->param_retry_us = now_us() + (int64_t)PARAM_RETRY_MS * 1000;
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
            char item[200];
            val_to_str(recs[i].val, id);
            json_escape(recs[i].name, name, sizeof(name));
            static const char *names[] = {"pairing", "connected", "secured", "ready"};
            int m = snprintf(item, sizeof(item),
                             "%s{\"host\":%d,\"addr\":\"%s\",\"name\":\"%s\",\"connected\":%s,\"state\":\"%s\","
                             "\"active\":%s}",
                             first ? "" : ",", idx, id, name, l ? "true" : "false", l ? names[l->state] : "offline",
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
    return ESP_OK;
}

void host_on_sync(void)
{
    s_synced = true;
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
    size_t w = snprintf(buf, len, "{\"mouse_sent\":%lu,\"notify_avg_us\":%lld,\"notify_max_us\":%lld,\"hosts\":[",
                        (unsigned long)s_mouse_sent, (long long)navg, (long long)s_notify_max_us);
    bool first = true;
    for (int i = 0; i < HOST_LINK_MAX && w < len; i++) {
        const host_link_t *l = &s_links[i];
        if (!l->used || l->index < 0) {
            continue;
        }
        w += snprintf(buf + w, len - w, "%s{\"host\":%d,\"itvl\":%u,\"latency\":%u}", first ? "" : ",",
                      l->index + 1, l->itvl, l->latency);
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
