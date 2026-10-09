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

/* State of a connection being read. Looked up by connection handle */
typedef struct {
    bool used;
    uint16_t conn;
    uint16_t handle;   /* 0 means no response yet */
    bool more;         /* The Read By Type response was full, so the value may continue */
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
        return;   /* Ignore out-of-order fragments */
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
        return r->len >= GAP_NAME_MAX ? 1 : 0;   /* Once full, do not read the remaining fragments */
    }
    /* EDONE or error: use what has been received so far (the head from Read By Type is still there) */
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
        if (r->handle == 0) {   /* There is one Device Name. If more arrive, use only the first */
            r->handle = attr->handle;
            uint16_t n = OS_MBUF_PKTLEN(attr->om);
            append(r, attr->om, 0);
            /*
             * The response is truncated to the sender's MTU at that moment. Right after a reconnect this overlaps
             * the MTU exchange: the response arrives truncated for 23 (the default) while the current MTU may
             * already be larger (real hardware: cut at 19 bytes on Mac reconnect).
             * So always continue reading at the default-MTU size (19 bytes) or more. If the value ends there,
             * an empty response comes back.
             */
            r->more = n >= BLE_ATT_MTU_DFLT - 4 && r->len < GAP_NAME_MAX;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE && r->more) {
        /* The long read continues from the byte after what was received */
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
