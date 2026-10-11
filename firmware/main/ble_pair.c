#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

#include "ble_pair.h"
#include "cfg_store.h"
#include "hid_input.h"
#include "gap_name.h"
#include "host.h"
#include "hid_map.h"
#include "json_mini.h"
#include "router.h"

/* UUID (Bluetooth Assigned Numbers) */
#define UUID_HID_SERVICE 0x1812
#define UUID_HID_INFO 0x2A4A
#define UUID_REPORT_MAP 0x2A4B
#define UUID_REPORT 0x2A4D
#define UUID_CCCD 0x2902
#define UUID_REPORT_REF 0x2908
/* Treat it as an input device if the advertised appearance is in the HID category (0x03C0-0x03FF) */
#define HID_APPEARANCE_MASK 0xFFC0
#define HID_APPEARANCE_BASE 0x03C0
#define CHR_PROP_WRITE_NR 0x04
#define CHR_PROP_WRITE 0x08
#define CHR_PROP_NOTIFY 0x10
#define REPORT_TYPE_INPUT 1
#define REPORT_TYPE_OUTPUT 2

#define FOUND_MAX 16
#define PEER_MAX 2           /* Concurrent input connections (design value: 2 input + 3 host) */
#define SVC_MAX 3            /* HID service instances per device (HOGP: there may be several) */
#define REPORT_MAX 16        /* Total input reports per device */
#define CHR_MAX 48
#define MAP_MAX 512          /* Maximum Report Map length per the HID Service spec */
#define SCAN_MS 60000        /* Scanning for new devices stops by itself after 60 seconds (so auto-reconnect is not blocked for long) */
#define CONNECT_MS 10000     /* Time limit for a connection attempt to a user-selected device */
#define FAIL_LIMIT 3         /* Stop auto-reconnect for a device that disconnects this many times in a row before becoming ready */
#define HCI_USER_TERM 0x13   /* HCI reason: remote user terminated connection */
#define MONITOR_LEASE_MS 60000  /* Input content goes out this long after each monitor request (the UI renews it) */
#define ACTIVITY_MS 250      /* At most one activity event per input device in this time */

/* The NimBLE pool overrides in firmware/CMakeLists.txt must reach NimBLE (host reconnects depend on them) */
_Static_assert(MYNEWT_VAL(BLE_SM_MAX_PROCS) == CONFIG_BT_NIMBLE_MAX_CONNECTIONS, "BLE_SM_MAX_PROCS override missing");
_Static_assert(MYNEWT_VAL(BLE_GAP_MAX_PENDING_CONN_PARAM_UPDATE) == CONFIG_BT_NIMBLE_MAX_CONNECTIONS,
               "BLE_GAP_MAX_PENDING_CONN_PARAM_UPDATE override missing");

static const char *TAG = "pair";

/* ---------- State (all owned by the NimBLE host task) ---------- */

typedef struct {
    bool used;
    ble_addr_t addr;         /* Advertising address. If the controller resolved an RPA, it arrives with an identity type (2, 3) */
    char addr_str[18];
    char name[24];
    uint16_t appearance;
    int8_t rssi;
    bool hid;                /* The advertisement or scan response had the HID UUID or an HID appearance */
    bool announced;          /* The device_found event was sent */
    TickType_t seen;
} found_dev_t;

typedef enum { PS_CONNECTED, PS_SECURING, PS_DISCOVERING, PS_READY } peer_state_t;

typedef enum {
    ST_MTU, ST_SVC, ST_CHR, ST_DSC, ST_REF, ST_INFO, ST_MAP, ST_SUB, ST_DONE
} disc_stage_t;

typedef struct {
    uint16_t start;
    uint16_t end;
    uint16_t info_handle;
    uint16_t map_handle;
    uint16_t map_len;
    bool map_truncated;
    uint8_t map[MAP_MAX];
    hid_layout_t layout;     /* Input fields parsed from the Report Map (used by the router) */
    bool layout_ok;
} hid_svc_t;

typedef struct {
    uint16_t val;            /* Value handle of the Report characteristic (0x2A4D): input (notify) or output (write) */
    uint16_t cccd;           /* 0 if absent */
    uint16_t ref;            /* Report Reference descriptor, 0 if absent */
    uint8_t svc;
    uint8_t report_id;       /* ID from the Report Reference. 0 if there is no descriptor (a device with a single report) */
    uint8_t type;            /* From the Report Reference: 1 input, 2 output, 3 feature; 0 if unknown */
    uint8_t props;           /* Characteristic properties */
} report_t;

typedef struct {
    bool used;
    uint16_t conn;
    peer_state_t state;
    bool user;               /* User-selected connection (not an auto-reconnect) */
    bool sec_wait;           /* Waiting for another connection's security procedure to finish */
    uint8_t io_action;       /* Pairing input awaiting a response (BLE_SM_IOACT_*) */
    ble_addr_t ota;          /* On-air address of the connection */
    char addr_str[18];
    bool id_valid;           /* Identity address confirmed after encryption */
    ble_addr_t id;
    char id_str[18];
    /* GATT discovery */
    disc_stage_t stage;
    uint8_t idx;
    hid_svc_t svc[SVC_MAX];
    uint8_t svc_count;
    uint16_t chr_def[CHR_MAX];
    uint8_t chr_count;
    report_t rep[REPORT_MAX];
    uint8_t rep_count;
    uint8_t kinds;
    uint16_t mtu;            /* ATT MTU agreed at the start of discovery */
    int64_t activity_us;     /* Last activity event (esp_timer time) */
    bool led_written;        /* led_last reached the device */
    uint8_t led_last;        /* Host LED byte last written to the device's LED output report */
    bool att_busy;           /* A request (read, write with response) is waiting for its response */
    bool name_done;          /* Device Name read started (once per connection) */
} peer_t;

typedef enum { RADIO_IDLE, RADIO_SCAN, RADIO_CONNECT, RADIO_AUTO, RADIO_CANCEL } radio_t;

/* Auto-reconnect exclusion marks. Devices the user disconnected or that failed repeatedly (RAM only, cleared on reboot) */
typedef struct {
    bool used;
    ble_addr_t id;
    uint8_t fails;
    bool paused;
} auto_ctl_t;

static ble_pair_event_fn s_emit;
static uint8_t s_own_addr_type;
static bool s_synced;
static found_dev_t s_found[FOUND_MAX];
static peer_t s_peers[PEER_MAX];
static auto_ctl_t s_auto[CFG_PAIRED_MAX];

static radio_t s_radio;
static bool s_want_scan;
static bool s_user_pending;          /* There is a user-selected connection target */
static ble_addr_t s_user_target;
static char s_user_target_str[18];
static bool s_radio_hold;            /* An operation that needs the radio stopped (such as bond deletion) is in progress */
static bool s_auto_dirty;            /* Auto-reconnect targets changed (rebuild the accept list) */
static peer_t *s_sec_owner;          /* Input connection currently running a security procedure (inputs start one at a time) */

static char s_evt[1500];             /* For long events such as the Report Map */

static int gap_event(struct ble_gap_event *event, void *arg);
static void radio_update(void);
static void disc_next(peer_t *p);

/* NimBLE bond store init. There is no public header, so declare it directly as the ESP-IDF example (blecent) does */
void ble_store_config_init(void);

static void emit(const char *json)
{
    if (s_emit) {
        s_emit(json);
    }
}

/* ---------- Addresses ---------- */

static void val_to_str(const uint8_t val[6], char *out)
{
    /* NimBLE stores address bytes in reverse order, so write them backwards */
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             val[5], val[4], val[3], val[2], val[1], val[0]);
}

static bool str_to_val(const char *s, uint8_t val[6])
{
    unsigned b[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        val[i] = (uint8_t)b[5 - i];
    }
    return true;
}

/* Identity address types are 0 (public), 1 (random). Types 2 and 3 resolved by the controller are the same device */
static bool addr_eq(const ble_addr_t *a, const ble_addr_t *b)
{
    return (a->type & 1) == (b->type & 1) && memcmp(a->val, b->val, 6) == 0;
}

static ble_addr_t id_norm(const ble_addr_t *a)
{
    ble_addr_t r = *a;
    r.type &= 1;
    return r;
}

/* ---------- Bonds (source of truth: NimBLE store) ---------- */

/*
 * Marker that tells a new pairing apart from "encryption restored with stored keys".
 * A new pairing writes the peer keys (PEER_SEC) to the store; a restore does not.
 * NimBLE calls store callbacks while holding the ble_hs lock, so the hook only records the address.
 */
static ble_store_write_fn *s_store_write_next;
static ble_addr_t s_new_bond_addr;
static bool s_new_bond_pending;

static int store_write_hook(int obj_type, const union ble_store_value *val)
{
    if (obj_type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        s_new_bond_addr = val->sec.peer_addr;
        s_new_bond_pending = true;
    }
    return s_store_write_next(obj_type, val);
}

/* If the host reinitializes the store during sync, the callback reverts to the original, so reinstall on every sync */
static void install_store_hook(void)
{
    if (ble_hs_cfg.store_write_cb != store_write_hook) {
        s_store_write_next = ble_hs_cfg.store_write_cb;
        ble_hs_cfg.store_write_cb = store_write_hook;
    }
}

static int bonds_get(ble_addr_t *out, int max)
{
    int n = 0;
    if (ble_store_util_bonded_peers(out, &n, max) != 0) {
        return 0;
    }
    return n;
}

static bool bonds_has(const ble_addr_t *a)
{
    ble_addr_t list[CFG_PAIRED_MAX];
    int n = bonds_get(list, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (addr_eq(&list[i], a)) {
            return true;
        }
    }
    return false;
}

/* Find a bond by address bytes (the user gives only the address string, without a type) */
static bool bonds_find_val(const uint8_t val[6], ble_addr_t *out)
{
    ble_addr_t list[CFG_PAIRED_MAX];
    int n = bonds_get(list, CFG_PAIRED_MAX);
    for (int i = 0; i < n; i++) {
        if (memcmp(list[i].val, val, 6) == 0) {
            *out = list[i];
            return true;
        }
    }
    return false;
}

static int bond_count_local(void)
{
    int count = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) != 0) {
        return -1;
    }
    return count;
}

static bool meta_get(const ble_addr_t *id, cfg_paired_t *out)
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

static uint32_t meta_next_seq(void)
{
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int n = cfg_paired_list(recs, CFG_PAIRED_MAX);
    uint32_t max = 0;
    for (int i = 0; i < n; i++) {
        if (recs[i].last_seq > max) {
            max = recs[i].last_seq;
        }
    }
    return max + 1;
}

/*
 * Reconcile the two stores. Bonds are the source of truth.
 * - Metadata without a bond: there are no keys, so it cannot connect. Delete the metadata.
 * - Bond without metadata (orphan bond): create empty metadata so it shows in the list and can be removed.
 */
