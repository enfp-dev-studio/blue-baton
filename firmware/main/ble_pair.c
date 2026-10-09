#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
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
/* 광고의 외형(appearance)이 HID 분류(0x03C0~0x03FF)면 입력 장치로 본다 */
#define HID_APPEARANCE_MASK 0xFFC0
#define HID_APPEARANCE_BASE 0x03C0
#define CHR_PROP_NOTIFY 0x10

#define FOUND_MAX 16
#define PEER_MAX 2           /* 동시 입력 연결 (설계값: 입력 2 + 호스트 3) */
#define SVC_MAX 3            /* 한 장치의 HID 서비스 인스턴스 (HOGP: 여러 개일 수 있다) */
#define REPORT_MAX 16        /* 한 장치의 입력 리포트 전체 */
#define CHR_MAX 48
#define MAP_MAX 512          /* HID Service 규격의 Report Map 최대 길이 */
#define SCAN_MS 60000        /* 새 장치 스캔은 60초 뒤 스스로 멈춘다 (자동 재연결을 오래 막지 않게) */
#define CONNECT_MS 10000     /* 사용자가 고른 장치로의 연결 시도 제한 */
#define FAIL_LIMIT 3         /* 준비 전에 연속으로 끊기면 그 장치의 자동 재연결을 멈춘다 */
#define HCI_USER_TERM 0x13   /* HCI 사유: 사용자가 연결을 끊음 */

static const char *TAG = "pair";

/* ---------- 상태 (모두 NimBLE 호스트 태스크가 소유) ---------- */

typedef struct {
    bool used;
    ble_addr_t addr;         /* 광고 주소. 컨트롤러가 RPA를 풀었으면 identity 타입(2, 3)으로 온다 */
    char addr_str[18];
    char name[24];
    uint16_t appearance;
    int8_t rssi;
    bool hid;                /* 광고나 스캔 응답에 HID UUID 또는 HID 외형이 있었다 */
    bool announced;          /* device_found 이벤트를 보냈다 */
    TickType_t seen;
} found_dev_t;

typedef enum { PS_CONNECTED, PS_SECURING, PS_DISCOVERING, PS_READY } peer_state_t;

typedef enum {
    ST_SVC, ST_CHR, ST_DSC, ST_REF, ST_INFO, ST_MAP, ST_SUB, ST_DONE
} disc_stage_t;

typedef struct {
    uint16_t start;
    uint16_t end;
    uint16_t info_handle;
    uint16_t map_handle;
    uint16_t map_len;
    bool map_truncated;
    uint8_t map[MAP_MAX];
    hid_layout_t layout;     /* Report Map을 해석한 입력 필드 (라우터가 쓴다) */
    bool layout_ok;
} hid_svc_t;

typedef struct {
    uint16_t val;            /* 입력 리포트(0x2A4D, notify) 값 핸들 */
    uint16_t cccd;           /* 0이면 없음 */
    uint16_t ref;            /* Report Reference 디스크립터, 0이면 없음 */
    uint8_t svc;
    uint8_t report_id;       /* Report Reference의 ID. 디스크립터가 없으면 0 (리포트가 하나뿐인 장치) */
} report_t;

typedef struct {
    bool used;
    uint16_t conn;
    peer_state_t state;
    bool user;               /* 사용자가 고른 연결 (자동 재연결이 아님) */
    bool sec_wait;           /* 다른 연결의 보안 절차가 끝나기를 기다린다 */
    uint8_t io_action;       /* 응답을 기다리는 페어링 입력 (BLE_SM_IOACT_*) */
    ble_addr_t ota;          /* 연결된 on-air 주소 */
    char addr_str[18];
    bool id_valid;           /* 암호화 뒤 확정된 identity 주소 */
    ble_addr_t id;
    char id_str[18];
    /* GATT 탐색 */
    disc_stage_t stage;
    uint8_t idx;
    hid_svc_t svc[SVC_MAX];
    uint8_t svc_count;
    uint16_t chr_def[CHR_MAX];
    uint8_t chr_count;
    report_t rep[REPORT_MAX];
    uint8_t rep_count;
    uint8_t kinds;
} peer_t;

