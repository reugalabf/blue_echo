# Design Proposal: BLE GATT Echo Server

## 1. Overview

This document proposes the design of a Bluetooth Low Energy (BLE) peripheral device that implements a **GATT-based echo server**. The device advertises a custom service, accepts data written by a connected central, and echoes that data back via notifications. This is a common pattern for validating a BLE stack, testing throughput/latency, or bootstrapping a custom serial-over-BLE link (similar in spirit to Nordic's UART Service or Apple's ANCS-style RX/TX pattern).

**Assumptions** (stated explicitly since the request didn't specify a platform):
- The echo server runs on an embedded MCU acting as a BLE **Peripheral / GATT Server** (e.g., Nordic nRF52/nRF53 with Zephyr or nRF Connect SDK, or Espressif ESP32 with ESP-IDF/NimBLE).
- A phone, PC, or another MCU acts as the **Central / GATT Client** for testing.
- Single active connection at a time (extendable to multiple, see §11).

If any of these assumptions don't match your target platform, the profile design below still applies — only the implementation stack (§10) changes.

---

## 2. Goals and Non-Goals

**Goals**
- Reliable byte-accurate echo of arbitrary-length payloads.
- Correct handling of BLE ATT_MTU limits and fragmentation.
- Reasonable, configurable security posture (not wide open by default).
- Clean, testable state machine with defined error behavior.

**Non-goals**
- Not designed for high-throughput streaming (that would favor a single bulk-notify characteristic and PHY 2M/Coded tuning — noted as a future enhancement).
- Not a general-purpose UART bridge (though the same profile can be trivially repurposed as one).

---

## 3. Architecture Overview

```
 ┌─────────────────────┐          BLE Link           ┌──────────────────────┐
 │   Central (Client)   │ <──────────────────────────> │  Peripheral (Server)  │
 │  phone / PC / MCU     │                              │   Echo Server MCU     │
 └─────────────────────┘                              └──────────────────────┘
        writes to RX  ────────────────────────────────────>
        notified on TX <────────────────────────────────────
```

The server's only job: whatever bytes arrive on the **RX characteristic**, copy them (optionally transformed) and push them out on the **TX characteristic** as one or more notifications.

---

## 4. GATT Profile Design

### 4.1 Service

| Item | Value |
|---|---|
| Name | Echo Service |
| UUID | Custom 128-bit, e.g. `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` (reuse Nordic UART base, or generate your own) |
| Type | Primary Service |

Using a 128-bit custom UUID avoids collisions with SIG-adopted 16-bit services and clearly signals "custom protocol" to any client that inspects it.

### 4.2 Characteristics

| Characteristic | UUID (example) | Properties | Max Length | Purpose |
|---|---|---|---|---|
| RX (Echo In) | `...0002...` | Write, Write Without Response | ATT_MTU − 3 (see §5) | Central sends data to be echoed |
| TX (Echo Out) | `...0003...` | Notify | ATT_MTU − 3 | Server pushes echoed data back |
| CCCD (on TX) | `0x2902` (standard) | Read/Write | 2 bytes | Central enables/disables notifications |
| Status (optional) | `...0004...` | Read | 1 byte | Exposes server state (idle/echoing/error) — useful for debugging, not required for core echo |

**Design rationale — why two characteristics instead of one bidirectional one:**
BLE GATT characteristics are directionally asymmetric by design (a client writes, a server notifies). Splitting RX/TX mirrors this cleanly, matches the pattern most BLE developers already recognize from UART-style services, and lets each direction have independent flow control (Write Without Response for RX to avoid blocking, Notify for TX to avoid requiring client ACKs on every packet).

**Both Write and Write Without Response on RX**, because:
- *Write Without Response* is preferred for throughput (no ATT-layer handshake per packet).
- *Write* (with response) is kept available for clients that want per-packet delivery confirmation, or for platforms whose BLE stack doesn't expose Write-Without-Response cleanly (some mobile OS BLE APIs are inconsistent here).

---

## 5. MTU and Fragmentation Handling

The default ATT_MTU is 23 bytes (20 usable payload bytes after the 3-byte ATT header). Real-world central/peripheral pairs typically negotiate up to 247–517 bytes.

**Proposed handling:**
1. On connection, the server should support (but not require) an **MTU Exchange Request** from the client. If the client doesn't initiate one, the server operates at the 20-byte default.
2. Payloads longer than the negotiated `(ATT_MTU − 3)` must be **application-layer fragmented**:
   - Simplest approach: the server echoes back in a stream of notifications sized to the current MTU, in order, with no additional framing — acceptable if the client only cares about byte-accurate reassembly and reads until a quiet period.
   - More robust approach: add a **1-byte header** to each RX/TX packet: `[seq_no | more_fragments_flag]`, so the client can detect drops/reordering (BLE notifications are not guaranteed in the same sense as Indications, though in-order delivery on a single connection is standard for real controllers).
3. **Indications vs Notifications tradeoff:** Notifications are used here for TX because they avoid a per-packet application ACK, keeping the echo path fast. If guaranteed delivery matters more than latency, swap `Notify` for `Indicate` (adds a link-layer ACK per packet, ~roughly doubling round-trip overhead but guaranteeing delivery before the next indication is sent).

**Recommendation:** implement the 1-byte sequence/more-flag header from the start — it costs one byte per packet and removes an entire class of ambiguity bugs during testing.

---

## 6. Data Flow (Sequence)

```
Central                                  Peripheral (Echo Server)
  |--- Scan / Connect --------------------->|
  |<-- Connection established --------------|
  |--- (optional) MTU Exchange ------------>|
  |<-- MTU Response -------------------------|
  |--- Discover Services -------------------->|
  |--- Discover Characteristics ------------->|
  |--- Write CCCD (enable notify on TX) ---->|
  |--- Write RX characteristic (payload) --->|
  |                                          | [server copies RX buffer -> TX buffer]
  |<-- Notify TX characteristic (echo) ------|
  |    (repeat Notify if fragmented) --------|
```

---

## 7. Connection & Advertising Parameters

| Parameter | Recommended Value | Rationale |
|---|---|---|
| Advertising interval | 100–250 ms | Balance discoverability vs. power |
| Advertised name | e.g. `"EchoSrv-XXXX"` (last 4 hex of MAC) | Distinguishable across multiple test units |
| Connection interval | 15–30 ms | Low latency for echo round-trips |
| Slave latency | 0 | Echo is latency-sensitive; don't let the peripheral skip connection events |
| Supervision timeout | ≥ 4× connection interval, per spec minimum | Standard reliability margin |

---

## 8. Security Considerations

Echo servers are frequently left "wide open" during bring-up, which is fine for a lab bench but worth designing against from the start:

| Level | Behavior | When to use |
|---|---|---|
| No security (Just Works, no encryption) | Anyone can connect and write | Bench testing only |
| LE Secure Connections + encryption, no MITM | Encrypted link, no passkey | Default recommended baseline |
| LE Secure Connections + MITM protection (passkey/numeric comparison) | Requires pairing confirmation | If the device will be shipped or used outside a controlled lab |

**Proposal:** Set the RX characteristic's write permission to require **encryption** (`BT_GATT_PERM_WRITE_ENCRYPT` in Zephyr terms, or equivalent). This forces pairing before any data can be written, while keeping the pairing method itself configurable (Just Works for lab use, passkey for anything more exposed). Reject writes on an unencrypted link with `ATT_ERR_INSUFFICIENT_ENCRYPTION` rather than silently dropping them, so clients get a clear signal.

---

## 9. Error Handling & Edge Cases

| Case | Server Behavior |
|---|---|
| Write larger than negotiated MTU allows | Reject with `ATT_ERR_INVALID_ATTRIBUTE_LEN`; central should have fragmented already at the ATT layer for long writes, or the app layer should chunk |
| Notify attempted while CCCD not enabled | Skip notify; optionally log/increment an error counter exposed via the optional Status characteristic |
| Central disconnects mid-fragment-sequence | Discard partial buffer; reset echo state machine to idle on next connection |
| Malformed sequence header (if used) | Drop packet, do not echo; optionally notify an error code on Status characteristic |
| Write while a previous echo is still in-flight | Either queue (bounded, e.g. 2–3 buffers) or reject with `ATT_ERR_PREPARE_QUEUE_FULL`-style busy response — queueing is preferred for a better test experience |

---

## 10. State Machine

```
        ┌────────┐   connect    ┌────────────┐   RX write    ┌───────────┐
        │  IDLE  │ ───────────> │  CONNECTED │ ────────────> │  ECHOING  │
        └────────┘              └────────────┘               └─────┬─────┘
             ^                        ^                             │
             │      disconnect        │        TX notify complete   │
             └────────────────────────┴─────────────────────────────┘
```

- **IDLE**: advertising, no connection.
- **CONNECTED**: link up, services discoverable, no active echo transaction.
- **ECHOING**: RX buffer received, TX notifications in flight; returns to CONNECTED once the final fragment's notification completes (or is acked, if using Indicate).

---

---

*This proposal favors the two-characteristic (RX/Write, TX/Notify) pattern because it's the most widely recognized BLE serial-style profile shape, keeps flow control simple, and is directly extensible into a full UART-over-BLE bridge if the project grows beyond a pure echo test.*