static void bonds_reconcile(void)
{
    ble_addr_t bonds[CFG_PAIRED_MAX];
    int nb = bonds_get(bonds, CFG_PAIRED_MAX);
    cfg_paired_t recs[CFG_PAIRED_MAX];
    int nm = cfg_paired_list(recs, CFG_PAIRED_MAX);

    for (int i = 0; i < nm; i++) {
        ble_addr_t a = {.type = recs[i].type};
        memcpy(a.val, recs[i].val, 6);
        if (!bonds_has(&a)) {
            cfg_paired_remove(recs[i].type, recs[i].val);
            ESP_LOGW(TAG, "meta without bond removed");
        }
    }
    for (int i = 0; i < nb; i++) {
        cfg_paired_t m;
        if (!meta_get(&bonds[i], &m)) {
            cfg_paired_t rec = {.type = bonds[i].type & 1};
            memcpy(rec.val, bonds[i].val, 6);
            cfg_paired_put(&rec);
            ESP_LOGW(TAG, "orphan bond: empty meta created");
        }
    }
}

/* ---------- Auto-reconnect exclusion marks ---------- */

static auto_ctl_t *auto_ctl(const ble_addr_t *id, bool create)
{
    for (int i = 0; i < CFG_PAIRED_MAX; i++) {
        if (s_auto[i].used && addr_eq(&s_auto[i].id, id)) {
            return &s_auto[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < CFG_PAIRED_MAX; i++) {
        if (!s_auto[i].used) {
            memset(&s_auto[i], 0, sizeof(s_auto[i]));
            s_auto[i].used = true;
            s_auto[i].id = id_norm(id);
            return &s_auto[i];
        }
    }
    return NULL;
}

static void auto_resume(const ble_addr_t *id)
{
    auto_ctl_t *a = auto_ctl(id, false);
    if (a) {
        a->used = false;
    }
}

/* ---------- Found devices ---------- */

static bool is_hid_fields(const struct ble_hs_adv_fields *f)
{
    for (int i = 0; i < f->num_uuids16; i++) {
        if (ble_uuid_u16(&f->uuids16[i].u) == UUID_HID_SERVICE) {
            return true;
        }
    }
    return f->appearance_is_present && (f->appearance & HID_APPEARANCE_MASK) == HID_APPEARANCE_BASE;
}

static found_dev_t *found_get(const ble_addr_t *a)
{
    for (int i = 0; i < FOUND_MAX; i++) {
        if (s_found[i].used && addr_eq(&s_found[i].addr, a)) {
            return &s_found[i];
        }
    }
    return NULL;
}

static found_dev_t *found_by_val(const uint8_t val[6])
{
    for (int i = 0; i < FOUND_MAX; i++) {
        if (s_found[i].used && memcmp(s_found[i].addr.val, val, 6) == 0) {
            return &s_found[i];
        }
    }
    return NULL;
}

/* If no slot is free, evict the device seen longest ago */
static found_dev_t *found_alloc(void)
{
    TickType_t now = xTaskGetTickCount();
    found_dev_t *oldest = &s_found[0];
    for (int i = 0; i < FOUND_MAX; i++) {
        if (!s_found[i].used) {
            return &s_found[i];
        }
        if ((TickType_t)(now - s_found[i].seen) > (TickType_t)(now - oldest->seen)) {
            oldest = &s_found[i];
        }
    }
    return oldest;
}

static void emit_found(const found_dev_t *d)
{
    char name[48];
    char buf[224];
    json_escape(d->name, name, sizeof(name));
    snprintf(buf, sizeof(buf),
             "{\"event\":\"device_found\",\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,"
             "\"paired\":%s,\"appearance\":%u}",
             d->addr_str, name, d->rssi, bonds_has(&d->addr) ? "true" : "false", d->appearance);
    emit(buf);
}

/*
 * Merge advertisements and scan responses by address. Some devices put the name and UUIDs only in the scan response.
 * Directed advertising is a reconnect request from a bonded device, so auto-reconnect handles it and it is not listed.
 */
static void note_adv(const struct ble_gap_disc_desc *d)
{
    if (d->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        d->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        return;
    }
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) {
        return;
    }
    found_dev_t *e = found_get(&d->addr);
    if (!e) {
        if (d->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
            return;   /* The matching advertisement has not been seen yet */
        }
        e = found_alloc();
        memset(e, 0, sizeof(*e));
        e->used = true;
        e->addr = d->addr;
        val_to_str(d->addr.val, e->addr_str);
    }
    bool changed = false;
    e->rssi = d->rssi;
    e->seen = xTaskGetTickCount();
    if (f.name_len > 0 && (f.name_is_complete || e->name[0] == '\0')) {
        char name[sizeof(e->name)];
        size_t n = f.name_len < sizeof(name) - 1 ? f.name_len : sizeof(name) - 1;
        memcpy(name, f.name, n);
        name[n] = '\0';
        if (strcmp(name, e->name) != 0) {
            memcpy(e->name, name, sizeof(name));
            changed = true;
        }
    }
    if (f.appearance_is_present && e->appearance != f.appearance) {
        e->appearance = f.appearance;
        changed = true;
    }
    if (!e->hid && is_hid_fields(&f)) {
        e->hid = true;
        changed = true;
    }
    if (e->hid && (changed || !e->announced)) {
        e->announced = true;
        emit_found(e);
    }
}

/* ---------- Connections ---------- */

static peer_t *peer_by_conn(uint16_t conn)
{
    for (int i = 0; i < PEER_MAX; i++) {
        if (s_peers[i].used && s_peers[i].conn == conn) {
            return &s_peers[i];
        }
    }
    return NULL;
}

/*
 * Create the peer state for an input connection, or return the existing one.
 *
 * NimBLE (ESP-IDF 6.1) delivers BLE_GAP_EVENT_CONNECT only after the remote feature and version exchange
 * (ble_gap.c). A device that sends a Security Request right after connecting makes NimBLE start encryption
 * or pairing by itself (ble_sm.c: ble_sm_sec_req_rx), so ENC_CHANGE or PASSKEY_ACTION can arrive before
 * CONNECT. The host side hit this on real hardware (host.c: link_ensure). The first event creates the peer
 * so that none of these is lost; CONNECT then finishes the radio bookkeeping.
 */
static peer_t *peer_ensure(uint16_t conn)
{
    peer_t *p = peer_by_conn(conn);
    if (p) {
        return p;
    }
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0 || desc.role != BLE_GAP_ROLE_MASTER) {
        return NULL;   /* Host connections are handled by host.c */
    }
    for (int i = 0; i < PEER_MAX && !p; i++) {
        if (!s_peers[i].used) {
            p = &s_peers[i];
        }
    }
    if (!p) {
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->conn = conn;
    p->state = PS_CONNECTED;
    p->ota = desc.peer_ota_addr;
    val_to_str(desc.peer_ota_addr.val, p->addr_str);
    /* A bonded device (auto-reconnect, RPA resolved) has a known identity at connect time. A new one is confirmed after encryption */
    p->id = id_norm(&desc.peer_id_addr);
    val_to_str(p->id.val, p->id_str);
    p->id_valid = bonds_has(&p->id);
    return p;
}

/* Find a connection by identity (once confirmed) or on-air address */
static peer_t *peer_by_addr(const ble_addr_t *a)
{
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (p->used && ((p->id_valid && addr_eq(&p->id, a)) || addr_eq(&p->ota, a))) {
            return p;
        }
    }
    return NULL;
}

static peer_t *peer_by_val(const uint8_t val[6])
{
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (p->used && ((p->id_valid && memcmp(p->id.val, val, 6) == 0) ||
                        memcmp(p->ota.val, val, 6) == 0)) {
            return p;
        }
    }
    return NULL;
}

static int peers_free(void)
{
    int n = 0;
    for (int i = 0; i < PEER_MAX; i++) {
        if (!s_peers[i].used) {
            n++;
        }
    }
    return n;
}

static const char *state_name(peer_state_t s)
{
    switch (s) {
    case PS_CONNECTED:
        return "connected";
    case PS_SECURING:
        return "securing";
    case PS_DISCOVERING:
        return "discovering";
    default:
        return "ready";
    }
}

/* End one connection as failed. Report the reason and disconnect. The slot is freed on the DISCONNECT event */
static void fail_peer(peer_t *p, const char *stage, int status)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"event\":\"failed\",\"addr\":\"%s\",\"stage\":\"%s\",\"status\":%d}",
             p->addr_str, stage, status);
    emit(buf);
    ble_gap_terminate(p->conn, HCI_USER_TERM);
}

/* ---------- Security procedure serialization (input devices start one at a time) ---------- */

/*
 * The SM procedure pool has one slot per connection (firmware/CMakeLists.txt; the ESP-IDF default is 1).
 * Input devices still start security one at a time, so a host's LTK request always finds a free slot.
 * ENOMEM means the pool is full anyway. This module's connection events cannot tell when another
 * procedure ends, so retry on ENC_CHANGE from the global GAP listener and a 1-second timer.
 */
static struct ble_npl_callout s_sec_retry;
static struct ble_gap_event_listener s_gap_listener;
static void sec_pump(void);

static void sec_retry_cb(struct ble_npl_event *ev)
{
    sec_pump();
}

static int gap_listener(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_ENC_CHANGE || event->type == BLE_GAP_EVENT_DISCONNECT) {
        sec_pump();
    }
    return 0;
}

static void sec_pump(void)
{
    if (s_sec_owner) {
        return;
    }
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (!p->used || !p->sec_wait) {
            continue;
        }
        int rc = ble_gap_security_initiate(p->conn);
        if (rc == 0 || rc == BLE_HS_EALREADY) {
            /* EALREADY: the stack already started because of a Security Request the peer sent first */
            p->sec_wait = false;
            p->state = PS_SECURING;
            s_sec_owner = p;
            return;
        }
        if (rc == BLE_HS_ENOMEM) {
            /* Another connection's procedure is running. Retry when it ends (listener) or after 1 second (timer) */
            ble_npl_callout_reset(&s_sec_retry, ble_npl_time_ms_to_ticks32(1000));
            return;
        }
        p->sec_wait = false;
        fail_peer(p, "security_start", rc);
    }
}

/* ---------- Radio state machine (NimBLE runs one Central procedure at a time: ble_gap_master) ---------- */

/* Is this a bond recorded as a host in the metadata? Hosts are not input device auto-connect targets */
static bool is_host_bond(const ble_addr_t *id)
{
    cfg_paired_t m;
    return meta_get(id, &m) && m.role == CFG_ROLE_HOST;
}

static int auto_candidates(ble_addr_t *out, int max)
{
    ble_addr_t bonds[CFG_PAIRED_MAX];
    int nb = bonds_get(bonds, CFG_PAIRED_MAX);
    int n = 0;
    for (int i = 0; i < nb && n < max; i++) {
        if (peer_by_addr(&bonds[i]) || is_host_bond(&bonds[i])) {
            continue;
        }
        auto_ctl_t *a = auto_ctl(&bonds[i], false);
        if (a && a->paused) {
            continue;
        }
        out[n++] = id_norm(&bonds[i]);
    }
    return n;
}

static radio_t radio_wanted(void)
{
    if (s_radio_hold) {
        return RADIO_IDLE;
    }
    if (s_user_pending) {
        return RADIO_CONNECT;
    }
    if (s_want_scan) {
        return RADIO_SCAN;
    }
    ble_addr_t list[CFG_PAIRED_MAX];
    if (peers_free() > 0 && auto_candidates(list, CFG_PAIRED_MAX) > 0) {
        return RADIO_AUTO;
    }
    return RADIO_IDLE;
}

