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
#include "host.h"
#include "host_report.h"
#include "json_mini.h"
#include "router.h"
#include "version.h"

static const char *TAG = "host";

#define HOST_LINK_MAX 4          /* 호스트 3 + 페어링 중인 후보 1 */
#define WINDOW_MS 180000         /* 호스트 추가 창: HOGP의 Limited Discoverable 상한 (H-9) */
#define FAST_ADV_MS 30000        /* 빠른 광고를 유지하는 시간 */
#define CANDIDATE_MS 60000       /* 후보 연결이 페어링을 마쳐야 하는 시간 */
#define PARAM_RETRY_MS 2000      /* 연결 값 요청이 상대의 절차와 겹쳤을 때 다시 요청하기까지 */
#define PARAM_RETRY_MAX 2
#define SEC_REQ_MS 1000          /* 본딩된 호스트가 이 시간 안에 암호화를 시작하지 않으면 Security Request */
/* 광고 간격 (단위 0.625 ms): Apple 권장 20 ms 다음 152.5 ms (H-40), 창은 30 ms (Swift Pair, H-45) */
#define ADV_ITVL_RECONNECT_FAST 32
#define ADV_ITVL_WINDOW_FAST 48
#define ADV_ITVL_SLOW 244
#define DEVICE_NAME "KM Router"
#define APPEARANCE_KEYBOARD 0x03C1
#define HCI_USER_TERM 0x13

/* ---------- GATT 표 (핸들 고정. 순서를 바꾸지 않는다, H-17) ---------- */

enum {
    A_MANUF = 1, A_MODEL, A_FWREV, A_PNP, A_BATTERY,
    A_HID_INFO, A_REPORT_MAP, A_CONTROL_POINT, A_PROTOCOL_MODE,
    A_REPORT_KB, A_REPORT_MOUSE, A_REPORT_CONSUMER, A_REPORT_SYSTEM, A_REPORT_LED,
    A_BOOT_KB_IN, A_BOOT_KB_OUT, A_BOOT_MOUSE_IN,
};

/* 알림을 보내는 특성 (연결별 구독 비트 번호) */
enum { N_KB, N_MOUSE, N_CONSUMER, N_SYSTEM, N_BOOT_KB, N_BOOT_MOUSE, N_COUNT };
static uint16_t s_notify_handle[N_COUNT];

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ref_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