typedef enum { RADIO_IDLE, RADIO_SCAN, RADIO_CONNECT, RADIO_AUTO, RADIO_CANCEL } radio_t;

/* 자동 재연결 제외 표시. 사용자가 끊었거나 연속으로 실패한 장치 (RAM에만, 재부팅하면 풀린다) */
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
static bool s_user_pending;          /* 사용자가 고른 연결 대상이 있다 */
static ble_addr_t s_user_target;
static char s_user_target_str[18];
static bool s_radio_hold;            /* 본딩 삭제처럼 라디오를 멈춰야 하는 작업 중 */
static bool s_auto_dirty;            /* 자동 재연결 대상이 바뀌었다 (accept list를 다시 만든다) */
static peer_t *s_sec_owner;          /* 지금 보안 절차를 진행 중인 연결 (전체에서 하나, N-13) */

static char s_evt[1500];             /* Report Map처럼 긴 이벤트용 */

static int gap_event(struct ble_gap_event *event, void *arg);
static void radio_update(void);
static void disc_next(peer_t *p);

/* NimBLE 본딩 저장소 초기화. 공개 헤더가 없어서 ESP-IDF 예제(blecent)처럼 직접 선언한다 */
void ble_store_config_init(void);

static void emit(const char *json)
{
    if (s_emit) {
        s_emit(json);
    }
}

/* ---------- 주소 ---------- */

static void val_to_str(const uint8_t val[6], char *out)
{
    /* NimBLE은 주소 바이트를 뒤에서부터 저장하므로 거꾸로 적는다 */
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

/* identity 주소 타입은 0(public), 1(random). 컨트롤러가 풀어 준 타입 2, 3도 같은 기기다 */
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

/* ---------- 본딩 (원천: NimBLE 저장소) ---------- */

/*
 * 새 페어링과 "저장된 키로 암호화 복원"을 구분하기 위한 표시.
 * 새 페어링이면 상대 키(PEER_SEC)가 저장소에 쓰이고, 복원이면 쓰이지 않는다.
 * NimBLE은 ble_hs 락을 잡은 채 저장소 콜백을 부르므로, 훅에서는 주소만 기록한다.
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

/* 호스트가 sync 중에 저장소를 다시 초기화하면 콜백이 원래 것으로 돌아가므로, sync마다 다시 건다 */
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

/* 주소 바이트로 본딩을 찾는다 (사용자는 타입 없이 주소 문자열만 준다) */
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
 * 두 저장소를 맞춘다. 원천은 본딩이다.
 * - 메타는 있는데 본딩이 없다: 키가 없으므로 연결할 수 없다. 메타를 지운다.
 * - 본딩은 있는데 메타가 없다(고아 본딩): 빈 메타를 만들어 목록에 보이고 지울 수 있게 한다.
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

/* ---------- 자동 재연결 제외 표시 ---------- */

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

/* ---------- 찾은 장치 ---------- */

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

/* 빈 칸이 없으면 가장 오래전에 본 장치를 밀어낸다 */
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
 * 광고와 스캔 응답을 주소별로 합친다. 이름과 UUID가 스캔 응답에만 있는 장치도 있다.
 * directed 광고는 본딩된 장치의 재연결 요청이므로 자동 재연결이 받고, 목록에는 올리지 않는다.
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
            return;   /* 짝이 되는 광고를 아직 못 봤다 */
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

/* ---------- 연결 ---------- */

static peer_t *peer_by_conn(uint16_t conn)
{
    for (int i = 0; i < PEER_MAX; i++) {
        if (s_peers[i].used && s_peers[i].conn == conn) {
            return &s_peers[i];
        }
    }
    return NULL;
}

/* identity(확정 뒤) 또는 on-air 주소로 연결을 찾는다 */
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

/* 연결 하나를 실패로 끝낸다. 사유를 알리고 끊는다. 슬롯은 DISCONNECT 이벤트에서 비운다 */
static void fail_peer(peer_t *p, const char *stage, int status)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"event\":\"failed\",\"addr\":\"%s\",\"stage\":\"%s\",\"status\":%d}",
             p->addr_str, stage, status);
    emit(buf);
    ble_gap_terminate(p->conn, HCI_USER_TERM);
}