/*
 * Input connection parameters: 8.75 ms, Latency 0, supervision timeout 4 s.
 * - The board adds on average half the input interval to a report's trip (the host link wait is the same as with
 *   the device connected directly), so shorter is better.
 * - Host links run on each host's clock and input links on ours, so their timing drifts and they collide now and
 *   then whatever we pick; an interval that is not a multiple or divisor of the host interval (15 ms on macOS)
 *   spreads those collisions instead of bunching them while the timings pass each other.
 * - With a host connected the controller refuses 7.5 ms (HCI 0x0D, every time on real hardware).
 * If the device later requests other values, they are accepted as asked (the controller may refuse them).
 * Connection-attempt scan: user connections scan continuously (to find it fast). Auto-reconnect follows HOGP 1.0
 * 5.2.3 Table 5.5: 30 ms every 60 ms for AUTO_FAST_MS after a device drops (or at start), then 11.25 ms every
 * 1.28 s for as long as it takes. Listening half the time indefinitely (as before) took radio time from every
 * connection whenever a keyboard or mouse slept.
 * Units: interval 1.25 ms, scan 0.625 ms, supervision 10 ms.
 */
static const struct ble_gap_conn_params s_conn_user = {
    .scan_itvl = 0x0010, .scan_window = 0x0010, .itvl_min = 7, .itvl_max = 7,
    .latency = 0, .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};
static const struct ble_gap_conn_params s_conn_auto_fast = {
    .scan_itvl = 0x0060, .scan_window = 0x0030, .itvl_min = 7, .itvl_max = 7,
    .latency = 0, .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};
static const struct ble_gap_conn_params s_conn_auto_slow = {
    .scan_itvl = 0x0800, .scan_window = 0x0012, .itvl_min = 7, .itvl_max = 7,
    .latency = 0, .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};
#define AUTO_FAST_MS 30000
static int64_t s_auto_fast_until_us;   /* Auto-reconnect listens fast until then */

static void radio_start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 0x0050,
        .window = 0x0030,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,            /* Active scan: also catches devices whose name and UUIDs are in the scan response */
        .filter_duplicates = 0,  /* Receive both advertisements and scan responses and merge them (note_adv) */
    };
    int rc = ble_gap_disc(s_own_addr_type, SCAN_MS, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan start failed rc=%d", rc);
        s_want_scan = false;
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"event\":\"scan_stopped\",\"reason\":\"error\",\"status\":%d}", rc);
        emit(buf);
        return;
    }
    s_radio = RADIO_SCAN;
    emit("{\"event\":\"scan_started\"}");
}

static void radio_start_connect(void)
{
    int rc = ble_gap_connect(s_own_addr_type, &s_user_target, CONNECT_MS, &s_conn_user, gap_event, NULL);
    if (rc != 0) {
        s_user_pending = false;
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"event\":\"connect_failed\",\"addr\":\"%s\",\"status\":%d}",
                 s_user_target_str, rc);
        emit(buf);
        return;
    }
    s_radio = RADIO_CONNECT;
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"connecting\",\"addr\":\"%s\"}", s_user_target_str);
    emit(buf);
}

/*
 * If starting auto-reconnect fails, try again after AUTO_RETRY_MS. Real hardware: LE Create Connection was refused
 * with HCI 0x0D (limited resources) and the radio then stayed idle, so the device never came back.
 */
#define AUTO_RETRY_MS 2000
static struct ble_npl_callout s_auto_retry;

/* Also fires when the fast phase ends: the attempt restarts with the slow scan */
static void auto_retry_cb(struct ble_npl_event *ev)
{
    s_auto_dirty = true;
    radio_update();
}

/* A device dropped (or the board started): listen fast for AUTO_FAST_MS */
static void auto_fast(void)
{
    s_auto_fast_until_us = esp_timer_get_time() + (int64_t)AUTO_FAST_MS * 1000;
}

/* Put bonded devices in the accept list and wait. Connects to whichever listed device advertises first (any advertising type) */
static void radio_start_auto(void)
{
    ble_addr_t list[CFG_PAIRED_MAX];
    int n = auto_candidates(list, CFG_PAIRED_MAX);
    if (n == 0) {
        return;
    }
    int64_t fast_left_us = s_auto_fast_until_us - esp_timer_get_time();
    bool fast = fast_left_us > 0;
    int rc = ble_gap_wl_set(list, n);
    if (rc == 0) {
        rc = ble_gap_connect(s_own_addr_type, NULL, BLE_HS_FOREVER, fast ? &s_conn_auto_fast : &s_conn_auto_slow,
                             gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "autoconnect start failed rc=%d", rc);
        char buf[80];
        snprintf(buf, sizeof(buf), "{\"event\":\"autoconnect_failed\",\"rc\":%d,\"retry_ms\":%d}", rc, AUTO_RETRY_MS);
        emit(buf);
        ble_npl_callout_reset(&s_auto_retry, ble_npl_time_ms_to_ticks32(AUTO_RETRY_MS));
        return;
    }
    s_radio = RADIO_AUTO;
    s_auto_dirty = false;
    if (fast) {
        ble_npl_callout_reset(&s_auto_retry, ble_npl_time_ms_to_ticks32((uint32_t)(fast_left_us / 1000) + 1));
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"event\":\"autoconnect\",\"devices\":%d,\"fast\":%s}", n, fast ? "true" : "false");
    emit(buf);
}

static void radio_update(void)
{
    if (!s_synced || s_radio == RADIO_CANCEL) {
        return;   /* Called again once the cancel completes (CONNECT event) */
    }
    if (s_user_pending && s_radio != RADIO_CONNECT && peers_free() == 0) {
        /* Auto-reconnect took the last slot while waiting */
        s_user_pending = false;
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"event\":\"connect_failed\",\"addr\":\"%s\",\"status\":\"no_slot\"}",
                 s_user_target_str);
        emit(buf);
    }
    radio_t want = radio_wanted();
    if (want == s_radio && !(want == RADIO_AUTO && s_auto_dirty)) {
        return;
    }
    if (ble_gap_disc_active()) {
        /* NimBLE does not send DISC_COMPLETE when a scan is cancelled */
        ble_gap_disc_cancel();
        emit("{\"event\":\"scan_stopped\",\"reason\":\"radio\"}");
    }
    if (ble_gap_conn_active()) {
        /* Cancelling a connection attempt ends with a CONNECT event (BLE_HS_EAPP). Decide again then */
        if (ble_gap_conn_cancel() == 0) {
            s_radio = RADIO_CANCEL;
            return;
        }
    }
    s_radio = RADIO_IDLE;
    switch (want) {
    case RADIO_SCAN:
        radio_start_scan();
        break;
    case RADIO_CONNECT:
        radio_start_connect();
        break;
    case RADIO_AUTO:
        radio_start_auto();
        break;
    default:
        break;
    }
}

static const char *radio_name(void)
{
    switch (s_radio) {
    case RADIO_SCAN:
        return "scan";
    case RADIO_CONNECT:
        return "connect";
    case RADIO_AUTO:
        return "auto";
    case RADIO_CANCEL:
        return "cancel";
    default:
        return "idle";
    }
}

/* ---------- Metadata updates ---------- */

/*
 * When a connection becomes ready and when the name becomes known. Only changes are written
 * (cfg_paired_put skips identical content).
 * Name priority: GATT Device Name (full name) > advertised name (may be shortened). The advertised name is used only when empty.
 */
static void meta_update(peer_t *p, const char *name, bool name_is_gatt, bool touch)
{
    if (!p->id_valid || !bonds_has(&p->id)) {
        return;
    }
    cfg_paired_t rec;
    if (!meta_get(&p->id, &rec)) {
        memset(&rec, 0, sizeof(rec));
        rec.type = p->id.type & 1;
        memcpy(rec.val, p->id.val, 6);
    }
    rec.role = CFG_ROLE_INPUT;   /* This module handles only input devices (board as Central) */
    if (name && name[0] && (name_is_gatt || rec.name[0] == '\0')) {
        snprintf(rec.name, sizeof(rec.name), "%s", name);
    }
    if (p->kinds) {
        rec.kinds = p->kinds;
    }
    if (touch) {
        rec.last_seq = meta_next_seq();
    }
    cfg_paired_put(&rec);
}

/* ---------- Input monitor ----------
 * What was typed (raw reports, the routed key state) goes out on USB only while the settings UI's input monitor
 * holds a lease. Any program on the computer can open the serial port, while the OS asks the user before an app
 * may read keystrokes (macOS: Input Monitoring). Otherwise only "activity" goes out: which device, no content,
 * at most every ACTIVITY_MS.
 */
static portMUX_TYPE s_monitor_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_monitor_until;      /* esp_timer time. Written by the USB task, read by the host task */

uint32_t ble_pair_monitor(bool on)
{
    int64_t until = on ? esp_timer_get_time() + (int64_t)MONITOR_LEASE_MS * 1000 : 0;
    taskENTER_CRITICAL(&s_monitor_lock);
    s_monitor_until = until;
    taskEXIT_CRITICAL(&s_monitor_lock);
    return on ? MONITOR_LEASE_MS : 0;
}

static bool monitor_on(void)
{
    taskENTER_CRITICAL(&s_monitor_lock);
    int64_t until = s_monitor_until;
    taskEXIT_CRITICAL(&s_monitor_lock);
    return esp_timer_get_time() < until;
}

static void emit_activity(peer_t *p)
{
    int64_t now = esp_timer_get_time();
    if (p->activity_us && now - p->activity_us < (int64_t)ACTIVITY_MS * 1000) {
        return;
    }
    p->activity_us = now;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"event\":\"activity\",\"addr\":\"%s\"}", p->addr_str);
    emit(buf);
}

/* ---------- GATT discovery (HOGP 1.0 Table 4.1 order) ---------- */

static void emit_report(peer_t *p, uint16_t handle, const uint8_t *data, uint16_t len)
{
    char buf[220];
    int n = snprintf(buf, sizeof(buf), "{\"event\":\"report\",\"addr\":\"%s\",\"handle\":%u,\"data\":\"",
                     p->addr_str, handle);
    for (uint16_t i = 0; i < len && n + 3 < (int)sizeof(buf); i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%02X", data[i]);
    }
    snprintf(buf + n, sizeof(buf) - n, "\"}");
    emit(buf);
}

/* ---------- GATT cache (report table and layout of bonded devices) ----------
 * While the bond is valid, the attribute cache stays valid across connections (Core Vol 3 Part G 2.5.2, HOGP 5.2.4).
 * A bonded device sends input on reconnect before discovery finishes (CCCD values persist),
 * so route with the previous table until discovery finishes. This keeps the first key that woke the device from being dropped.
 * Kept in RAM only for now (the first connection after a reboot is routed only after discovery finishes).
 */
#define GATT_CACHE_MAX 4

typedef struct {
    bool used;
    ble_addr_t id;
    uint32_t stamp;
    uint8_t rep_count;
    report_t rep[REPORT_MAX];
    hid_layout_t *layout[SVC_MAX];   /* Heap. Only services that have a layout */
} gatt_cache_t;

static gatt_cache_t s_cache[GATT_CACHE_MAX];
static uint32_t s_cache_stamp;

static gatt_cache_t *cache_find(const ble_addr_t *id)
{
    for (int i = 0; i < GATT_CACHE_MAX; i++) {
        if (s_cache[i].used && addr_eq(&s_cache[i].id, id)) {
            return &s_cache[i];
        }
    }
    return NULL;
}