/* 보호: HID, 배터리, 장치 정보의 값은 인증된(MITM) 본딩이 있어야 읽고 쓴다 (HOGP 6.1 Level 3).
 * 서비스와 특성 선언(탐색)은 보호하지 않는다 (Apple H-41) */
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
        /* 장치 정보 (DIS, 정확히 하나, H-1). PnP ID는 HOGP가 필수로 만든다 */
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
        /* 배터리 (USB 전원이라 늘 100). Notify는 선택이라 두지 않는다 */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = chr_access, .arg = (void *)A_BATTERY, .flags = R_AUTH},
            {0},
        },
    },
    {
        /* HID 서비스 (HIDS 1.0 표 2.1) */
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

/* ---------- 호스트 연결 상태 ---------- */

typedef enum { HL_CANDIDATE, HL_CONNECTED, HL_SECURED, HL_READY } hl_state_t;

typedef struct {
    bool used;
    uint16_t conn;
    hl_state_t state;
    ble_addr_t id;
    char id_str[18];
    int index;                   /* 호스트 번호 (0부터). 후보면 -1 */
    uint8_t sub;                 /* 구독 비트 (N_*) */
    uint8_t protocol_mode;       /* 1 Report, 0 Boot. 연결마다 Report로 시작한다 (H-3) */
    bool suspended;
    uint8_t led;
    host_sent_t sent;
    uint8_t boot_kb[HOST_KB_LEN];
    uint8_t boot_buttons;
    host_motion_t motion;
    uint8_t buttons;
    bool mouse_pending;
    int64_t next_mouse_us;
    uint16_t itvl;               /* 연결 간격 (1.25 ms 단위) */
    bool pk_wait;                /* 패스키 입력을 기다린다 */
    int64_t deadline_us;         /* 후보: 이 시각까지 페어링을 마쳐야 한다 */
    int64_t param_retry_us;      /* 0이 아니면 이 시각에 연결 값을 다시 요청한다 */
    int64_t sec_req_us;          /* 0이 아니면 이 시각에 Security Request를 보낸다 */
    uint8_t param_retries;
} host_link_t;

static host_link_t s_links[HOST_LINK_MAX];
/*
 * 마우스 송신 타이머는 칸마다 한 번만 만든다. 이 포트의 callout 초기화는 부를 때마다 타이머 자원을
 * 새로 만들므로, 연결마다 초기화하면 자원이 샌다. 연결 상태(s_links)를 지워도 타이머는 남긴다.
 */
static struct ble_npl_callout s_mouse_timer[HOST_LINK_MAX];

static struct ble_npl_callout *mouse_timer(const void *l);
static host_event_fn s_emit;
static bool s_synced;
static bool s_window;
static int64_t s_window_until_us;
static int64_t s_fast_until_us;
static int s_adv_hold;
static int s_adv_mode;           /* 0 끔, 1 재연결, 2 창 */
static int s_adv_itvl;
static struct ble_npl_callout s_tick;

static int host_gap_event(struct ble_gap_event *event, void *arg);
static void adv_update(void);
static void request_conn_params(host_link_t *l);
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

/* ---------- 메타 (호스트 본딩) ---------- */

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

/* 본딩된 호스트의 번호. 아니면 -1 */
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

/* 끊겨 있는 본딩 호스트가 있는가 (재연결 광고가 필요한가) */
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

/* ---------- GATT 접근 ---------- */

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
    }
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int which = (int)(uintptr_t)arg;
    host_link_t *l = link_by_conn(conn);
    bool write = ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR;
    static const uint8_t zeros[HOST_MOUSE_LEN] = {0};

    switch (which) {
    case A_MANUF:
        return append(ctxt, "KM Router (DIY)", 15);
    case A_MODEL:
        return append(ctxt, "KM Router ESP32-S3", 18);
    case A_FWREV:
        return append(ctxt, KM_FW_VERSION, strlen(KM_FW_VERSION));
    case A_PNP: {
        /* Vendor ID Source 2(USB-IF), VID 0x303A, PID 0xA001, 버전 0x0100. 개인용 임시값 (설계 6장 Q1) */
        static const uint8_t pnp[7] = {0x02, 0x3A, 0x30, 0x01, 0xA0, 0x00, 0x01};
        return append(ctxt, pnp, sizeof(pnp));
    }
    case A_BATTERY: {
        static const uint8_t full = 100;
        return append(ctxt, &full, 1);
    }
    case A_HID_INFO: {
        /* bcdHID 1.11, 나라 0, Flags: RemoteWake | NormallyConnectable */
        static const uint8_t info[4] = {0x11, 0x01, 0x00, 0x03};
        return append(ctxt, info, sizeof(info));
    }
    case A_REPORT_MAP:
        return append(ctxt, HOST_REPORT_MAP, HOST_REPORT_MAP_LEN);
    case A_CONTROL_POINT: {
        uint8_t v;
        int rc = read_byte(ctxt, &v);
        if (rc == 0 && l) {
            l->suspended = v == 0;   /* 0 Suspend, 1 Exit Suspend. 장치 쪽 동작은 규격이 정하지 않는다 */
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

/* ---------- 보내기 ---------- */

static void notify(host_link_t *l, int n, const uint8_t *data, uint16_t len)
{
    if (!(l->sub & (1u << n)) || l->state != HL_READY) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return;
    }
    int rc = ble_gatts_notify_custom(l->conn, s_notify_handle[n], om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d", rc);
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
        if (host_sent_update(l->boot_kb, kb, HOST_KB_LEN)) {
            notify(l, N_BOOT_KB, kb, HOST_KB_LEN);
        }
    } else if (host_sent_update(l->sent.kb, kb, HOST_KB_LEN)) {
        notify(l, N_KB, kb, HOST_KB_LEN);
    }
}

static void send_consumer(host_link_t *l, const uint16_t *usages, uint8_t count)
{
    if (boot_mode(l)) {
        return;   /* Boot 호스트는 Report 리포트를 무시한다 */
    }
    uint8_t c[HOST_CONSUMER_LEN];
    host_consumer_report(usages, count, c);
    if (host_sent_update(l->sent.consumer, c, HOST_CONSUMER_LEN)) {
        notify(l, N_CONSUMER, c, HOST_CONSUMER_LEN);
    }
}

/* 모인 마우스 이동과 버튼을 한 번 보낸다 */
static void flush_mouse(host_link_t *l)
{
    if (!l->mouse_pending) {
        return;
    }
    if (boot_mode(l)) {
        uint8_t b[HOST_BOOT_MOUSE_LEN];
        host_boot_mouse_report(&l->motion, l->buttons, b);
        l->boot_buttons = b[0];
        notify(l, N_BOOT_MOUSE, b, sizeof(b));
    } else {
        uint8_t m[HOST_MOUSE_LEN];
        host_mouse_report(&l->motion, l->buttons, m);
        l->sent.buttons = l->buttons;
        notify(l, N_MOUSE, m, sizeof(m));
    }
    l->mouse_pending = !host_motion_empty(&l->motion);   /* 다 못 담았으면 다음 간격에 */
    int64_t itvl_us = (int64_t)(l->itvl ? l->itvl : 12) * 1250;
    l->next_mouse_us = now_us() + itvl_us;
    if (l->mouse_pending) {
        ble_npl_callout_reset(mouse_timer(l), ble_npl_time_ms_to_ticks32((uint32_t)(itvl_us / 1000) + 1));
    }
}

static struct ble_npl_callout *mouse_timer(const void *l)
{
    return &s_mouse_timer[(const host_link_t *)l - s_links];
}

static void mouse_timer_cb(struct ble_npl_event *ev)
{
    host_link_t *l = ble_npl_event_get_arg(ev);
    if (l && l->used) {
        flush_mouse(l);
    }
}

/*
 * 마우스: 이동은 연결 간격마다 최대 한 번 보낸다. 컨트롤러에 쌓여 지연이 늘지 않게 하기 위해서다 (H-32).
 * 버튼이 바뀌면 그때까지 모인 이동과 함께 바로 보낸다.
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
    l->mouse_pending = true;
    int64_t now = now_us();
    if (button_change || now >= l->next_mouse_us) {
        flush_mouse(l);
    } else {
        ble_npl_callout_reset(mouse_timer(l),
                              ble_npl_time_ms_to_ticks32((uint32_t)((l->next_mouse_us - now) / 1000) + 1));
    }
}

/* 이 연결에 0이 아닌 값으로 남은 리포트를 0으로 맞춘다 (전환 때 이전 호스트로) */
static void release_all(host_link_t *l)
{
    if (!l || l->state != HL_READY) {
        return;
    }
    host_release_t r = host_release_needed(&l->sent);
    uint8_t zeros[HOST_KB_LEN > HOST_MOUSE_LEN ? HOST_KB_LEN : HOST_MOUSE_LEN] = {0};
    if (boot_mode(l)) {
        if (memcmp(l->boot_kb, zeros, HOST_KB_LEN) != 0) {
            memset(l->boot_kb, 0, HOST_KB_LEN);
            notify(l, N_BOOT_KB, zeros, HOST_KB_LEN);
        }
        if (l->boot_buttons) {
            l->boot_buttons = 0;
            notify(l, N_BOOT_MOUSE, zeros, HOST_BOOT_MOUSE_LEN);
        }
    } else {
        if (r.kb) {
            memset(l->sent.kb, 0, HOST_KB_LEN);
            notify(l, N_KB, zeros, HOST_KB_LEN);
        }
        if (r.mouse) {
            l->sent.buttons = 0;
            notify(l, N_MOUSE, zeros, HOST_MOUSE_LEN);
        }
        if (r.consumer) {
            memset(l->sent.consumer, 0, HOST_CONSUMER_LEN);
            notify(l, N_CONSUMER, zeros, HOST_CONSUMER_LEN);
        }
        if (r.system) {
            l->sent.system = 0;
            notify(l, N_SYSTEM, zeros, HOST_SYSTEM_LEN);
        }
    }
    memset(&l->motion, 0, sizeof(l->motion));
    l->buttons = 0;
    l->mouse_pending = false;
}

void host_send(const router_out_t *out, int switched_from)
{
    if (switched_from >= 0) {
        /* 뗌은 이전 호스트 연결로 직접 보낸다 (활성 값과 무관하게, H-49의 ZMK 버그를 피한다) */
        release_all(link_by_index(switched_from));
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"event\":\"host_online\",\"host\":%d,\"online\":%s}",
                 router_active_host() + 1, host_online(router_active_host()) ? "true" : "false");
        emit(buf);
    }
    host_link_t *l = link_by_index(router_active_host());
    if (!l || l->state != HL_READY) {
        return;   /* 끊긴 호스트로 가는 입력은 버린다 (나중에 재생되는 키 입력은 위험하다) */
    }
    if (switched_from >= 0) {
        release_all(l);   /* 새 호스트에 남아 있던 값이 있으면 0으로 맞춘다 (H-55) */
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
    if (out->buttons_changed || out->motion) {
        send_mouse(l, out);
    }
}

/* ---------- 광고 ---------- */

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
    /* 창: Limited Discoverable. 재연결: 발견 불가 (본딩된 호스트는 플래그와 상관없이 붙는다, H-44) */
    f.flags = BLE_HS_ADV_F_BREDR_UNSUP | (mode == 2 ? BLE_HS_ADV_F_DISC_LTD : 0);
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    f.uuids16 = &hid_uuid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.appearance = APPEARANCE_KEYBOARD;
    f.appearance_is_present = 1;
    f.name = (const uint8_t *)DEVICE_NAME;
    f.name_len = strlen(DEVICE_NAME);
    f.name_is_complete = 1;   /* Windows는 능동 스캔을 하지 않으므로 이름을 광고 패킷에 넣는다 (H-45) */
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
    p.conn_mode = BLE_GAP_CONN_MODE_UND;   /* Directed 광고는 쓰지 않는다 (Apple H-40) */
    p.disc_mode = mode == 2 ? BLE_GAP_DISC_MODE_LTD : BLE_GAP_DISC_MODE_NON;
    p.itvl_min = itvl;
    p.itvl_max = itvl;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &p, host_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv start rc=%d", rc);   /* 빈 연결 칸이 없으면 ENOMEM (H-21). 칸이 생기면 다시 정한다 */
        return;
    }
    s_adv_mode = mode;
    s_adv_itvl = itvl;
}