/* ---------- 보안 절차 직렬화 (ESP-IDF는 SM 절차를 전체에서 동시에 1개로 고정한다) ---------- */

/*
 * 다른 연결(나중의 호스트 연결 포함)의 보안 절차가 칸을 잡고 있으면 ENOMEM이다. 그 절차가 끝나는 것을
 * 이 모듈의 연결 이벤트로는 알 수 없으므로, 전역 GAP 리스너의 ENC_CHANGE와 1초 타이머로 다시 시도한다
 * (host-side-design 3.6).
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
            /* EALREADY: 상대가 먼저 보낸 Security Request로 스택이 이미 시작했다 */
            p->sec_wait = false;
            p->state = PS_SECURING;
            s_sec_owner = p;
            return;
        }
        if (rc == BLE_HS_ENOMEM) {
            /* 다른 연결의 절차가 돌고 있다. 그것이 끝나면(리스너) 또는 1초 뒤(타이머) 다시 시도한다 */
            ble_npl_callout_reset(&s_sec_retry, ble_npl_time_ms_to_ticks32(1000));
            return;
        }
        p->sec_wait = false;
        fail_peer(p, "security_start", rc);
    }
}

/* ---------- 라디오 상태 기계 (스캔과 연결 시도는 한 번에 하나, N-12) ---------- */

/* 메타에 호스트로 적힌 본딩인가. 호스트는 입력 장치 자동 연결 대상이 아니다 (host-side-design H-59) */
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
 * 입력 연결 값. 출력까지의 지연은 입력 간격 + 출력 간격이므로 짧게(7.5~15 ms, Latency 0) 요청한다.
 * 장치가 나중에 다른 값을 요청하면 받아들인다. 감시 시간 4초.
 * 연결 시도 스캔: 사용자 연결은 쉬지 않고(빨리 찾게), 자동 재연결은 30 ms/60 ms(50%)로 라디오를 나눠 쓴다.
 * 단위: 간격 1.25 ms, 스캔 0.625 ms, 감시 10 ms.
 */
static const struct ble_gap_conn_params s_conn_user = {
    .scan_itvl = 0x0010, .scan_window = 0x0010, .itvl_min = 6, .itvl_max = 12,
    .latency = 0, .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};
static const struct ble_gap_conn_params s_conn_auto = {
    .scan_itvl = 0x0060, .scan_window = 0x0030, .itvl_min = 6, .itvl_max = 12,
    .latency = 0, .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};

static void radio_start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 0x0050,
        .window = 0x0030,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,            /* 능동 스캔: 이름과 UUID가 스캔 응답에 있는 장치도 받는다 */
        .filter_duplicates = 0,  /* 광고와 스캔 응답을 모두 받아 합친다 (note_adv) */
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

/* 본딩된 장치를 accept list에 넣고 기다린다. 목록 중 먼저 광고하는 장치에 연결된다 (광고 종류 무관) */
static void radio_start_auto(void)
{
    ble_addr_t list[CFG_PAIRED_MAX];
    int n = auto_candidates(list, CFG_PAIRED_MAX);
    if (n == 0) {
        return;
    }
    int rc = ble_gap_wl_set(list, n);
    if (rc == 0) {
        rc = ble_gap_connect(s_own_addr_type, NULL, BLE_HS_FOREVER, &s_conn_auto, gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "autoconnect start failed rc=%d", rc);
        return;
    }
    s_radio = RADIO_AUTO;
    s_auto_dirty = false;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"event\":\"autoconnect\",\"devices\":%d}", n);
    emit(buf);
}