static void cache_clear(gatt_cache_t *c)
{
    for (int i = 0; i < SVC_MAX; i++) {
        free(c->layout[i]);
    }
    memset(c, 0, sizeof(*c));
}

static void cache_forget(const ble_addr_t *id)
{
    gatt_cache_t *c = cache_find(id);
    if (c) {
        cache_clear(c);
    }
}

/* Save the table of a device whose discovery finished. If no slot is free, evict the oldest */
static void cache_save(const peer_t *p)
{
    if (!p->id_valid) {
        return;
    }
    gatt_cache_t *c = cache_find(&p->id);
    if (!c) {
        c = &s_cache[0];
        for (int i = 0; i < GATT_CACHE_MAX; i++) {
            if (!s_cache[i].used) {
                c = &s_cache[i];
                break;
            }
            if (s_cache[i].stamp < c->stamp) {
                c = &s_cache[i];
            }
        }
    }
    cache_clear(c);
    c->used = true;
    c->id = p->id;
    c->stamp = ++s_cache_stamp;
    c->rep_count = p->rep_count;
    memcpy(c->rep, p->rep, sizeof(c->rep));
    for (int i = 0; i < p->svc_count; i++) {
        if (p->svc[i].layout_ok) {
            c->layout[i] = malloc(sizeof(hid_layout_t));
            if (c->layout[i]) {
                memcpy(c->layout[i], &p->svc[i].layout, sizeof(hid_layout_t));
            }
        }
    }
}

/* ---------- Keyboard LEDs ----------
 * A host writes its LED state (Num Lock, Caps Lock, ...) to the board's keyboard; the board writes the active host's
 * state to each input device that has an LED output report (HOGP: the Report Host writes Output Reports).
 * Written when the active host's LEDs change, after a host switch, and when a device becomes ready.
 */
static int on_led_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);
static void on_name_read(uint16_t conn, const char *name);

static void peer_write_led(peer_t *p, uint8_t host_led)
{
    if (p->led_written && p->led_last == host_led) {
        return;
    }
    for (int i = 0; i < p->rep_count; i++) {
        const report_t *r = &p->rep[i];
        const hid_svc_t *s = r->svc < p->svc_count ? &p->svc[r->svc] : NULL;
        if (r->type != REPORT_TYPE_OUTPUT || !s || !s->layout_ok || !s->layout.led.present ||
            r->report_id != s->layout.led.report_id) {
            continue;
        }
        uint8_t buf[8];
        size_t n = hid_led_report(&s->layout, host_led, buf, sizeof(buf));
        if (n == 0) {
            return;
        }
        bool request = !(r->props & CHR_PROP_WRITE_NR);
        int rc = request ? ble_gattc_write_flat(p->conn, r->val, buf, n, on_led_written, NULL)
                         : ble_gattc_write_no_rsp_flat(p->conn, r->val, buf, n);
        if (rc == 0) {
            p->led_written = true;
            p->led_last = host_led;
            p->att_busy = request;
        } else {
            ESP_LOGW(TAG, "LED write rc=%d", rc);
        }
        return;
    }
}

/*
 * Requests to a ready device go one at a time (a client has one request outstanding: Core Vol 3 Part F 3.3.2):
 * first the Device Name read, then the active host's LEDs whenever the keyboard has a different value.
 * Write Without Response is not a request, so it does not wait.
 */
static void peer_att_next(peer_t *p)
{
    if (!p->used || p->state != PS_READY || p->att_busy) {
        return;
    }
    if (!p->name_done) {
        p->name_done = true;
        int rc = gap_name_read(p->conn, on_name_read);
        if (rc == 0) {
            p->att_busy = true;
            return;
        }
        ESP_LOGW(TAG, "device name read start failed rc=%d", rc);
    }
    peer_write_led(p, router_host_led(router_active_host()));
}

static int on_led_written(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (p) {
        p->att_busy = false;
        peer_att_next(p);
    }
    return 0;
}

void ble_pair_leds_update(void)
{
    for (int i = 0; i < PEER_MAX; i++) {
        peer_att_next(&s_peers[i]);
    }
}

/* ---------- Routing (input device -> active host) ---------- */

/*
 * The state sent to the active host is also emitted as an event, for the settings UI and debugging
 * (host.c sends the actual reports). Sent only when the key state changes (mouse motion is not sent),
 * and only while the input monitor is on.
 */
static void emit_route(const router_out_t *o)
{
    char buf[320];
    int n = snprintf(buf, sizeof(buf), "{\"event\":\"route\",\"host\":%d,\"keys\":[", router_active_host() + 1);
    bool first = true;
    for (int u = 0; u < 256 && n < (int)sizeof(buf) - 64; u++) {
        if (hid_key_down(o->keys, (uint8_t)u)) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s%d", first ? "" : ",", u);
            first = false;
        }
    }
    n += snprintf(buf + n, sizeof(buf) - n, "],\"buttons\":%u,\"consumer\":[", o->buttons);
    for (int c = 0; c < o->consumer_count && n < (int)sizeof(buf) - 16; c++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s%u", c ? "," : "", o->consumer[c]);
    }
    snprintf(buf + n, sizeof(buf) - n, "],\"system\":%u}", o->system);
    emit(buf);
}

static void route_out(const router_out_t *out, int switched_from)
{
    if (switched_from >= 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"event\":\"host_switch\",\"host\":%d,\"from\":%d}", router_active_host() + 1,
                 switched_from + 1);
        emit(buf);
    }
    bool monitor = monitor_on();
    if (out->pre_valid && monitor) {
        /* Send the held combo modifiers first (for Ctrl+C: Ctrl, then Ctrl+C) */
        router_out_t pre = *out;
        memcpy(pre.keys, out->pre_keys, sizeof(pre.keys));
        pre.buttons_changed = false;
        pre.consumer_changed = false;
        emit_route(&pre);
    }
    if (monitor && (switched_from >= 0 || out->keys_changed || out->buttons_changed || out->consumer_changed)) {
        emit_route(out);
    }
    if (out->pk_event != ROUTER_PK_NONE) {
        host_passkey_event(out);   /* Host pairing: passkey typed on the keyboard */
    }
    host_send(out, switched_from);   /* On a switch: release-all to the previous host, then the new host */
    if (switched_from >= 0) {
        ble_pair_leds_update();   /* The keyboard shows the new host's Caps Lock etc. */
        /* Kept across restarts (power, firmware update). Saved after the reports, since a flash write holds up this task */
        esp_err_t err = cfg_active_host_set(router_active_host());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "active host save failed: %s", esp_err_to_name(err));
        }
    }
}

static void route_report(peer_t *p, uint16_t handle, const uint8_t *data, uint16_t len)
{
    /* The current table if discovery has finished, otherwise the previous one (bonded device) */
    const report_t *rep = p->rep;
    uint8_t rep_count = p->rep_count;
    const hid_layout_t *layouts[SVC_MAX] = {0};
    if (p->state == PS_READY) {
        for (int i = 0; i < p->svc_count; i++) {
            layouts[i] = p->svc[i].layout_ok ? &p->svc[i].layout : NULL;
        }
    } else {
        gatt_cache_t *c = p->id_valid ? cache_find(&p->id) : NULL;
        if (!c) {
            return;
        }
        rep = c->rep;
        rep_count = c->rep_count;
        for (int i = 0; i < SVC_MAX; i++) {
            layouts[i] = c->layout[i];
        }
    }
    for (int i = 0; i < rep_count; i++) {
        if (rep[i].val != handle) {
            continue;
        }
        const hid_layout_t *layout = rep[i].svc < SVC_MAX ? layouts[rep[i].svc] : NULL;
        hid_input_t in;
        if (!layout || !hid_decode(layout, rep[i].report_id, data, len, &in)) {
            return;
        }
        router_out_t out;
        int from;
        router_input((uint8_t)(p - s_peers), (uint8_t)i, &in, &out, &from);
        route_out(&out, from);
        return;
    }
}

/* Apply the switch combos. Empty text means the default. ESP_ERR_INVALID_ARG if malformed */
#define DEFAULT_COMBOS "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"

static esp_err_t apply_combos(const char *text)
{
    combo_t list[ROUTER_HOST_MAX];
    if (!combo_parse_list(text && text[0] ? text : DEFAULT_COMBOS, list)) {
        return ESP_ERR_INVALID_ARG;
    }
    router_set_combos(list);
    return ESP_OK;
}

static const ble_uuid16_t s_uuid_hid = BLE_UUID16_INIT(UUID_HID_SERVICE);

static void disc_fail(peer_t *p, const char *stage, int status)
{
    p->stage = ST_DONE;
    fail_peer(p, stage, status);
}

/* Name: Device Name (0x2A00) in the GAP service is mandatory. Devices with no name in their advertising get one here */
static void on_name_read(uint16_t conn, const char *name)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return;
    }
    p->att_busy = false;
    if (name[0]) {
        meta_update(p, name, true, false);
        char esc[48];
        char buf[160];
        json_escape(name, esc, sizeof(esc));
        snprintf(buf, sizeof(buf), "{\"event\":\"device_name\",\"addr\":\"%s\",\"name\":\"%s\"}", p->addr_str, esc);
        emit(buf);
    }
    peer_att_next(p);
}

static void peer_ready(peer_t *p)
{
    p->state = PS_READY;
    p->stage = ST_DONE;
    cache_save(p);
    auto_ctl_t *a = auto_ctl(&p->id, false);
    if (a) {
        a->fails = 0;
    }
    meta_update(p, NULL, false, true);
    char kinds[64];
    char buf[200];
    hid_kinds_json(p->kinds, kinds, sizeof(kinds));
    snprintf(buf, sizeof(buf),
             "{\"event\":\"hid_ready\",\"addr\":\"%s\",\"id_addr\":\"%s\",\"kinds\":%s,\"reports\":%u,"
             "\"services\":%u,\"mtu\":%u}",
             p->addr_str, p->id_str, kinds, p->rep_count, p->svc_count, p->mtu);
    emit(buf);
    peer_att_next(p);
}

static int on_cccd_write(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status != 0) {
        /* A subscription failure on one report does not drop the whole device. Report it and move on */
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"event\":\"subscribe_failed\",\"addr\":\"%s\",\"val\":%u,\"status\":%d}",
                 p->addr_str, p->rep[p->idx].val, error->status);
        emit(buf);
    }
    p->idx++;
    disc_next(p);
    return 0;
}

static int on_ref_read(uint16_t conn, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status == 0 && attr != NULL && attr->om != NULL) {
        /* Report Reference: [Report ID, type (1 input, 2 output, 3 Feature)] */
        uint8_t ref[2] = {0, 0};
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        os_mbuf_copydata(attr->om, 0, n < 2 ? n : 2, ref);
        p->rep[p->idx].report_id = ref[0];
        p->rep[p->idx].type = ref[1];
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"event\":\"report_ref\",\"addr\":\"%s\",\"val\":%u,\"id\":%u,\"type\":%u,\"svc\":%u}",
                 p->addr_str, p->rep[p->idx].val, ref[0], ref[1], p->rep[p->idx].svc);
        emit(buf);
    } else {
        ESP_LOGE(TAG, "report reference read failed status=%d", error->status);
    }
    p->idx++;
    disc_next(p);
    return 0;
}