/* 광고 상태를 정한다: 창 > 끊긴 본딩 호스트 재연결 > 끔 */
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

/* 1초마다: 창 시간, 빠른 광고 시간, 후보 연결 시간을 본다 */
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

/* ---------- 호스트 연결 이벤트 ---------- */

static void emit_link(const char *event, const host_link_t *l, const char *extra)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "{\"event\":\"%s\",\"host\":%d,\"addr\":\"%s\"%s}", event, l->index + 1, l->id_str,
             extra ? extra : "");
    emit(buf);
}

static void request_conn_params(host_link_t *l)
{
    /* 15 ms (Apple 15의 배수, Microsoft 키보드 15 ms 이상), Latency 4, 감시 6초 (H-39, H-43, 설계 4.8) */
    struct ble_gap_upd_params p = {
        .itvl_min = 12, .itvl_max = 12, .latency = 4, .supervision_timeout = 600, .min_ce_len = 0, .max_ce_len = 0,
    };
    int rc = ble_gap_update_params(l->conn, &p);
    if (rc != 0) {
        ESP_LOGW(TAG, "conn param request rc=%d", rc);
    }
}

/*
 * 본딩된 호스트가 다시 붙고도 암호화를 시작하지 않으면 우리가 Security Request를 보낸다
 * (Core v5.4 Vol 3 Part H 2.4.6: Peripheral이 보안을 요청하면 Central은 저장된 키로 암호화한다).
 * 실기: Mac이 재연결 뒤 90초 넘게 암호화하지 않은 적이 있다. 보안 절차 칸은 전체에서 1개라
 * 다른 연결이 쓰고 있으면(ENOMEM) 1초 뒤 다시 시도한다.
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
    /* 호스트의 GAP 이름을 읽어 목록에 보인다 (Mac은 컴퓨터 이름) */
    gap_name_read(l->conn, on_host_name);
}