static void radio_update(void)
{
    if (!s_synced || s_radio == RADIO_CANCEL) {
        return;   /* 취소가 끝나면(CONNECT 이벤트) 다시 불린다 */
    }
    if (s_user_pending && s_radio != RADIO_CONNECT && peers_free() == 0) {
        /* 기다리는 사이 자동 재연결이 마지막 칸을 썼다 */
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
        /* NimBLE은 스캔 취소 때 DISC_COMPLETE를 보내지 않는다 */
        ble_gap_disc_cancel();
        emit("{\"event\":\"scan_stopped\",\"reason\":\"radio\"}");
    }
    if (ble_gap_conn_active()) {
        /* 연결 시도 취소는 CONNECT 이벤트(BLE_HS_EAPP)로 끝난다. 그때 다시 정한다 */
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

/* ---------- 메타데이터 갱신 ---------- */

/*
 * 연결이 준비되었을 때와 이름을 알았을 때. 바뀐 것만 쓴다 (cfg_paired_put이 같은 내용은 건너뛴다).
 * 이름 우선순위: GATT Device Name(정식 이름) > 광고 이름(줄인 이름일 수 있다). 광고 이름은 비어 있을 때만 쓴다.
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
    rec.role = CFG_ROLE_INPUT;   /* 이 모듈은 입력 장치(보드가 Central)만 다룬다 */
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

/* ---------- GATT 탐색 (HOGP 1.0 표 4.1 순서) ---------- */

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

/* ---------- GATT 캐시 (본딩된 장치의 리포트 표와 레이아웃) ----------
 * 본딩이 유효하면 속성 캐시는 연결 사이에도 유효하다 (Core Vol 3 Part G 2.5.2, HOGP 5.2.4).
 * 본딩된 장치는 다시 연결되면 탐색이 끝나기 전부터 입력을 보내므로(CCCD 값이 유지됨),
 * 탐색이 끝날 때까지는 지난번 표로 라우팅한다. 장치를 깨운 첫 키가 버려지지 않게 하기 위해서다.
 * 지금은 RAM에만 둔다 (재부팅 뒤 첫 연결은 탐색이 끝난 뒤부터 라우팅된다).
 */
#define GATT_CACHE_MAX 4

typedef struct {
    bool used;
    ble_addr_t id;
    uint32_t stamp;
    uint8_t rep_count;
    report_t rep[REPORT_MAX];
    hid_layout_t *layout[SVC_MAX];   /* 힙. 레이아웃이 있는 서비스만 */
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

/* 탐색이 끝난 장치의 표를 저장한다. 칸이 없으면 가장 오래된 것을 밀어낸다 */
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

/* ---------- 라우팅 (입력 장치 -> 활성 호스트) ---------- */

/*
 * 출력 쪽(호스트로 가는 BLE)이 생기기 전까지는 활성 호스트로 보낼 상태를 이벤트로 보여 준다.
 * 키 상태가 바뀔 때만 보낸다 (마우스 이동은 보내지 않는다).
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
    snprintf(buf + n, sizeof(buf) - n, "]}");
    emit(buf);
}

static void route_out(const router_out_t *out, int switched_from)
{
    if (switched_from >= 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"event\":\"host_switch\",\"host\":%d,\"from\":%d}", router_active_host() + 1,
                 switched_from + 1);
        emit(buf);
        /* 이전 호스트에는 "모두 뗌"을 보낸다. 출력 쪽이 생기면 여기서 이전 호스트 연결로 보낸다 */
    }
    if (out->pre_valid) {
        /* 보류했던 조합 수정 키를 먼저 보낸다 (Ctrl+C면 Ctrl, 그다음 Ctrl+C) */
        router_out_t pre = *out;
        memcpy(pre.keys, out->pre_keys, sizeof(pre.keys));
        pre.buttons_changed = false;
        pre.consumer_changed = false;
        emit_route(&pre);
    }
    if (switched_from >= 0 || out->keys_changed || out->buttons_changed || out->consumer_changed) {
        emit_route(out);
    }
    if (out->pk_event != ROUTER_PK_NONE) {
        host_passkey_event(out);   /* 호스트 페어링: 키보드로 친 패스키 */
    }
    host_send(out, switched_from);
}

static void route_report(peer_t *p, uint16_t handle, const uint8_t *data, uint16_t len)
{
    /* 탐색이 끝났으면 지금 표, 아니면 지난번 표(본딩된 장치) */
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

/* 전환 키 조합을 적용한다. text가 비어 있으면 기본값. 형식이 틀리면 ESP_ERR_INVALID_ARG */
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

/* 이름: GAP 서비스의 Device Name(0x2A00)은 필수 항목이다. 광고에 이름이 없는 장치도 여기서 얻는다 */
static void on_name_read(uint16_t conn, const char *name)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return;
    }
    meta_update(p, name, true, false);
    char esc[48];
    char buf[160];
    json_escape(name, esc, sizeof(esc));
    snprintf(buf, sizeof(buf), "{\"event\":\"device_name\",\"addr\":\"%s\",\"name\":\"%s\"}", p->addr_str, esc);
    emit(buf);
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
             "\"services\":%u}",
             p->addr_str, p->id_str, kinds, p->rep_count, p->svc_count);
    emit(buf);
    int rc = gap_name_read(p->conn, on_name_read);
    if (rc != 0) {
        ESP_LOGW(TAG, "device name read start failed rc=%d", rc);
    }
}