static int on_info_read(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status == 0 && attr != NULL && attr->om != NULL) {
        /* HID Information: bcdHID(2), bCountryCode(1), Flags(1: bit0 RemoteWake, bit1 NormallyConnectable) */
        uint8_t info[4] = {0};
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        os_mbuf_copydata(attr->om, 0, n < 4 ? n : 4, info);
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"event\":\"hid_info\",\"addr\":\"%s\",\"svc\":%u,\"bcd_hid\":%u,\"country\":%u,"
                 "\"remote_wake\":%s,\"normally_connectable\":%s}",
                 p->addr_str, p->idx, (unsigned)(info[0] | (info[1] << 8)), info[2],
                 (info[3] & 0x01) ? "true" : "false", (info[3] & 0x02) ? "true" : "false");
        emit(buf);
    } else {
        ESP_LOGW(TAG, "HID information read failed status=%d", error->status);
    }
    p->idx++;
    disc_next(p);
    return 0;
}

static void emit_report_map(peer_t *p, uint8_t svc)
{
    hid_svc_t *s = &p->svc[svc];
    uint8_t kinds = hid_map_kinds(s->map, s->map_len);
    p->kinds |= kinds;
    hid_layout_parse(s->map, s->map_len, &s->layout);
    s->layout_ok = true;
    if (s->layout.truncated) {
        ESP_LOGW(TAG, "report map layout truncated (fields %u)", s->layout.field_count);
    }
    char kj[64];
    hid_kinds_json(kinds, kj, sizeof(kj));
    int n = snprintf(s_evt, sizeof(s_evt),
                     "{\"event\":\"report_map\",\"addr\":\"%s\",\"svc\":%u,\"len\":%u,\"truncated\":%s,"
                     "\"kinds\":%s,\"hex\":\"",
                     p->addr_str, svc, s->map_len, s->map_truncated ? "true" : "false", kj);
    for (uint16_t i = 0; i < s->map_len && n + 3 < (int)sizeof(s_evt); i++) {
        n += snprintf(s_evt + n, sizeof(s_evt) - n, "%02X", s->map[i]);
    }
    snprintf(s_evt + n, sizeof(s_evt) - n, "\"}");
    emit(s_evt);
}

static int on_map_read(uint16_t conn, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    hid_svc_t *s = &p->svc[p->idx];
    if (error->status == 0 && attr != NULL && attr->om != NULL) {
        /* Read Long is called once per fragment. When done, EDONE arrives one more time */
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (s->map_len + n > MAP_MAX) {
            n = MAP_MAX - s->map_len;
            s->map_truncated = true;   /* Device exceeds the spec maximum (512). Flag that parsing may be off */
        }
        os_mbuf_copydata(attr->om, 0, n, &s->map[s->map_len]);
        s->map_len += n;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        emit_report_map(p, p->idx);
    } else {
        ESP_LOGE(TAG, "report map read failed status=%d", error->status);
    }
    p->idx++;
    disc_next(p);
    return 0;
}

/*
 * A descriptor belongs to the input report value right before it.
 * If another characteristic definition sits in between, it does not belong to that report.
 */
static void assign_desc(peer_t *p, uint16_t handle, bool is_cccd)
{
    int best = -1;
    for (int i = 0; i < p->rep_count; i++) {
        if (p->rep[i].val < handle && (best < 0 || p->rep[i].val > p->rep[best].val)) {
            best = i;
        }
    }
    if (best < 0) {
        return;
    }
    uint16_t *slot = is_cccd ? &p->rep[best].cccd : &p->rep[best].ref;
    if (*slot != 0) {
        return;
    }
    for (int j = 0; j < p->chr_count; j++) {
        if (p->chr_def[j] > p->rep[best].val && p->chr_def[j] < handle) {
            return;
        }
    }
    *slot = handle;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status == 0) {
        uint16_t uuid = ble_uuid_u16(&dsc->uuid.u);
        if (uuid == UUID_CCCD) {
            assign_desc(p, dsc->handle, true);
        } else if (uuid == UUID_REPORT_REF) {
            assign_desc(p, dsc->handle, false);
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "descriptor discovery error status=%d", error->status);
    }
    p->idx++;
    disc_next(p);
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status == 0) {
        uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
        hid_svc_t *s = &p->svc[p->idx];
        if (p->chr_count < CHR_MAX) {
            p->chr_def[p->chr_count++] = chr->def_handle;
        }
        /* Input reports (notify) are subscribed; output reports (write) are kept for the keyboard LEDs */
        if (uuid == UUID_REPORT && (chr->properties & (CHR_PROP_NOTIFY | CHR_PROP_WRITE | CHR_PROP_WRITE_NR)) &&
            p->rep_count < REPORT_MAX) {
            report_t *r = &p->rep[p->rep_count++];
            memset(r, 0, sizeof(*r));
            r->val = chr->val_handle;
            r->svc = p->idx;
            r->props = chr->properties;
        } else if (uuid == UUID_REPORT_MAP) {
            s->map_handle = chr->val_handle;
        } else if (uuid == UUID_HID_INFO) {
            s->info_handle = chr->val_handle;
        }
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"event\":\"hid_chr\",\"addr\":\"%s\",\"svc\":%u,\"val\":%u,\"props\":%u,\"uuid16\":%u}",
                 p->addr_str, p->idx, chr->val_handle, chr->properties, uuid);
        emit(buf);
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        disc_fail(p, "characteristics", error->status);
        return 0;
    }
    p->idx++;
    disc_next(p);
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *service, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status == 0) {
        if (p->svc_count < SVC_MAX) {
            hid_svc_t *s = &p->svc[p->svc_count++];
            memset(s, 0, sizeof(*s));
            s->start = service->start_handle;
            s->end = service->end_handle;
        } else {
            ESP_LOGW(TAG, "more than %d HID services, extra ignored", SVC_MAX);
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        disc_fail(p, "services", error->status);
        return 0;
    }
    if (p->svc_count == 0) {
        disc_fail(p, "no_hid_service", 0);
        return 0;
    }
    p->stage = ST_CHR;
    p->idx = 0;
    disc_next(p);
    return 0;
}

/*
 * Advance discovery one stage at a time. Each stage's callback calls this function again when it finishes.
 * Services → (per service) characteristics → (per service) descriptors → (per report) Report Reference
 * → (per service) HID Information → (per service) Report Map → (per report) enable notifications → ready
 */
static void disc_next(peer_t *p)
{
    int rc = 0;
    for (;;) {
        switch (p->stage) {
        case ST_CHR:
            if (p->idx < p->svc_count) {
                rc = ble_gattc_disc_all_chrs(p->conn, p->svc[p->idx].start, p->svc[p->idx].end, on_chr, NULL);
                if (rc != 0) {
                    disc_fail(p, "characteristics", rc);
                }
                return;
            }
            p->stage = ST_DSC;
            p->idx = 0;
            break;
        case ST_DSC:
            if (p->idx < p->svc_count) {
                /* NimBLE treats the start handle as already handled, so request from service start + 1 */
                hid_svc_t *s = &p->svc[p->idx];
                if (s->start + 1 > s->end) {
                    p->idx++;
                    break;
                }
                rc = ble_gattc_disc_all_dscs(p->conn, s->start + 1, s->end, on_dsc, NULL);
                if (rc != 0) {
                    disc_fail(p, "descriptors", rc);
                }
                return;
            }
            p->stage = ST_REF;
            p->idx = 0;
            break;
        case ST_REF:
            while (p->idx < p->rep_count && p->rep[p->idx].ref == 0) {
                p->idx++;
            }
            if (p->idx < p->rep_count) {
                rc = ble_gattc_read(p->conn, p->rep[p->idx].ref, on_ref_read, NULL);
                if (rc == 0) {
                    return;
                }
                p->idx++;
                break;
            }
            p->stage = ST_INFO;
            p->idx = 0;
            break;
        case ST_INFO:
            while (p->idx < p->svc_count && p->svc[p->idx].info_handle == 0) {
                p->idx++;
            }
            if (p->idx < p->svc_count) {
                rc = ble_gattc_read(p->conn, p->svc[p->idx].info_handle, on_info_read, NULL);
                if (rc == 0) {
                    return;
                }
                p->idx++;
                break;
            }
            p->stage = ST_MAP;
            p->idx = 0;
            break;
        case ST_MAP:
            while (p->idx < p->svc_count && p->svc[p->idx].map_handle == 0) {
                p->idx++;
            }
            if (p->idx < p->svc_count) {
                p->svc[p->idx].map_len = 0;
                rc = ble_gattc_read_long(p->conn, p->svc[p->idx].map_handle, 0, on_map_read, NULL);
                if (rc == 0) {
                    return;
                }
                p->idx++;
                break;
            }
            p->stage = ST_SUB;
            p->idx = 0;
            break;
        case ST_SUB:
            while (p->idx < p->rep_count && p->rep[p->idx].cccd == 0) {
                p->idx++;
            }
            if (p->idx < p->rep_count) {
                static const uint8_t enable_notify[2] = {0x01, 0x00};
                rc = ble_gattc_write_flat(p->conn, p->rep[p->idx].cccd, enable_notify, sizeof(enable_notify),
                                          on_cccd_write, NULL);
                if (rc == 0) {
                    return;
                }
                p->idx++;
                break;
            }
            peer_ready(p);
            return;
        default:
            return;
        }
    }
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p || p->stage != ST_MTU) {
        return 0;
    }
    /* If the device refuses, the ATT default (23) stays and discovery goes on as usual */
    p->mtu = ble_att_mtu(conn);
    p->stage = ST_SVC;
    int rc = ble_gattc_disc_svc_by_uuid(p->conn, &s_uuid_hid.u, on_svc, NULL);
    if (rc != 0) {
        disc_fail(p, "services", rc);
    }
    return 0;
}

/*
 * Discovery starts with the ATT MTU exchange (once per connection, by the client: Core Vol 3 Part F 3.4.2), as other
 * HID hosts do. With the default 23 bytes, notifications carry at most 20 bytes and the Report Map is read 22 bytes at a time
 */
static void disc_start(peer_t *p)
{
    p->state = PS_DISCOVERING;
    p->stage = ST_MTU;
    p->idx = 0;
    p->svc_count = 0;
    p->chr_count = 0;
    p->rep_count = 0;
    p->kinds = 0;
    int rc = ble_gattc_exchange_mtu(p->conn, on_mtu, NULL);
    if (rc != 0) {
        disc_fail(p, "mtu", rc);
    }
}

/* ---------- GAP events ---------- */

/*
 * Pairing input requests.
 * NUMCMP: send the number to the GUI and wait for pair_accept.
 * INPUT:  ask the GUI for passkey entry and wait for passkey (the peer displays the digits).
 * DISP:   the board generates a passkey and shows it in the GUI; the user types it on the peer device (keyboard).
 */
