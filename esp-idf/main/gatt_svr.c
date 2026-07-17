/*
 * Echo Service GATT server.
 * Implements the profile from the design proposal (§4):
 *   - RX characteristic (Write / Write-Without-Response): central sends data
 *   - TX characteristic (Notify): server echoes the data back
 *   - Status characteristic (Read, optional): current echo state byte
 *
 * Fragmentation follows design §5: outgoing notifications are prefixed
 * with a 1-byte header (7-bit sequence number + 1-bit "more fragments"
 * flag) so a client can detect drops/reordering across chunks.
 */

#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "gatt_svr.h"

static const char *TAG = "echo_gatt";

/* ---- UUIDs ----
 * Base UUID reused from the design proposal (Nordic UART Service base),
 * with the last two bytes of the 3rd group distinguishing service /
 * characteristics: ...0001 (service), ...0002 (RX), ...0003 (TX),
 * ...0004 (status). BLE_UUID128_INIT takes bytes in little-endian order,
 * i.e. reversed from the standard XXXXXXXX-XXXX-... string form.
 */
static const ble_uuid128_t echo_svc_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                      0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

static const ble_uuid128_t echo_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                      0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

static const ble_uuid128_t echo_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                      0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

//static const ble_uuid128_t echo_status_uuid =
//    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
//                      0x93, 0xF3, 0xA3, 0xB5, 0x04, 0x00, 0x40, 0x6E);

#define ECHO_MAX_PAYLOAD 512   /* generous cap on a single RX write */

static uint16_t s_tx_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_subscribed = false;

typedef enum {
    ECHO_STATE_IDLE = 0,
    ECHO_STATE_ECHOING = 1,
    ECHO_STATE_ERROR = 2,
} echo_state_t;
static volatile echo_state_t s_state = ECHO_STATE_IDLE;

static int gatt_svr_chr_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_status(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &echo_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* RX: central writes here. Write-without-response is the
                 * primary path for throughput; plain Write is also
                 * enabled for clients whose stack prefers it (design §4.2).
                 * To require pairing before writes are accepted
                 * (design §8), replace the flags line with the ENC
                 * variant below. */
                .uuid = &echo_rx_uuid.u,
                .access_cb = gatt_svr_chr_access_rx,
                .flags = BLE_GATT_CHR_F_WRITE /*| BLE_GATT_CHR_F_WRITE_NO_RSP,*/
                /* .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP
                 *        | BLE_GATT_CHR_F_WRITE_ENC, */
            },
            {
                /* TX: server notifies echoed data here. Notify-only,
                 * no read/write access callback needed. */
                .uuid = &echo_tx_uuid.u,
                .access_cb = gatt_svr_chr_access_tx,
                .val_handle = &s_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            //{
                /* Status: optional read-only diagnostic byte (design §4.2). */
            //    .uuid = &echo_status_uuid.u,
            //    .access_cb = gatt_svr_chr_access_status,
            //    .flags = BLE_GATT_CHR_F_READ,
            //},
            {
                0, /* terminator */
            },
        },
    },
    {
        0, /* terminator */
    },
};

/* ---- Connection / subscription state, driven from main.c's GAP handler ---- */

void
echo_svc_set_conn_handle(uint16_t conn_handle, bool connected)
{
    s_conn_handle = connected ? conn_handle : BLE_HS_CONN_HANDLE_NONE;
    s_state = ECHO_STATE_IDLE;
}

void
echo_svc_set_subscribed(bool subscribed)
{
    s_subscribed = subscribed;
    ESP_LOGI(TAG, "TX notifications %s", subscribed ? "enabled" : "disabled");
}

static uint16_t
echo_svc_get_mtu(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }
    return ble_att_mtu(s_conn_handle);
}

/* ---- Fragmentation / notify logic (design §5) ----
 * Each outgoing packet is prefixed with a 1-byte header:
 *   bit 0 (LSB): more-fragments flag (1 = more fragments follow)
 *   bits 1-7:    sequence number, wraps mod 128
 * Notifications on a single BLE connection are delivered in order by
 * the controller, so this header is for the client's own bookkeeping
 * (detecting an unexpected gap) rather than reordering.
 */
static void
echo_send_fragments(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_subscribed) {
        ESP_LOGW(TAG, "cannot notify: no connection or client not subscribed");
        s_state = ECHO_STATE_ERROR;
        return;
    }

    uint16_t mtu = echo_svc_get_mtu();
    /* usable payload = ATT_MTU - 3 (ATT op+handle header) - 1 (our seq header) */
    size_t chunk_size = (mtu > 4) ? (size_t)(mtu - 3 - 1) : 16;

    s_state = ECHO_STATE_ECHOING;

    size_t offset = 0;
    uint8_t seq = 0;
    uint8_t buf[512 + 1]; /* bounded by ECHO_MAX_PAYLOAD + header byte */

    if (chunk_size > sizeof(buf) - 1) {
        chunk_size = sizeof(buf) - 1;
    }

    while (offset < len) {
        size_t remaining = len - offset;
        size_t n = remaining > chunk_size ? chunk_size : remaining;
        bool more = (offset + n) < len;

        buf[0] = (uint8_t)((seq << 1) | (more ? 1 : 0));
        memcpy(&buf[1], data + offset, n);

        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, n + 1);
        if (om == NULL) {
            ESP_LOGE(TAG, "mbuf alloc failed");
            s_state = ECHO_STATE_ERROR;
            return;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
        if (rc != 0) {
            ESP_LOGE(TAG, "notify failed; rc=%d", rc);
            s_state = ECHO_STATE_ERROR;
            return;
        }

        offset += n;
        seq = (seq + 1) & 0x7F;
    }

    s_state = ECHO_STATE_IDLE;
}

/* ---- TX access handler ----
 * TX is notify-driven (see echo_send_fragments); this callback only
 * exists because NimBLE requires a non-NULL access_cb per characteristic
 * at registration time. A direct GATT Read against TX (rather than
 * subscribing) just returns an empty value. */
static int
gatt_svr_chr_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0; /* empty read */
}

/* ---- RX write handler ---- */
static int
gatt_svr_chr_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len == 0) {
        return 0;
    }
    if (om_len > ECHO_MAX_PAYLOAD) {
        /* design §9: reject oversize writes explicitly */
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    static uint8_t rx_buf[ECHO_MAX_PAYLOAD];
    uint16_t out_len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, rx_buf, sizeof(rx_buf), &out_len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "RX %u bytes, echoing back", (unsigned)out_len);
    echo_send_fragments(rx_buf, out_len);

    return 0;
}

/* ---- Status read handler (optional, design §4.2) ---- */
static int
gatt_svr_chr_access_status(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t status = (uint8_t)s_state;
    int rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int
gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}