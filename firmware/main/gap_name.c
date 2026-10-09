#include <string.h>

#include "esp_log.h"

#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

#include "gap_name.h"
#include "json_mini.h"

static const char *TAG = "gap_name";

/* 읽는 중인 연결의 상태. 연결 핸들로 찾는다 */
typedef struct {
    bool used;
    uint16_t conn;
    uint16_t handle;   /* 0이면 아직 응답이 없다 */
    bool more;         /* Read By Type 응답이 꽉 차서 값이 더 있을 수 있다 */
    int len;
    char buf[GAP_NAME_MAX + 1];
    gap_name_fn fn;
} name_read_t;

static name_read_t s_reads[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];

static name_read_t *find(uint16_t conn)
{
    for (int i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++) {
        if (s_reads[i].used && s_reads[i].conn == conn) {
            return &s_reads[i];
        }
    }
    return NULL;
}

static void finish(name_read_t *r)
{
    gap_name_fn fn = r->fn;
    uint16_t conn = r->conn;
    int n = utf8_complete_len(r->buf, r->len);
    r->buf[n] = '\0';
    char name[GAP_NAME_MAX + 1];
    memcpy(name, r->buf, n + 1);
    r->used = false;
    if (n > 0 && fn) {
        fn(conn, name);
    }
}

static void append(name_read_t *r, const struct os_mbuf *om, uint16_t offset)
{
    uint16_t n = OS_MBUF_PKTLEN(om);
    if (offset != r->len || r->len >= GAP_NAME_MAX) {
        return;   /* 순서가 어긋난 조각은 받지 않는다 */
    }
    if (n > GAP_NAME_MAX - r->len) {
        n = GAP_NAME_MAX - r->len;
    }
    os_mbuf_copydata(om, 0, n, r->buf + r->len);
    r->len += n;
}

static int on_long(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    name_read_t *r = find(conn);
    if (!r) {
        return 1;
    }
    if (error->status == 0 && attr && attr->om) {
        append(r, attr->om, attr->offset);
        return r->len >= GAP_NAME_MAX ? 1 : 0;   /* 다 받았으면 남은 조각은 읽지 않는다 */
    }
    /* EDONE 또는 오류: 그때까지 받은 만큼 쓴다 (Read By Type로 받은 앞부분은 남아 있다) */
    if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "read long status=%d, using %d bytes", error->status, r->len);
    }
    finish(r);
    return 0;
}

static int on_by_type(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    name_read_t *r = find(conn);
    if (!r) {
        return 1;
    }
    if (error->status == 0 && attr && attr->om) {
        if (r->handle == 0) {   /* Device Name은 하나다. 혹시 더 오면 첫 것만 쓴다 */
            r->handle = attr->handle;
            uint16_t n = OS_MBUF_PKTLEN(attr->om);
            append(r, attr->om, 0);
            /*
             * 응답은 보낸 쪽의 그때 MTU로 잘린다. 재연결 직후에는 MTU 교환과 겹쳐, 응답은 23(기본값) 기준으로
             * 잘려 오는데 지금 MTU는 이미 커져 있을 수 있다 (실기: Mac 재연결 때 19바이트에서 잘림).
             * 그래서 기본 MTU 기준(19바이트) 이상이면 늘 이어 읽는다. 값이 거기서 끝나면 빈 응답이 온다.
             */
            r->more = n >= BLE_ATT_MTU_DFLT - 4 && r->len < GAP_NAME_MAX;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE && r->more) {
        /* 긴 읽기는 받은 다음 바이트부터 이어 읽는다 */
        if (ble_gattc_read_long(conn, r->handle, (uint16_t)r->len, on_long, NULL) == 0) {
            return 0;
        }
    } else if (error->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "device name read failed status=%d", error->status);
    }
    finish(r);
    return 0;
}

int gap_name_read(uint16_t conn, gap_name_fn fn)
{
    name_read_t *r = find(conn);
    if (r) {
        return BLE_HS_EALREADY;
    }
    for (int i = 0; i < CONFIG_BT_NIMBLE_MAX_CONNECTIONS && !r; i++) {
        if (!s_reads[i].used) {
            r = &s_reads[i];
        }
    }
    if (!r) {
        return BLE_HS_ENOMEM;
    }
    memset(r, 0, sizeof(*r));
    r->used = true;
    r->conn = conn;
    r->fn = fn;
    static const ble_uuid16_t name_uuid = BLE_UUID16_INIT(0x2A00);
    int rc = ble_gattc_read_by_uuid(conn, 1, 0xFFFF, &name_uuid.u, on_by_type, NULL);
    if (rc != 0) {
        r->used = false;
    }
    return rc;
}