static void on_passkey_action(struct ble_gap_event *event)
{
    peer_t *p = peer_ensure(event->passkey.conn_handle);
    if (!p) {
        return;
    }
    uint8_t action = event->passkey.params.action;
    char buf[160];
    if (action == BLE_SM_IOACT_NUMCMP) {
        p->io_action = action;
        snprintf(buf, sizeof(buf), "{\"event\":\"numcmp\",\"addr\":\"%s\",\"code\":\"%06lu\"}",
                 p->addr_str, (unsigned long)event->passkey.params.numcmp);
        emit(buf);
    } else if (action == BLE_SM_IOACT_INPUT) {
        p->io_action = action;
        snprintf(buf, sizeof(buf), "{\"event\":\"passkey_request\",\"addr\":\"%s\"}", p->addr_str);
        emit(buf);
    } else if (action == BLE_SM_IOACT_DISP) {
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
        int rc = ble_sm_inject_io(p->conn, &io);
        snprintf(buf, sizeof(buf), "{\"event\":\"passkey_display\",\"addr\":\"%s\",\"code\":\"%06lu\",\"ok\":%s}",
                 p->addr_str, (unsigned long)io.passkey, rc == 0 ? "true" : "false");
        emit(buf);
    } else {
        ESP_LOGW(TAG, "unsupported passkey action %u", action);
    }
}

static void on_connect(struct ble_gap_event *event)
{
    radio_t was = s_radio;
    s_radio = RADIO_IDLE;
    if (event->connect.status != 0) {
        /*
         * EPREEMPTED: the stack aborts an in-progress connection attempt when it adds a new device's IRK to
         * the resolving list (ble_hs_pvcy_add_entry calls ble_gap_preempt).
         * It is not an error, so a user connection is simply retried.
         */
        if (was == RADIO_CONNECT && event->connect.status != BLE_HS_EPREEMPTED) {
            s_user_pending = false;
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"event\":\"connect_failed\",\"addr\":\"%s\",\"status\":%d}",
                     s_user_target_str, event->connect.status);
            emit(buf);
        }
        radio_update();
        return;
    }

    peer_t *p = peer_ensure(event->connect.conn_handle);
    struct ble_gap_conn_desc desc;
    if (!p || ble_gap_conn_find(event->connect.conn_handle, &desc) != 0) {
        /* Connections are started only when a slot is free, so this does not happen in the normal flow */
        ble_gap_terminate(event->connect.conn_handle, HCI_USER_TERM);
        radio_update();
        return;
    }
    p->user = (was == RADIO_CONNECT);
    if (p->user) {
        s_user_pending = false;
    }

    char buf[160];
    /* Record the actual connection parameters (interval in 1.25 ms units, supervision in 10 ms units) */
    snprintf(buf, sizeof(buf),
             "{\"event\":\"connected\",\"addr\":\"%s\",\"id_addr\":\"%s\",\"bonded\":%s,\"user\":%s,"
             "\"itvl\":%u,\"latency\":%u,\"timeout\":%u}",
             p->addr_str, p->id_str, p->id_valid ? "true" : "false", p->user ? "true" : "false",
             desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
    emit(buf);

    /*
     * HOGP 5.2.3: start encryption right after connecting. With a bond, restore with the stored keys; otherwise pair anew.
     * Skip it if the device already started security before CONNECT was delivered (see peer_ensure).
     */
    if (p->state == PS_CONNECTED && s_sec_owner != p) {
        p->sec_wait = true;
        sec_pump();
    }
    s_auto_dirty = true;   /* This device drops out of the accept list */
    radio_update();
}

static void on_disconnect(struct ble_gap_event *event)
{
    peer_t *p = peer_by_conn(event->disconnect.conn.conn_handle);
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"event\":\"disconnected\",\"addr\":\"%s\",\"id_addr\":\"%s\",\"reason\":%d}",
             p ? p->addr_str : "", p ? p->id_str : "", event->disconnect.reason);
    emit(buf);
    if (p) {
        if (s_sec_owner == p) {
            s_sec_owner = NULL;
        }
        /* A bonded device dropped before ready counts as a failure. Repeated failures pause auto-reconnect to stop endless retries */
        if (p->state != PS_READY && p->id_valid && bonds_has(&p->id)) {
            auto_ctl_t *a = auto_ctl(&p->id, true);
            if (a && !a->paused && ++a->fails >= FAIL_LIMIT) {
                a->paused = true;
                snprintf(buf, sizeof(buf), "{\"event\":\"autoconnect_paused\",\"id_addr\":\"%s\",\"fails\":%u}",
                         p->id_str, a->fails);
                emit(buf);
            }
        }
        /* Treat the keys and buttons that device was pressing as released (so no key stays stuck on the host) */
        router_out_t out;
        router_device_gone((uint8_t)(p - s_peers), &out);
        route_out(&out, -1);
        p->used = false;
        auto_fast();
    }
    s_auto_dirty = true;
    sec_pump();
    radio_update();
}

static void on_enc_change(struct ble_gap_event *event)
{
    peer_t *p = peer_ensure(event->enc_change.conn_handle);
    if (!p) {
        return;
    }
    if (s_sec_owner == p) {
        s_sec_owner = NULL;
    }
    p->sec_wait = false;   /* Clear the wait flag even if the peer started security first */
    p->io_action = BLE_SM_IOACT_NONE;

    struct ble_gap_conn_desc desc;
    bool have_desc = ble_gap_conn_find(p->conn, &desc) == 0;
    bool ok = event->enc_change.status == 0 && have_desc;
    /* On a new pairing, this peer's keys were just written to the store (store_write_hook) */
    bool new_bond = ok && s_new_bond_pending && addr_eq(&s_new_bond_addr, &desc.peer_id_addr);
    if (new_bond) {
        s_new_bond_pending = false;
    }
    const char *bond = !ok ? "none" : new_bond ? "new" : desc.sec_state.bonded ? "restored" : "none";
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"encrypted\",\"addr\":\"%s\",\"ok\":%s,\"status\":%d,\"bond\":\"%s\","
             "\"authenticated\":%s,\"key_size\":%u}",
             p->addr_str, ok ? "true" : "false", event->enc_change.status, bond,
             have_desc && desc.sec_state.authenticated ? "true" : "false",
             have_desc ? (unsigned)desc.sec_state.key_size : 0u);
    emit(buf);

    if (!ok) {
        fail_peer(p, "security", event->enc_change.status);
    } else if (!desc.sec_state.bonded) {
        /* HOGP 6.1: the host must bond with the device. A connection that is encrypted but not bonded is not used */
        fail_peer(p, "not_bonded", 0);
    } else {
        /* Key distribution is done, so peer_id_addr is the identity (for RPA devices too, the address resolved with the IRK) */
        p->id = id_norm(&desc.peer_id_addr);
        p->id_valid = true;
        val_to_str(p->id.val, p->id_str);
        if (new_bond) {
            bonds_reconcile();   /* Create metadata for the new bond */
            s_auto_dirty = true;
        }
        found_dev_t *fd = found_get(&p->ota);
        meta_update(p, fd ? fd->name : NULL, false, false);

        cfg_paired_t meta;
        char name[48] = "";
        if (meta_get(&p->id, &meta)) {
            json_escape(meta.name, name, sizeof(name));
        }
        snprintf(buf, sizeof(buf),
                 "{\"event\":\"paired\",\"addr\":\"%s\",\"id_addr\":\"%s\",\"name\":\"%s\",\"bond\":\"%s\"}",
                 p->addr_str, p->id_str, name, bond);
        emit(buf);
        /* Do not start an overlapping discovery if an encryption event comes again (e.g. on key refresh) */
        if (p->state == PS_SECURING || p->state == PS_CONNECTED) {
            disc_start(p);
        }
    }
    sec_pump();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        note_adv(&event->disc);
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        /* Arrives on timeout (0) or when the stack aborted it (EPREEMPTED, when an IRK is added to the resolving list). Not sent on cancel */
        if (s_radio == RADIO_SCAN) {
            s_radio = RADIO_IDLE;
        }
        if (event->disc_complete.reason == BLE_HS_EPREEMPTED) {
            emit("{\"event\":\"scan_stopped\",\"reason\":\"preempted\"}");   /* s_want_scan is unchanged, so it restarts */
        } else {
            s_want_scan = false;
            emit("{\"event\":\"scan_stopped\",\"reason\":\"timeout\"}");
        }
        radio_update();
        break;

    case BLE_GAP_EVENT_CONNECT:
        on_connect(event);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnect(event);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        on_enc_change(event);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        peer_t *p = peer_by_conn(event->notify_rx.conn_handle);
        if (!p) {
            break;
        }
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len > 64) {
            len = 64;
        }
        uint8_t data[64];
        os_mbuf_copydata(event->notify_rx.om, 0, len, data);
        emit_activity(p);
        if (monitor_on()) {
            emit_report(p, event->notify_rx.attr_handle, data, len);
        }
        route_report(p, event->notify_rx.attr_handle, data, len);
        break;
    }

    case BLE_GAP_EVENT_CONN_UPDATE: {
        /* The device changed the connection parameters. Recorded for latency measurement */
        peer_t *p = peer_by_conn(event->conn_update.conn_handle);
        struct ble_gap_conn_desc d;
        if (p && ble_gap_conn_find(p->conn, &d) == 0) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"event\":\"conn_params\",\"addr\":\"%s\",\"status\":%d,\"itvl\":%u,\"latency\":%u,"
                     "\"timeout\":%u}",
                     p->addr_str, event->conn_update.status, d.conn_itvl, d.conn_latency, d.supervision_timeout);
            emit(buf);
        }
        break;
    }

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
    case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
        /*
         * The device asks for other connection parameters. Recorded, then accepted as asked (the controller may
         * still refuse). Real hardware: the mouse asks for 7.5 ms, latency 48, 3 s right after connecting and,
         * when refused, 11.25 ms; with two hosts connected the controller refused nearly every update
         * (HCI 0x12), and after repeated refusals the mouse sometimes disconnected.
         */
        peer_t *p = peer_by_conn(event->conn_update_req.conn_handle);
        const struct ble_gap_upd_params *q = event->conn_update_req.peer_params;
        if (p && q) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"event\":\"conn_params_req\",\"addr\":\"%s\",\"itvl_min\":%u,\"itvl_max\":%u,"
                     "\"latency\":%u,\"timeout\":%u}",
                     p->addr_str, q->itvl_min, q->itvl_max, q->latency, q->supervision_timeout);
            emit(buf);
        }
        return 0;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        on_passkey_action(event);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* NimBLE delivers this only for Peripheral (host) connections, which host.c handles. Ignored here */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    default:
        break;
    }
    return 0;
}

/*
 * When the bond store is full. The stock example's ble_store_util_status_rr deletes the oldest bond
 * without the user knowing, so it is not used. Instead, reject the new pairing and notify the GUI.
 * FULL arrives before pairing starts (returning nonzero rejects it). OVERFLOW is a failure at the write stage.
 */