static void on_connect(struct ble_gap_event *event)
{
    s_adv_mode = 0;   /* Peripheral 연결이 생기면 광고는 끝난다 (H-21) */
    if (event->connect.status != 0) {
        adv_update();
        return;
    }
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(event->connect.conn_handle, &d) != 0) {
        adv_update();
        return;
    }
    int index = host_index_of(&d.peer_id_addr);
    host_link_t *l = NULL;
    for (int i = 0; i < HOST_LINK_MAX && !l; i++) {
        if (!s_links[i].used) {
            l = &s_links[i];
        }
    }
    /* 본딩된 호스트이거나, 창이 열려 있고 자리가 남았을 때만 받는다. 광고 필터를 못 쓰므로 여기서 거른다 (설계 4.1) */
    bool accept = l && (index >= 0 ? !link_by_index(index) : (s_window && host_count() < ROUTER_HOST_MAX));
    if (!accept) {
        ble_gap_terminate(event->connect.conn_handle, HCI_USER_TERM);
        adv_update();
        return;
    }
    memset(l, 0, sizeof(*l));
    l->used = true;
    l->conn = event->connect.conn_handle;
    l->id = d.peer_id_addr;
    l->id.type &= 1;
    val_to_str(l->id.val, l->id_str);
    l->index = index;
    l->state = index >= 0 ? HL_CONNECTED : HL_CANDIDATE;
    l->protocol_mode = 1;
    l->itvl = d.conn_itvl;
    l->deadline_us = now_us() + (int64_t)CANDIDATE_MS * 1000;
    if (index >= 0) {
        l->led = router_host_led(index);
        l->sec_req_us = now_us() + (int64_t)SEC_REQ_MS * 1000;
    }
    char extra[96];
    snprintf(extra, sizeof(extra), ",\"bonded\":%s,\"itvl\":%u", index >= 0 ? "true" : "false", d.conn_itvl);
    emit_link("host_connected", l, extra);
    adv_update();
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
    ble_npl_callout_stop(mouse_timer(l));
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"reason\":%d", event->disconnect.reason);
    emit_link("host_disconnected", l, extra);
    l->used = false;
    fast_adv();   /* 끊긴 직후는 빠르게 광고해 다시 붙게 한다 (HOGP 표 5.2) */
    adv_update();
}