static int on_cccd_write(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) {
        return 0;
    }
    if (error->status != 0) {
        /* 리포트 하나의 구독 실패로 장치 전체를 버리지 않는다. 알리고 다음으로 간다 */
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
        /* Report Reference: [Report ID, 종류(1 입력, 2 출력, 3 Feature)] */
        uint8_t ref[2] = {0, 0};
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        os_mbuf_copydata(attr->om, 0, n < 2 ? n : 2, ref);
        p->rep[p->idx].report_id = ref[0];
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
        /* Read Long은 조각마다 불린다. 끝나면 EDONE이 한 번 더 온다 */
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (s->map_len + n > MAP_MAX) {
            n = MAP_MAX - s->map_len;
            s->map_truncated = true;   /* 규격 최대(512)를 넘는 장치. 해석이 틀어질 수 있음을 알린다 */
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
 * 디스크립터는 바로 앞의 입력 리포트 값에 속한다.
 * 사이에 다른 characteristic 정의가 끼어 있으면 그 리포트의 것이 아니다.
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
        if (uuid == UUID_REPORT && (chr->properties & CHR_PROP_NOTIFY) && p->rep_count < REPORT_MAX) {
            report_t *r = &p->rep[p->rep_count++];
            memset(r, 0, sizeof(*r));
            r->val = chr->val_handle;
            r->svc = p->idx;
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
 * 탐색 단계를 하나씩 진행한다. 각 단계의 콜백이 끝나면 다시 이 함수를 부른다.
 * 서비스 → (서비스마다) 특성 → (서비스마다) 디스크립터 → (리포트마다) Report Reference
 * → (서비스마다) HID Information → (서비스마다) Report Map → (리포트마다) 알림 켜기 → 준비
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
                /* NimBLE은 시작 핸들을 이미 처리한 것으로 보므로 서비스 시작 +1부터 요청한다 */
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

static void disc_start(peer_t *p)
{
    p->state = PS_DISCOVERING;
    p->stage = ST_SVC;
    p->idx = 0;
    p->svc_count = 0;
    p->chr_count = 0;
    p->rep_count = 0;
    p->kinds = 0;
    int rc = ble_gattc_disc_svc_by_uuid(p->conn, &s_uuid_hid.u, on_svc, NULL);
    if (rc != 0) {
        disc_fail(p, "services", rc);
    }
}

/* ---------- GAP 이벤트 ---------- */

/*
 * 페어링 입력 요청.
 * NUMCMP: 숫자를 GUI에 보내고 pair_accept를 기다린다.
 * INPUT:  GUI에 패스키 입력을 요청하고 passkey를 기다린다 (상대가 숫자를 보여 줌).
 * DISP:   보드가 패스키를 만들어 GUI에 보여 주고, 사용자가 상대 장치(키보드)로 입력한다.
 */
static void on_passkey_action(struct ble_gap_event *event)
{
    peer_t *p = peer_by_conn(event->passkey.conn_handle);
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
         * EPREEMPTED: 새 장치의 IRK를 해석 리스트에 넣을 때 스택이 진행 중인 연결 시도를 끊는다 (N-10).
         * 오류가 아니므로 사용자 연결이면 그대로 다시 건다.
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

    struct ble_gap_conn_desc desc;
    peer_t *p = NULL;
    for (int i = 0; i < PEER_MAX && !p; i++) {
        if (!s_peers[i].used) {
            p = &s_peers[i];
        }
    }
    if (!p || ble_gap_conn_find(event->connect.conn_handle, &desc) != 0) {
        /* 빈 칸이 있을 때만 연결을 걸므로 정상 흐름에서는 오지 않는다 */
        ble_gap_terminate(event->connect.conn_handle, HCI_USER_TERM);
        radio_update();
        return;
    }
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->conn = event->connect.conn_handle;
    p->state = PS_CONNECTED;
    p->user = (was == RADIO_CONNECT);
    p->ota = desc.peer_ota_addr;
    val_to_str(desc.peer_ota_addr.val, p->addr_str);
    /* 이미 본딩된 장치(자동 재연결, RPA 해석)는 연결 시점에 identity를 안다. 새 장치는 암호화 뒤에 확정한다 */
    p->id = id_norm(&desc.peer_id_addr);
    val_to_str(p->id.val, p->id_str);
    p->id_valid = bonds_has(&p->id);
    if (p->user) {
        s_user_pending = false;
    }

    char buf[160];
    /* 실제 연결 값을 남긴다 (간격 단위 1.25 ms, 감시 단위 10 ms) */
    snprintf(buf, sizeof(buf),
             "{\"event\":\"connected\",\"addr\":\"%s\",\"id_addr\":\"%s\",\"bonded\":%s,\"user\":%s,"
             "\"itvl\":%u,\"latency\":%u,\"timeout\":%u}",
             p->addr_str, p->id_str, p->id_valid ? "true" : "false", p->user ? "true" : "false",
             desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
    emit(buf);

    /* HOGP 5.2.3: 연결 직후 암호화를 시작한다. 본딩이 있으면 저장된 키로 복원, 없으면 새 페어링 */
    p->sec_wait = true;
    sec_pump();
    s_auto_dirty = true;   /* 이 장치는 accept list에서 빠진다 */
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
        /* 준비 전에 끊긴 본딩 장치는 실패로 센다. 연속으로 실패하면 자동 재연결을 멈춰 무한 반복을 막는다 */
        if (p->state != PS_READY && p->id_valid && bonds_has(&p->id)) {
            auto_ctl_t *a = auto_ctl(&p->id, true);
            if (a && !a->paused && ++a->fails >= FAIL_LIMIT) {
                a->paused = true;
                snprintf(buf, sizeof(buf), "{\"event\":\"autoconnect_paused\",\"id_addr\":\"%s\",\"fails\":%u}",
                         p->id_str, a->fails);
                emit(buf);
            }
        }
        /* 그 장치가 누르던 키와 버튼은 뗀 것으로 본다 (호스트에 키가 눌린 채로 남지 않게) */
        router_out_t out;
        router_device_gone((uint8_t)(p - s_peers), &out);
        route_out(&out, -1);
        p->used = false;
    }
    s_auto_dirty = true;
    sec_pump();
    radio_update();
}

static void on_enc_change(struct ble_gap_event *event)
{
    peer_t *p = peer_by_conn(event->enc_change.conn_handle);
    if (!p) {
        return;
    }
    if (s_sec_owner == p) {
        s_sec_owner = NULL;
    }
    p->sec_wait = false;   /* 상대가 먼저 보안을 시작한 경우에도 대기 표시를 지운다 */
    p->io_action = BLE_SM_IOACT_NONE;

    struct ble_gap_conn_desc desc;
    bool have_desc = ble_gap_conn_find(p->conn, &desc) == 0;
    bool ok = event->enc_change.status == 0 && have_desc;
    /* 새 페어링이면 방금 이 상대의 키가 저장소에 쓰였다 (store_write_hook) */
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
        /* HOGP 6.1: 호스트는 장치와 본딩해야 한다. 본딩 없이 암호화만 된 연결은 쓰지 않는다 */
        fail_peer(p, "not_bonded", 0);
    } else {
        /* 키 분배가 끝난 뒤라 peer_id_addr가 identity다 (RPA 장치도 IRK로 풀린 주소) */
        p->id = id_norm(&desc.peer_id_addr);
        p->id_valid = true;
        val_to_str(p->id.val, p->id_str);
        if (new_bond) {
            bonds_reconcile();   /* 새 본딩의 메타를 만든다 */
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
        /* 키 갱신 등으로 암호화 이벤트가 다시 와도 탐색을 겹쳐 시작하지 않는다 */
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
        /* 시간이 다 됐거나(0), 스택이 끊었을 때(EPREEMPTED, N-10) 온다. 취소 때는 오지 않는다 */
        if (s_radio == RADIO_SCAN) {
            s_radio = RADIO_IDLE;
        }
        if (event->disc_complete.reason == BLE_HS_EPREEMPTED) {
            emit("{\"event\":\"scan_stopped\",\"reason\":\"preempted\"}");   /* s_want_scan은 그대로, 다시 시작한다 */
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
        emit_report(p, event->notify_rx.attr_handle, data, len);
        route_report(p, event->notify_rx.attr_handle, data, len);
        break;
    }

    case BLE_GAP_EVENT_CONN_UPDATE: {
        /* 장치가 연결 값을 바꿨다. 지연 측정을 위해 남긴다 */
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

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        on_passkey_action(event);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Peripheral 역할(호스트 연결)에서만 온다. 정책은 호스트 단계에서 정한다 (P12). 지금은 무시한다 */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    default:
        break;
    }
    return 0;
}

/*
 * 본딩 저장소가 가득 찼을 때. 기본 예제의 ble_store_util_status_rr는 가장 오래된 본딩을
 * 사용자 모르게 지우므로 쓰지 않는다. 대신 새 페어링을 거부하고 GUI에 알린다.
 * FULL은 페어링을 시작하기 전에 온다(0이 아니면 거부). OVERFLOW는 저장 단계의 실패다.
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
        /* 구독(CCCD) 같은 다른 항목의 칸이 모자라다. 설정값(MAX_CCCDS) 문제라 경고로 알린다 */
        snprintf(buf, sizeof(buf), "{\"event\":\"store_full\",\"obj\":%d}", obj);
    }
    emit(buf);
    return BLE_HS_ESTORE_CAP;
}

static void on_sync(void)
{
    /* 공개 주소가 없으면 임의 주소를 만든다 */
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
    radio_update();   /* 본딩된 장치가 있으면 자동 재연결을 건다 */
    host_on_sync();   /* 끊긴 본딩 호스트를 위한 광고 */
}

static void on_reset(int reason)
{
    /* 호스트가 리셋되면 연결과 진행 중인 절차가 모두 사라진다. sync 때 다시 시작한다 */
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

/* ---------- 다른 태스크의 요청을 호스트 태스크에서 실행 ---------- */

typedef enum {
    OP_SCAN, OP_CONNECT, OP_DISCONNECT, OP_ACCEPT, OP_PASSKEY, OP_CANCEL,
    OP_REMOVE, OP_CLEAR, OP_LIST, OP_PEERS, OP_PAIRED, OP_STATUS, OP_RADIO, OP_BONDS,
    OP_COMBOS, OP_HOST_SELECT, OP_HOST_GET,
    OP_HOST_PAIR, OP_HOST_LIST, OP_HOST_REMOVE, OP_HOST_PASSKEY,
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
        return ESP_ERR_INVALID_STATE;   /* 이미 연결되어 있다 */
    }
    if (s_user_pending) {
        return ESP_ERR_INVALID_STATE;   /* 다른 연결 시도가 진행 중이다 */
    }
    if (peers_free() == 0) {
        return ESP_ERR_NO_MEM;
    }
    ble_addr_t bond;
    if (bonds_find_val(val, &bond)) {
        /*
         * 본딩된 장치는 직접 연결(10초 시도)을 걸지 않는다. 그동안 다른 장치의 자동 재연결이 멈추기 때문이다.
         * 자동 재연결 목록에 다시 넣고(사용자가 끊었거나 실패로 멈췄던 것 해제), 스캔 중이면 멈춰서 바로 기다리게 한다.
         * 장치가 광고하면(깨우면) 연결된다.
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
        return ESP_ERR_NOT_ALLOWED;   /* 호스트 추가 창이 열린 동안에는 새 입력 장치를 페어링하지 않는다 */
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
            a->paused = true;   /* 사용자가 끊은 장치는 다시 연결할 때까지 자동으로 붙이지 않는다 */
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
            ble_gap_terminate(p->conn, HCI_USER_TERM);   /* 패스키 입력은 거절 수단이 없어 연결을 끊는다 */
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
        s_user_pending = false;   /* 지우는 장치로의 연결 시도는 그만둔다 */
    }
    /* ble_gap_unpair는 스캔이나 광고 중이면 EBUSY다. 라디오를 잠시 멈춘다 */
    s_radio_hold = true;
    radio_update();
    host_adv_hold(true);            /* ble_gap_unpair는 광고 중에도 EBUSY다 (H-64) */
    int rc = ble_gap_unpair(&id);   /* 연결 중이면 끊고, 해석 리스트와 저장소에서 지운다 */
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
        ble_store_clear();   /* CCCD 같은 나머지 항목까지 지운다 */
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
        /* 지금 연결된 기기는 "찾은 기기"가 아니다 */
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
            break;   /* 한 항목이 통째로 들어가지 않으면 멈춘다. JSON이 중간에 잘리면 안 된다 */
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
        char item[160];
        int m = snprintf(item, sizeof(item), "%s{\"addr\":\"%s\",\"id_addr\":\"%s\",\"state\":\"%s\",\"hid_ready\":%s}",
                         first ? "" : ",", p->addr_str, p->id_valid ? p->id_str : "", state_name(p->state),
                         p->state == PS_READY ? "true" : "false");
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

/* 원천은 본딩 목록이고, 이름과 종류는 메타에서 붙인다 */
static size_t paired_json(char *buf, size_t len)
{
    ble_addr_t bonds[CFG_PAIRED_MAX];
    int nb = bonds_get(bonds, CFG_PAIRED_MAX);
    size_t n = snprintf(buf, len, "[");
    for (int i = 0; i < nb; i++) {
        cfg_paired_t meta;
        bool has_meta = meta_get(&bonds[i], &meta);
        if (has_meta && meta.role == CFG_ROLE_HOST) {
            continue;   /* 호스트는 입력 장치 목록에 넣지 않는다 */
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
            return ESP_ERR_NOT_ALLOWED;   /* 호스트 추가 창과 입력 장치 페어링은 섞지 않는다 (IO 능력이 다르다) */
        }
        if (r->flag && s_user_pending) {
            /* 가장 최근의 사용자 동작이 이긴다: 기다리던 연결 시도를 그만두고 스캔한다 */
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
        /* 화면에서 직접 고른 전환. 키 조합 전환과 같은 처리: 이전 호스트에는 모두 뗌 */
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
            /* 입력 장치 스캔과 사용자 연결 시도를 멈추고 창을 연다 */
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

/* ---------- 공개 함수 ---------- */

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
    ble_gap_event_listener_register(&s_gap_listener, gap_listener, NULL);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    /* 키보드 + 디스플레이: 상대의 입출력 능력에 따라 스택이 규격대로 방식을 고른다 (Just Works, 패스키, 숫자 비교) */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_DISPLAY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    /*
     * 우리 IRK는 분배하지 않는다. 우리는 RPA를 쓰지 않는데 NimBLE은 ID 키를 주면 진짜 IRK를 보낸다.
     * Core는 RPA를 안 쓰는 기기의 IRK를 0으로 하라고 하고, Microsoft는 RPA를 쓸 때만 IRK를 교환하라고 한다.
     * 진짜 IRK를 받은 상대가 Network Privacy 모드로 두면 우리 공개 주소 광고를 받지 않을 수 있다
     * (host-side-design 3.5, H-15, H-16, H-44). 상대 IRK는 계속 받는다 (상대 RPA를 풀어야 한다).
     */
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = on_store_status;
    ble_store_config_init();
    ESP_RETURN_ON_ERROR(host_init(event), TAG, "host gatt init failed");

    /* 라우터: 호스트 태스크가 시작되기 전이라 여기서 바로 설정해도 된다 */
    router_init();
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
