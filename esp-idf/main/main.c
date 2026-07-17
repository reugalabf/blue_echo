/*
 * BLE GATT Echo Server - ESP-IDF / NimBLE application entry point.
 *
 * Handles: NVS + NimBLE host init, advertising (design §7), and the GAP
 * event handler that feeds connection/subscription state into gatt_svr.c.
 *
 * NOTE ON ESP-IDF VERSIONS: NimBLE host/controller init calls have
 * changed across ESP-IDF releases (v4.4 vs v5.x differ in particular).
 * This file targets the ESP-IDF v5.x pattern, where nimble_port_init()
 * handles controller + host init together. If you're on an older IDF
 * and this fails to build, compare against
 * $IDF_PATH/examples/bluetooth/nimble/bleprph/main/main.c for your
 * installed version and adjust the init sequence accordingly.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

/*#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"*/


#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "gatt_svr.h"

static const char *TAG = "echo_main";
static uint8_t own_addr_type;

static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* Provided by the NimBLE "store" component; persists bonding info in NVS. */
void ble_store_config_init(void);

static void
echo_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof(fields));

    /* General discoverable, BR/EDR not supported (design §7). */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
    }
}

static int
gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            echo_svc_set_conn_handle(event->connect.conn_handle, true);
        } else {
            /* Failed connection attempt; resume advertising (design §10 IDLE). */
            echo_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        echo_svc_set_conn_handle(BLE_HS_CONN_HANDLE_NONE, false);
        echo_svc_set_subscribed(false);
        echo_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        echo_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; attr_handle=%d cur_notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        echo_svc_set_subscribed(event->subscribe.cur_notify != 0);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Client re-paired without deleting the old bond on our side;
         * delete our stale bond and let the pairing procedure retry. */
        {
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

static void
on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "no address available; rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return;
    }

    echo_advertise();
}

static void
on_reset(int reason)
{
    ESP_LOGE(TAG, "nimble host reset; reason=%d", reason);
}

static void
host_task(void *param)
{
    /* Blocks and processes host events until nimble_port_stop() is called. */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void
app_main(void)
{
    esp_err_t ret;
    int rc;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to init nimble port; rc=%d", ret);
        return;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Security defaults: Just Works pairing, bonding enabled, no MITM
     * protection (design §8 "baseline" tier). Raise sm_io_cap / sm_mitm
     * if you need passkey-based MITM protection for a non-lab deployment,
     * and pair that with BLE_GATT_CHR_F_WRITE_ENC on the RX characteristic
     * in gatt_svr.c. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;

    rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt_svr_init failed; rc=%d", rc);
        return;
    }

    rc = ble_svc_gap_device_name_set("EchoSrv-ESP32");
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set device name; rc=%d", rc);
    }

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
}