static void on_enc_change(struct ble_gap_event *event)
{
    host_link_t *l = link_by_conn(event->enc_change.conn_handle);
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
        /* 끊는다. 보안 시간 초과(30초) 뒤에는 그 링크로 SMP를 다시 보낼 수 없어 새로 붙어야 한다 (Vol 3 Part H 3.4) */
        if (ok && l->index < 0) {
            /* 인증되지 않은 새 본딩은 받지 않는다 (HID 특성은 인증된 링크가 필요하다) */
            ble_addr_t id = l->id;
            ble_gap_terminate(l->conn, HCI_USER_TERM);
            ble_store_util_delete_peer(&id);
        } else {
            ble_gap_terminate(l->conn, HCI_USER_TERM);
        }
        return;
    }
    /*
     * 키 분배가 끝났으니 peer_id_addr가 identity다. 연결 때 값은 RPA일 수 있다 (Mac은 RPA로 붙는다).
     * 본딩 저장소의 키가 identity이므로 메타도 identity로 적어야 둘이 맞는다.
     */
    l->id = d.peer_id_addr;
    l->id.type &= 1;
    val_to_str(l->id.val, l->id_str);
    if (l->index < 0) {
        /* 새 호스트: 비어 있는 가장 작은 번호를 준다 */
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
        host_pair_window(false);   /* 한 대 붙였으면 창을 닫는다 */
    }
    l->state = HL_SECURED;
    /* 본딩된 호스트는 구독(CCCD)을 저장소에서 되살린다. 이미 켜져 있으면 바로 준비 */
    if (l->sub) {
        link_ready(l);
    }
}