static int on_store_status(struct ble_store_status_event *event, void *arg)
{
    if (event->event_code != BLE_STORE_EVENT_FULL && event->event_code != BLE_STORE_EVENT_OVERFLOW) {
        return 0;
    }
    int obj = event->event_code == BLE_STORE_EVENT_FULL ? event->full.obj_type : event->overflow.obj_type;
    char buf[128];
    if (obj == BLE_STORE_OBJ_TYPE_PEER_SEC || obj == BLE_STORE_OBJ_TYPE_OUR_SEC) {
        snprintf(buf, sizeof(buf), "{\"event\":\"bond_full\",\"stage\":\"%s\",\"bonds\":%d,\"max\":%d}",
                 event->event_code == BLE_STORE_EVENT_FULL ? "before_pairing" : "write",
                 bond_count_local(), CONFIG_BT_NIMBLE_MAX_BONDS);
    } else {
        /* Out of slots for other entries such as subscriptions (CCCD). A config (MAX_CCCDS) issue, so report it as a warning */
        snprintf(buf, sizeof(buf), "{\"event\":\"store_full\",\"obj\":%d}", obj);
    }
    emit(buf);
    return BLE_HS_ESTORE_CAP;
}

static void on_sync(void)
{
    /* If there is no public address, generate a random one */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "own address setup failed rc=%d", rc);
        return;
    }
    install_store_hook();
    bonds_reconcile();
    s_synced = true;
    s_radio = RADIO_IDLE;
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"event\":\"ble_ready\",\"bonds\":%d,\"max\":%d}",
             bond_count_local(), CONFIG_BT_NIMBLE_MAX_BONDS);
    emit(buf);
    ESP_LOGI(TAG, "BLE ready, bonds=%d", bond_count_local());
    auto_fast();
    radio_update();   /* Start auto-reconnect if there are bonded devices */
    host_on_sync();   /* Advertising for disconnected bonded hosts */
}

