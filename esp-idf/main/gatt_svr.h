#ifndef GATT_SVR_H
#define GATT_SVR_H

#include <stdint.h>
#include <stdbool.h>

/* Registers the Echo Service (RX/TX/Status characteristics) with the
 * NimBLE GATT server. Call once during app_main(), after
 * nimble_port_init(). Returns 0 on success. */
int gatt_svr_init(void);

/* Called from main.c's GAP event handler on BLE_GAP_EVENT_CONNECT /
 * BLE_GAP_EVENT_DISCONNECT to track the active connection handle. */
void echo_svc_set_conn_handle(uint16_t conn_handle, bool connected);

/* Called from main.c's GAP event handler on BLE_GAP_EVENT_SUBSCRIBE to
 * track whether the central has enabled notifications on TX. */
void echo_svc_set_subscribed(bool subscribed);

#endif /* GATT_SVR_H */
