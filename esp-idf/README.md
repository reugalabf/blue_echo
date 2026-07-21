# BLE GATT Echo Server (ESP-IDF / NimBLE)

Implements the profile from the design proposal: an Echo Service with
RX (write), TX (notify), and an optional Status (read) characteristic.

## Build & Flash

```bash
. $IDF_PATH/export.sh          # or the equivalent for your shell
idf.py set-target esp32        # or esp32c3 / esp32s3 / etc.
idf.py menuconfig              # confirm Bluetooth + NimBLE are enabled
idf.py build
idf.py -p <PORT> flash monitor
```

`sdkconfig.defaults` pre-enables Bluetooth and NimBLE in peripheral-only
mode, but Kconfig symbol names shift a bit between ESP-IDF versions —
if `idf.py build` complains about an unrecognized option, open
`idf.py menuconfig` and find the equivalent under
`Component config -> Bluetooth`.

**Tested target pattern:** ESP-IDF v5.x. If you're on v4.4, the NimBLE
host/controller init sequence in `main.c` differs slightly — compare
against `$IDF_PATH/examples/bluetooth/nimble/bleprph/main/main.c` in
your installed IDF and adjust `app_main()` accordingly.

## Testing

- **nRF Connect for Mobile** (iOS/Android):
- scan for `EchoSrv-ESP32`
- connect,
- Open Nordic UART Service
- Enable notifications (click/touch on multiple-download-arrows icon) on the TX characteristic,
- Click/touch RX Characteristic (upload arrow icon) and  write  new value, then the value will be displayed on the TX characteristic.


## Where design decisions live in code

| Design section | Code location |
|---|---|
| §4 GATT profile | `main/gatt_svr.c` — UUIDs and `gatt_svr_svcs[]` |
| §5 MTU / fragmentation | `main/gatt_svr.c` — `echo_send_fragments()` |
| §7 Advertising params | `main/main.c` — `echo_advertise()` |
| §8 Security | `main/main.c` — `ble_hs_cfg.sm_*` fields; `main/gatt_svr.c` — commented `BLE_GATT_CHR_F_WRITE_ENC` flag |
| §9 Error handling | `main/gatt_svr.c` — return codes in `gatt_svr_chr_access_rx()` |
| §10 State machine | `main/gatt_svr.c` — `echo_state_t` / `s_state` |

- Swap `BLE_GATT_CHR_F_NOTIFY` for `BLE_GATT_CHR_F_INDICATE` on TX if
  you need guaranteed delivery over lower latency.