static void on_subscribe(struct ble_gap_event *event)
{
    host_link_t *l = link_by_conn(event->subscribe.conn_handle);
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
    host_link_t *l = link_by_conn(event->passkey.conn_handle);
    if (!l) {
        return;
    }
    if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        /* 호스트 화면의 6자리를 입력받는다. 라우터 키보드 입력은 이동안 어느 호스트에도 가지 않는다 */
        l->pk_wait = true;
        release_all(link_by_index(router_active_host()));   /* 활성 호스트에 눌린 채 남지 않게 */
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
    host_link_t *l = link_by_conn(event->repeat_pairing.conn_handle);
    /*
     * 본딩된 호스트가 다시 페어링하려 한다 (호스트에서 우리를 지웠다).
     * 창 안에서, 새 페어링이 인증될 때만 옛 키를 지우고 다시 받는다. 메타(번호, 이름)는 유지한다.
     * 창 밖은 거부하고 끊는다 (인증 전에 본딩을 지우게 하는 주소 위조를 막는다, 입력 쪽 P12).
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
            /*
             * 우리 요청이 상대(호스트)의 연결 값 절차와 겹치면 거절된다 (HCI 0x2A Different Transaction
             * Collision, 0x23 LL Procedure Collision). 실기: Mac 재연결 직후 0x2A. 잠시 뒤 다시 요청한다.
             * 요청일 뿐이라 호스트가 거절하거나 다른 값을 정하면 그 값으로 간다. 겹침이 아닌 거절은 다시 요청하지 않는다.
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

/* ---------- 명령 ---------- */

bool host_window_open(void)
{
    return s_window;
}

esp_err_t host_pair_window(bool open)
{
    if (open && host_count() >= ROUTER_HOST_MAX) {
        return ESP_ERR_NO_MEM;   /* 호스트 3대가 찼다. GUI에서 지울 호스트를 고른다 */
    }
    if (open == s_window) {
        return ESP_OK;
    }
    s_window = open;
    /* 창이 열린 동안만 키보드로 알린다 (호스트 화면의 숫자를 우리가 입력하는 Passkey Entry, H-8, H-26) */
    ble_hs_cfg.sm_io_cap = open ? BLE_HS_IO_KEYBOARD_ONLY : BLE_HS_IO_KEYBOARD_DISPLAY;
    if (open) {
        s_window_until_us = now_us() + (int64_t)WINDOW_MS * 1000;
        fast_adv();
    } else {
        /* 페어링을 마치지 못한 후보 연결은 끊는다 */
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
        host_adv_hold(true);   /* ble_gap_unpair는 광고 중이면 EBUSY (H-64) */
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

/* ---------- 초기화 ---------- */

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
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        ble_npl_callout_init(&s_mouse_timer[i], nimble_port_get_dflt_eventq(), mouse_timer_cb, &s_links[i]);
    }
    return ESP_OK;
}

void host_on_sync(void)
{
    s_synced = true;
    fast_adv();   /* 부팅 직후 끊긴 본딩 호스트가 빨리 붙게 한다 */
    adv_update();
    ble_npl_callout_reset(&s_tick, ble_npl_time_ms_to_ticks32(1000));
}

void host_on_reset(void)
{
    s_synced = false;
    s_adv_mode = 0;
    for (int i = 0; i < HOST_LINK_MAX; i++) {
        ble_npl_callout_stop(&s_mouse_timer[i]);
    }
    memset(s_links, 0, sizeof(s_links));
}