static void on_reset(int reason)
{
    /* When the host resets, all connections and in-progress procedures are gone. Restart on sync */
    s_synced = false;
    s_radio = RADIO_IDLE;
    s_sec_owner = NULL;
    s_user_pending = false;
    memset(s_peers, 0, sizeof(s_peers));
    host_on_reset();
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
    emit("{\"event\":\"ble_reset\"}");
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- Run requests from other tasks on the host task ---------- */

typedef enum {
    OP_SCAN, OP_CONNECT, OP_DISCONNECT, OP_ACCEPT, OP_PASSKEY, OP_CANCEL,
    OP_REMOVE, OP_CLEAR, OP_LIST, OP_PEERS, OP_PAIRED, OP_STATUS, OP_RADIO, OP_BONDS,
    OP_COMBOS, OP_HOST_SELECT, OP_HOST_GET,
    OP_HOST_PAIR, OP_HOST_LIST, OP_HOST_REMOVE, OP_HOST_PASSKEY, OP_STATS, OP_SELFTEST,
} op_t;

typedef struct {
    op_t op;
    const char *addr;
    bool flag;
    uint32_t num;
    char *buf;
    size_t len;
    size_t out_len;
    bool *scanning;
    bool *connected;
    bool *hid_ready;
    const char *radio;
    int count;
    esp_err_t result;
} req_t;

static SemaphoreHandle_t s_req_lock;
static SemaphoreHandle_t s_req_done;
static struct ble_npl_event s_req_ev;
static req_t *s_req;

static esp_err_t op_connect(const char *addr)
{
    uint8_t val[6];
    if (!s_synced) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!str_to_val(addr, val)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (peer_by_val(val)) {
        return ESP_ERR_INVALID_STATE;   /* Already connected */
    }
    if (s_user_pending) {
        return ESP_ERR_INVALID_STATE;   /* Another connection attempt is in progress */
    }
    if (peers_free() == 0) {
        return ESP_ERR_NO_MEM;
    }
    ble_addr_t bond;
    if (bonds_find_val(val, &bond)) {
        /*
         * Do not start a direct connection (10-second attempt) to a bonded device: it would pause auto-reconnect
         * for other devices meanwhile. Put it back on the auto-reconnect list (clearing a user disconnect or a
         * failure pause), and stop any scan so it waits right away.
         * It connects when the device advertises (wakes up).
         */
        auto_resume(&bond);
        s_want_scan = false;
        s_auto_dirty = true;
        radio_update();
        return ESP_OK;
    }
    found_dev_t *fd = found_by_val(val);
    if (!fd) {
        return ESP_ERR_NOT_FOUND;
    }
    if (host_window_open()) {
        return ESP_ERR_NOT_ALLOWED;   /* Do not pair a new input device while the host pairing window is open */
    }
    s_user_target = fd->addr;
    snprintf(s_user_target_str, sizeof(s_user_target_str), "%s", addr);
    auto_resume(&s_user_target);
    s_user_pending = true;
    radio_update();
    return ESP_OK;
}

static esp_err_t op_disconnect(const char *addr)
{
    uint8_t val[6];
    if (!str_to_val(addr, val)) {
        return ESP_ERR_INVALID_ARG;
    }
    peer_t *p = peer_by_val(val);
    if (!p) {
        return ESP_ERR_NOT_FOUND;
    }
    if (p->id_valid) {
        auto_ctl_t *a = auto_ctl(&p->id, true);
        if (a) {
            a->paused = true;   /* A device the user disconnected is not auto-connected until the user connects it again */
        }
    }
    ble_gap_terminate(p->conn, HCI_USER_TERM);
    return ESP_OK;
}

static esp_err_t op_io(op_t op, bool accept, uint32_t code)
{
    uint8_t want = op == OP_ACCEPT ? BLE_SM_IOACT_NUMCMP : BLE_SM_IOACT_INPUT;
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (p->used && p->io_action == want) {
            struct ble_sm_io io = {.action = want};
            if (want == BLE_SM_IOACT_NUMCMP) {
                io.numcmp_accept = accept ? 1 : 0;
            } else {
                io.passkey = code;
            }
            p->io_action = BLE_SM_IOACT_NONE;
            return ble_sm_inject_io(p->conn, &io) == 0 ? ESP_OK : ESP_FAIL;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

static void op_cancel(void)
{
    s_want_scan = false;
    s_user_pending = false;
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (!p->used || p->io_action == BLE_SM_IOACT_NONE) {
            continue;
        }
        if (p->io_action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 0};
            ble_sm_inject_io(p->conn, &io);
        } else {
            ble_gap_terminate(p->conn, HCI_USER_TERM);   /* Passkey entry has no way to reject, so disconnect */
        }
        p->io_action = BLE_SM_IOACT_NONE;
    }
    radio_update();
}

static esp_err_t op_remove(const char *addr)
{
    uint8_t val[6];
    ble_addr_t id;
    if (!str_to_val(addr, val)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bonds_find_val(val, &id)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_user_pending && memcmp(s_user_target.val, id.val, 6) == 0) {
        s_user_pending = false;   /* Abandon any connection attempt to the device being removed */
    }
    /* ble_gap_unpair returns EBUSY while scanning or advertising. Pause the radio briefly */
    s_radio_hold = true;
    radio_update();
    host_adv_hold(true);            /* ble_gap_unpair also returns EBUSY while advertising */
    int rc = ble_gap_unpair(&id);   /* Disconnects if connected, and removes it from the resolving list and the store */
    host_adv_hold(false);
    s_radio_hold = false;
    if (rc != 0) {
        ESP_LOGE(TAG, "unpair failed rc=%d", rc);
        s_auto_dirty = true;
        radio_update();
        return ESP_FAIL;
    }
    cfg_paired_remove(id.type & 1, id.val);
    cache_forget(&id);
    auto_resume(&id);
    char id_str[18];
    char buf[96];
    val_to_str(id.val, id_str);
    snprintf(buf, sizeof(buf), "{\"event\":\"unpaired\",\"id_addr\":\"%s\"}", id_str);
    emit(buf);
    s_auto_dirty = true;
    radio_update();
    return ESP_OK;
}

static esp_err_t op_clear(void)
{
    s_radio_hold = true;
    radio_update();
    host_adv_hold(true);
    ble_addr_t bonds[CFG_PAIRED_MAX];
    int n = bonds_get(bonds, CFG_PAIRED_MAX);
    esp_err_t result = ESP_OK;
    for (int i = 0; i < n; i++) {
        if (ble_gap_unpair(&bonds[i]) != 0) {
            result = ESP_FAIL;
        }
    }
    if (result == ESP_OK) {
        ble_store_clear();   /* Also clears the remaining entries such as CCCDs */
        cfg_paired_clear();
        memset(s_auto, 0, sizeof(s_auto));
        for (int i = 0; i < GATT_CACHE_MAX; i++) {
            cache_clear(&s_cache[i]);
        }
        emit("{\"event\":\"unpaired_all\"}");
    } else {
        bonds_reconcile();
    }
    host_adv_hold(false);
    s_radio_hold = false;
    s_auto_dirty = true;
    radio_update();
    return result;
}

static size_t list_json(char *buf, size_t len, bool all)
{
    size_t n = snprintf(buf, len, "[");
    bool first = true;
    for (int i = 0; i < FOUND_MAX; i++) {
        found_dev_t *d = &s_found[i];
        /* A currently connected device is not a "found device" */
        if (!d->used || (!all && !d->hid) || peer_by_addr(&d->addr)) {
            continue;
        }
        char name[48];
        char item[192];
        json_escape(d->name, name, sizeof(name));
        int m = snprintf(item, sizeof(item),
                         "%s{\"addr\":\"%s\",\"name\":\"%s\",\"rssi\":%d,\"paired\":%s,\"appearance\":%u,\"hid\":%s}",
                         first ? "" : ",", d->addr_str, name, d->rssi, bonds_has(&d->addr) ? "true" : "false",
                         d->appearance, d->hid ? "true" : "false");
        if (m < 0 || n + (size_t)m + 2 > len) {
            break;   /* Stop if an entry does not fit whole. The JSON must not be cut midway */
        }
        memcpy(buf + n, item, m + 1);
        n += m;
        first = false;
    }
    n += snprintf(buf + n, len - n, "]");
    return n < len ? n : len - 1;
}

static size_t peers_json(char *buf, size_t len)
{
    size_t n = snprintf(buf, len, "[");
    bool first = true;
    for (int i = 0; i < PEER_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (!p->used) {
            continue;
        }
        struct ble_gap_conn_desc d;
        unsigned itvl = ble_gap_conn_find(p->conn, &d) == 0 ? d.conn_itvl : 0;
        char item[180];
        int m = snprintf(item, sizeof(item),
                         "%s{\"addr\":\"%s\",\"id_addr\":\"%s\",\"state\":\"%s\",\"hid_ready\":%s,\"itvl\":%u}",
                         first ? "" : ",", p->addr_str, p->id_valid ? p->id_str : "", state_name(p->state),
                         p->state == PS_READY ? "true" : "false", itvl);
        if (m < 0 || n + (size_t)m + 2 > len) {
            break;
        }
        memcpy(buf + n, item, m + 1);
        n += m;
        first = false;
    }
    n += snprintf(buf + n, len - n, "]");
    return n < len ? n : len - 1;
}

/* The bond list is the source of truth; name and kind are added from the metadata */
static size_t paired_json(char *buf, size_t len)
{
    ble_addr_t bonds[CFG_PAIRED_MAX];
    int nb = bonds_get(bonds, CFG_PAIRED_MAX);
    size_t n = snprintf(buf, len, "[");
    for (int i = 0; i < nb; i++) {
        cfg_paired_t meta;
        bool has_meta = meta_get(&bonds[i], &meta);
        if (has_meta && meta.role == CFG_ROLE_HOST) {
            continue;   /* Hosts are not included in the input device list */
        }
        peer_t *p = peer_by_addr(&bonds[i]);
        auto_ctl_t *a = auto_ctl(&bonds[i], false);
        char id[18];
        char name[48] = "";
        char kinds[64];
        char item[256];
        val_to_str(bonds[i].val, id);
        if (has_meta) {
            json_escape(meta.name, name, sizeof(name));
        }
        hid_kinds_json(has_meta ? meta.kinds : 0, kinds, sizeof(kinds));
        int m = snprintf(item, sizeof(item),
                         "%s{\"addr\":\"%s\",\"type\":%u,\"name\":\"%s\",\"kinds\":%s,\"connected\":%s,"
                         "\"state\":\"%s\",\"auto\":%s,\"last_seq\":%lu}",
                         n > 1 ? "," : "", id, bonds[i].type & 1, name, kinds, p ? "true" : "false",
                         p ? state_name(p->state) : "offline", (a && a->paused) ? "false" : "true",
                         (unsigned long)(has_meta ? meta.last_seq : 0));
        if (m < 0 || n + (size_t)m + 2 > len) {
            break;
        }
        memcpy(buf + n, item, m + 1);
        n += m;
    }
    n += snprintf(buf + n, len - n, "]");
    return n < len ? n : len - 1;
}

static esp_err_t op_dispatch(req_t *r)
{
    switch (r->op) {
    case OP_SCAN:
        if (!s_synced) {
            return ESP_ERR_INVALID_STATE;
        }
        if (r->flag && host_window_open()) {
            return ESP_ERR_NOT_ALLOWED;   /* Do not mix the host pairing window with input device pairing (the IO capabilities differ) */
        }
        if (r->flag && s_user_pending) {
            /* The most recent user action wins: abandon the pending connection attempt and scan */
            s_user_pending = false;
            char buf[96];
            snprintf(buf, sizeof(buf), "{\"event\":\"connect_failed\",\"addr\":\"%s\",\"status\":\"cancelled\"}",
                     s_user_target_str);
            emit(buf);
        }
        s_want_scan = r->flag;
        radio_update();
        return ESP_OK;
    case OP_CONNECT:
        return op_connect(r->addr);
    case OP_DISCONNECT:
        return op_disconnect(r->addr);
    case OP_ACCEPT:
    case OP_PASSKEY:
        return op_io(r->op, r->flag, r->num);
    case OP_CANCEL:
        op_cancel();
        return ESP_OK;
    case OP_REMOVE:
        return op_remove(r->addr);
    case OP_CLEAR:
        return op_clear();
    case OP_LIST:
        r->out_len = list_json(r->buf, r->len, r->flag);
        return ESP_OK;
    case OP_PEERS:
        r->out_len = peers_json(r->buf, r->len);
        return ESP_OK;
    case OP_PAIRED:
        r->out_len = paired_json(r->buf, r->len);
        return ESP_OK;
    case OP_STATUS:
        *r->scanning = s_radio == RADIO_SCAN;
        *r->connected = false;
        *r->hid_ready = false;
        r->buf[0] = '\0';
        for (int i = 0; i < PEER_MAX; i++) {
            if (!s_peers[i].used) {
                continue;
            }
            if (!*r->connected) {
                snprintf(r->buf, r->len, "%s", s_peers[i].addr_str);
            }
            *r->connected = true;
            if (s_peers[i].state == PS_READY) {
                *r->hid_ready = true;
            }
        }
        return ESP_OK;
    case OP_RADIO:
        r->radio = radio_name();
        return ESP_OK;
    case OP_BONDS:
        r->count = bond_count_local();
        return ESP_OK;
    case OP_COMBOS:
        return apply_combos(r->addr);
    case OP_HOST_SELECT: {
        /* A switch picked directly in the UI. Handled like a combo switch: the previous host gets release-all */
        int host = (int)r->num;
        if (host < 0 || host >= ROUTER_HOST_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        int from = router_active_host();
        if (host != from) {
            router_set_active_host(host);
            router_out_t out;
            router_release_report(&out);
            route_out(&out, from);
        }
        return ESP_OK;
    }
    case OP_HOST_GET:
        r->count = router_active_host();
        return ESP_OK;
    case OP_HOST_PAIR:
        if (r->flag) {
            /* Stop input device scanning and user connection attempts, then open the window */
            s_want_scan = false;
            s_user_pending = false;
            radio_update();
        }
        return host_pair_window(r->flag);
    case OP_HOST_LIST:
        r->out_len = host_list_json(r->buf, r->len);
        return ESP_OK;
    case OP_HOST_REMOVE:
        return host_remove((int)r->num);
    case OP_HOST_PASSKEY:
        return host_passkey(r->num);
    case OP_SELFTEST:
#if CONFIG_KM_DIAG
        return host_selftest((int)r->num - 1, (int)r->count);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    case OP_STATS:
        r->out_len = host_stats_json(r->buf, r->len);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static void req_run(struct ble_npl_event *ev)
{
    s_req->result = op_dispatch(s_req);
    xSemaphoreGive(s_req_done);
}

static esp_err_t host_call(req_t *r)
{
    if (!s_req_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_req_lock, portMAX_DELAY);
    s_req = r;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_req_ev);
    xSemaphoreTake(s_req_done, portMAX_DELAY);
    s_req = NULL;
    xSemaphoreGive(s_req_lock);
    return r->result;
}

/* ---------- Public functions ---------- */

esp_err_t ble_pair_init(ble_pair_event_fn event)
{
    s_emit = event;
    s_req_lock = xSemaphoreCreateMutex();
    s_req_done = xSemaphoreCreateBinary();
    if (!s_req_lock || !s_req_done) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init failed");
    ble_npl_event_init(&s_req_ev, req_run, NULL);
    ble_npl_callout_init(&s_sec_retry, nimble_port_get_dflt_eventq(), sec_retry_cb, NULL);
    ble_npl_callout_init(&s_auto_retry, nimble_port_get_dflt_eventq(), auto_retry_cb, NULL);
    ble_gap_event_listener_register(&s_gap_listener, gap_listener, NULL);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    /* Keyboard + display: the stack picks the method per spec from the peer's IO capabilities (Just Works, passkey, numeric comparison) */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_DISPLAY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    /*
     * Do not distribute our IRK. We do not use an RPA, but NimBLE sends a real IRK if it distributes the ID key.
     * Core says a device that does not use an RPA should use an all-zero IRK (Core v6.0 Vol 3 Part H 3.6.4); Microsoft
     * says to exchange IRKs only with an RPA (Bluetooth Accessory Guidelines 7.2). A peer that receives a real IRK and
     * puts us in Network Privacy mode may ignore our public-address advertising (Core v6.0 Vol 6 Part B 4.7).
     * We still receive the peer's IRK (we need to resolve the peer's RPA).
     */
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = on_store_status;
    ble_store_config_init();
    ESP_RETURN_ON_ERROR(host_init(event), TAG, "host gatt init failed");

    /* Router: the host task has not started yet, so configuring it directly here is safe */
    router_init();
    router_set_active_host(cfg_active_host_get());
    char combos[CFG_COMBO_MAX + 1] = "";
    cfg_combo_get(combos, sizeof(combos));
    if (apply_combos(combos) != ESP_OK) {
        ESP_LOGW(TAG, "stored combo invalid, using default");
        apply_combos(NULL);
    }

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t ble_pair_scan(bool on)
{
    req_t r = {.op = OP_SCAN, .flag = on};
    return host_call(&r);
}

size_t ble_pair_list_json(char *buf, size_t len, bool all)
{
    req_t r = {.op = OP_LIST, .buf = buf, .len = len, .flag = all};
    host_call(&r);
    return r.out_len;
}

size_t ble_pair_peers_json(char *buf, size_t len)
{
    req_t r = {.op = OP_PEERS, .buf = buf, .len = len};
    host_call(&r);
    return r.out_len;
}

size_t ble_pair_paired_json(char *buf, size_t len)
{
    req_t r = {.op = OP_PAIRED, .buf = buf, .len = len};
    host_call(&r);
    return r.out_len;
}

esp_err_t ble_pair_paired_remove(const char *addr)
{
    req_t r = {.op = OP_REMOVE, .addr = addr};
    return host_call(&r);
}

esp_err_t ble_pair_paired_clear(void)
{
    req_t r = {.op = OP_CLEAR};
    return host_call(&r);
}

esp_err_t ble_pair_connect(const char *addr)
{
    req_t r = {.op = OP_CONNECT, .addr = addr};
    return host_call(&r);
}

esp_err_t ble_pair_disconnect(const char *addr)
{
    req_t r = {.op = OP_DISCONNECT, .addr = addr};
    return host_call(&r);
}

esp_err_t ble_pair_accept(bool accept)
{
    req_t r = {.op = OP_ACCEPT, .flag = accept};
    return host_call(&r);
}

esp_err_t ble_pair_passkey(uint32_t code)
{
    req_t r = {.op = OP_PASSKEY, .num = code};
    return host_call(&r);
}

void ble_pair_cancel(void)
{
    req_t r = {.op = OP_CANCEL};
    host_call(&r);
}

void ble_pair_status(bool *scanning, bool *connected, bool *hid_ready, char *peer, size_t peer_len)
{
    req_t r = {.op = OP_STATUS, .scanning = scanning, .connected = connected, .hid_ready = hid_ready,
               .buf = peer, .len = peer_len};
    if (host_call(&r) != ESP_OK) {
        *scanning = *connected = *hid_ready = false;
        peer[0] = '\0';
    }
}

const char *ble_pair_radio(void)
{
    req_t r = {.op = OP_RADIO, .radio = "idle"};
    host_call(&r);
    return r.radio;
}

int ble_pair_bond_count(void)
{
    req_t r = {.op = OP_BONDS, .count = -1};
    host_call(&r);
    return r.count;
}

esp_err_t ble_pair_set_combos(const char *text)
{
    req_t r = {.op = OP_COMBOS, .addr = text};
    return host_call(&r);
}

esp_err_t ble_pair_select_host(int host)
{
    req_t r = {.op = OP_HOST_SELECT, .num = (uint32_t)host};
    return host_call(&r);
}

int ble_pair_active_host(void)
{
    req_t r = {.op = OP_HOST_GET, .count = 0};
    host_call(&r);
    return r.count;
}

esp_err_t ble_pair_host_pair(bool open)
{
    req_t r = {.op = OP_HOST_PAIR, .flag = open};
    return host_call(&r);
}

size_t ble_pair_host_list_json(char *buf, size_t len)
{
    req_t r = {.op = OP_HOST_LIST, .buf = buf, .len = len};
    host_call(&r);
    return r.out_len;
}

esp_err_t ble_pair_selftest(int host, int minutes)
{
    req_t r = {.op = OP_SELFTEST, .num = (uint32_t)host, .count = minutes};
    return host_call(&r);
}

size_t ble_pair_stats_json(char *buf, size_t len)
{
    req_t r = {.op = OP_STATS, .buf = buf, .len = len};
    host_call(&r);
    return r.out_len;
}

esp_err_t ble_pair_host_remove(int index)
{
    req_t r = {.op = OP_HOST_REMOVE, .num = (uint32_t)index};
    return host_call(&r);
}

esp_err_t ble_pair_host_passkey(uint32_t code)
{
    req_t r = {.op = OP_HOST_PASSKEY, .num = code};
    return host_call(&r);
}
