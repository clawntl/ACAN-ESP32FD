# Troubleshooting / FAQ

Common problems and how to diagnose them. See also
[API.md](API.md) for exact method behavior and
[BIT_TIMING.md](BIT_TIMING.md) for anything sample-point/error-counter
related.

## `begin()`/`beginFD()` returns a non-zero error code

The return value is a bit mask — check it against the constants in
`ACAN_ESP32FD` (full table in
[API.md](API.md#error-codes--beginbeginfd)). Print it in hex and compare:

```cpp
const uint32_t errorCode = can0.beginFD (settings) ;
if (errorCode != 0) {
  Serial.printf ("CAN begin error: 0x%08lX\n", (unsigned long) errorCode) ;
}
```

Quick triage by bit:

- **`kSettingsError`** — call `settings.checkBitSettingConsistency()`
  yourself and inspect *which* flags are set (see
  [BIT_TIMING.md](BIT_TIMING.md#reading-checkbitsettingconsistency)). This
  is almost always either an unreachable bit rate/sample-point combination,
  or `mTxPin`/`mRxPin` left unset.
- **`kControllerDoesNotSupportFD`** — you called `beginFD()` on a
  classic-only target (ESP32-S3). Call `begin()` instead, or gate the call
  with `ACAN_ESP32FD::controllerSupportsFD()`.
- **`kNodeCreationFailed`** — usually a pin problem: the GPIO is invalid for
  this function on this chip, already claimed by another peripheral, or
  another `ACAN_ESP32FD` instance / TWAI controller is already using it.
  Double check `mTxPin`/`mRxPin` against your board's actual wiring and that
  you're not accidentally constructing two driver instances pointed at the
  same controller.
- **`kTimingConfigurationFailed`** — the computed (or hand-edited) BRP/segment
  values were rejected by the driver at the hardware level. If you hand-edited
  timing fields, re-run `checkBitSettingConsistency()` — it won't happen
  automatically after a manual edit.
- **`kFilterConfigurationFailed`** — a filter in `settings.mFilters` was
  rejected. This shouldn't happen if `addMaskFilter`/`addDualMaskFilter`/
  `addRangeFilter` all returned `true` at setup time — if you see this,
  double-check you checked those return values (see
  [FILTERS.md](FILTERS.md)).
- **`kCallbackRegistrationFailed`** / **`kEnableFailed`** — low-level
  ESP-IDF driver failures; rare. Check the Serial log for any ESP-IDF-level
  error output alongside it (this library doesn't currently surface the
  underlying `esp_err_t`).
- **`kOutOfMemory`** — the transmit-slot pool (`mHardwareTxQueueDepth`
  entries) couldn't be heap-allocated. Reduce `mHardwareTxQueueDepth`, or
  free up heap elsewhere before calling `begin()`/`beginFD()`.
- **`kAlreadyRunning`** — you called `begin()`/`beginFD()` on an instance
  that's already running. Call `end()` first if you want to reconfigure.

## Frames aren't being received

1. **Check `mModuleMode`.** If it's still `LISTEN_ONLY` or you meant to test
   without a bus, use `LOOP_BACK_NO_ACK` (see the `LoopBackDemoClassic` /
   `LoopBackDemoFD` examples) — it needs no transceiver or second node.
2. **Check filters.** An empty `mFilters` receives everything; if you
   configured any filter, confirm the ID/mask math with a decimal/hex
   calculator, or temporarily clear filters to confirm frames arrive at all
   before debugging the filter itself (see [FILTERS.md](FILTERS.md)).
3. **Check you're calling the matching accessor.** If `beginFD()` is active
   and any FD-format or >8-byte frames are on the bus, use `receiveFD()`,
   not `receive()` — see [the mixing gotcha](#mixing-receive-and-receivefd-drops-frames)
   below.
4. **Check `driverReceiveFIFODidOverflow()`.** If `true`, your `loop()`
   isn't draining the receive FIFO fast enough for the incoming frame rate —
   increase `mDriverReceiveFIFOSize`, or call `receive()`/`receiveFD()`/
   `dispatchReceivedMessage()` more often (e.g. drain in a `while` loop
   instead of once per `loop()` iteration).
5. **Check wiring/transceiver power** for `NORMAL`/`LOOP_BACK` mode — this
   library has only had limited testing on real silicon so far (see
   the README's Notes/limitations), so a wiring issue and a driver bug can
   look identical from software. If loopback mode works but real-bus mode
   doesn't, suspect wiring/transceiver/bus first.

## Mixing `receive()` and `receiveFD()` drops frames

There is exactly **one** software receive FIFO per `ACAN_ESP32FD` instance,
shared by both accessors. `receive()` is a classic-only convenience: if the
frame at the head of the FIFO turns out to be FD-format or longer than 8
bytes, `receive()` still removes it from the FIFO but returns `false` — the
frame is gone, not deferred for a later `receiveFD()` call.

**Fix:** on any driver instance where FD or >8-byte frames might appear
(i.e. any instance where `isFDEnabled()` could be `true`, or that shares a
bus with FD-capable senders), use `receiveFD()` exclusively. Reserve
`receive()` for drivers you know will only ever see classic frames.

## `tryToSend`/`tryToSendFD` returns `false`

In order of likelihood:

1. **Driver not running** — `begin()`/`beginFD()` wasn't called, failed, or
   `end()` was called since.
2. **Invalid message** — `inMessage.isValid()` failed. For CAN FD frames,
   `len` must be one of the fixed DLC lengths (`0..8, 12, 16, 20, 24, 32, 48,
   64`); call `frame.pad()` before sending if your payload length doesn't
   land on one of these.
3. **FD support not enabled** — the message needs FD framing or has
   `len > 8`, but the driver was started with `begin()` (not `beginFD()`),
   or `beginFD()` was called with settings where `CANFDIsEnabled()` was
   `false`, or on a controller where `controllerSupportsFD()` is `false`
   (ESP32-S3).
4. **Both transmit paths full** — every hardware transmit slot
   (`mHardwareTxQueueDepth`) is busy *and* the software transmit FIFO
   (`mDriverTransmitFIFOSize`) is full. Check
   `hardwareTransmitQueueRemaining()` and `driverTransmitFIFOCount()` /
   `driverTransmitFIFOSize()` — if this happens routinely, either you're
   generating traffic faster than the bus can drain, or the bus itself isn't
   draining (see bus-off below).

## Bus-off / rising error counters

CAN nodes count errors and drop to bus-off (no more TX/RX at all) once the
transmit error counter exceeds 255, per the CAN spec — this is the
controller protecting the bus from a persistently malfunctioning node.

1. Poll `txErrorCounter()` / `rxErrorCounter()` / `busErrorCount()` and log
   them periodically while reproducing the issue — a counter climbing
   steadily (vs. a one-off blip) points at a systemic problem: wrong bit
   rate/sample point relative to other nodes, a wiring/termination fault, or
   a missing/misbehaving transceiver.
2. Check `isBusOff()`. If `true`, call `recoverFromBusOff()` — this is
   **asynchronous**: the CAN spec requires 129 consecutive recessive bits on
   a healthy bus before recovery actually completes, so poll `isBusOff()`
   afterward rather than assuming it recovered immediately. If it never
   clears, the bus itself is still faulty (short, missing termination, no
   other node driving recessive, etc.) — recovery can't fix a bus that's
   still broken.
3. If error counters climb only under load or only over a longer/real bus
   (vs. a short bench loopback), suspect the **sample point**, not the
   nominal bit rate — see
   [BIT_TIMING.md](BIT_TIMING.md#verifying-on-real-hardware).
4. Confirm every node on the bus agrees on the *nominal* bit rate. A sample
   point mismatch between nodes is often intermittent and traffic-dependent;
   a bit rate mismatch is usually immediate and total.

## `beginFD()` "succeeds" but frames don't switch to the faster data rate

Check `can0.isFDEnabled()` after `beginFD()` returns `kNoError` — it's
`false` (silently) if the `ACAN_ESP32FD_Settings` you passed had
`CANFDIsEnabled() == false` (i.e. it was built with `DataBitRateFactor::x1`,
including via the classic-only constructor). Use a CAN-FD constructor of
`ACAN_ESP32FD_Settings` with a `DataBitRateFactor` other than `x1` if you
want an actual bit-rate switch — see
[API.md](API.md#acan_esp32fd_settings).

Also confirm you're sending `CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH`
frames specifically — `CANFD_NO_BIT_RATE_SWITCH` frames use FD *framing*
(64-byte payloads) but intentionally stay at the arbitration rate throughout.

## ESP32-S3 + CAN FD

ESP32-S3's TWAI controller is classic-CAN-only in hardware — this is a chip
limitation, not a driver bug. `ACAN_ESP32FD::controllerSupportsFD()` returns
`false` at compile time for S3 builds, and `beginFD()` returns
`kControllerDoesNotSupportFD` immediately without touching hardware. Per the
README, sending FD-format frames on an S3 causes bus errors — always call
`begin()` (not `beginFD()`) on S3, and write portable code by branching on
`controllerSupportsFD()` if the same sketch source targets both chips.

## Build errors mentioning `esp_twai.h`, `twai_node_...`, or `SOC_TWAI_SUPPORT_FD`

This library wraps the ESP-IDF v5.5+ `esp_driver_twai` ("twai_node") API,
which is newer than the classic `driver/twai.h` API most ESP32 CAN examples
online use. Confirm:

- You're building with **arduino-esp32 core 3.3.9 or newer** (ESP-IDF
  5.5.4+) — earlier cores don't have `twai_node_...` at all.
- For ESP32-C5 specifically, your arduino-esp32 core version actually
  supports the C5 target (support landed around the same core versions as
  the `twai_node` API).
- You haven't got another CAN library (e.g. one built on `driver/twai.h`)
  also included in the same sketch — the two APIs can coexist in ESP-IDF but
  mixing them for the *same* controller in one sketch is not something this
  library is designed to support.

## This library has only had limited real-hardware testing — what does that mean for me?

Per the README, the bit-timing math and register-range logic were derived
from ESP-IDF v5.5.5 source, and the library has only had basic testing on
real S3/C5 boards so far. Treat it more like bleeding edge than a drop-in
production-ready driver:

- Run the loopback examples (`LoopBackDemoClassic`, `LoopBackDemoFD`) first
  on your actual board — they need no transceiver or second board and are
  the fastest way to catch a real integration problem early.
- On first bring-up of any new bit rate, verify `actualArbitrationBitRate()`
  / `arbitrationSamplePointFromBitStart()` (and data-phase equivalents)
  against a scope/analyzer if you have one available, per
  [BIT_TIMING.md](BIT_TIMING.md#verifying-on-real-hardware).
- If you find a discrepancy between this library's behavior and the
  ESP-IDF/hardware reference, it's more likely a bug in this wrapper than in
  the underlying ESP-IDF driver — please note exactly which field/return
  value looked wrong so it can be traced back to the relevant source file.
