# ACAN_ESP32FD

An [ACAN-style](https://github.com/pierremolinaro) CAN / CAN-FD driver for **ESP32-S3**, **ESP32-C5**, and **ESP32-S31**, built directly on top of the new ESP-IDF v5.5+ `esp_driver_twai` component (`esp_twai.h` / `esp_twai_onchip.h` — the "twai_node" API), rather than on the older, timing-inflexible `driver/twai.h` legacy API.

Like `ACANFD_STM32` / `ACAN2517FD`, you specify a **desired bit rate and a desired sample point** (not a fixed table of "standard" bit rates), and a bit-timing calculator computes an explicit `BRP / PROP_SEG / PHASE_SEG1 / PHASE_SEG2 / SJW` quintet for you — every one of those fields is public afterwards and can be hand-tuned if you want full manual control. `CANMessage` and `CANFDMessage` are the same generic classes used across the whole ACAN family, so code and message-handling logic ports over easily.

## Hardware support

| Chip      | Controllers | Classic CAN | CAN FD | Mask filters | Range filters |
|-----------|:-----------:|:------------:|:------:|:-------------:|:-------------:|
| ESP32-S3  | 1           | ✅           | ❌ (bus errors on FD frames) | 1 (splittable into 2×16-bit) | — |
| ESP32-C5  | 2           | ✅           | ✅ (independent arbitration/data sample points) | 3 | 1 |
| ESP32-S31 | 2           | ✅           | ✅ (independent arbitration/data sample points) | 3 | 1 |

ESP32-S31 uses the same TWAI-FD hardware IP block as the C5 (near-identical HAL/register layer), so it gets the same capabilities. Note ESP32-S31 support currently requires a beta/alpha ESP-IDF 6.x-based Arduino core (e.g. the [pioarduino](https://github.com/pioarduino) `prep_IDF6` platform branch) — treat it as bleeding edge even by this library's own standards.

One `ACAN_ESP32FD` object drives one hardware controller. ESP32-C5/ESP32-S31 have two controllers each, so instantiate two objects (with different pins) if you need both.

**Requires arduino-esp32 core 3.3.9 or newer** (ESP-IDF 5.5.4+) — the `twai_node` API this library wraps was only introduced in ESP-IDF 5.5. ESP32-C5 support in arduino-esp32 itself only became available around the same core versions. On IDF 6.x-based cores (e.g. current ESP32-S31 support), the underlying FD-capability macro was renamed from `SOC_TWAI_SUPPORT_FD` to `SOC_TWAI_FD_SUPPORTED`; this library bridges both names automatically, so FD detection stays correct either way.

## Quick start — classic CAN (works on S3, C5, and S31)

```cpp
#include <ACAN_ESP32FD.h>

ACAN_ESP32FD can0 ;

void setup () {
  Serial.begin (115200) ;
  ACAN_ESP32FD_Settings settings (500UL * 1000UL) ; // 500 kbit/s, default 80.0 % sample point
  settings.mTxPin = GPIO_NUM_4 ;
  settings.mRxPin = GPIO_NUM_5 ;
  const uint32_t errorCode = can0.begin (settings) ;
  if (errorCode != 0) {
    Serial.printf ("CAN begin error: 0x%08lX\n", (unsigned long) errorCode) ;
  }else{
    Serial.printf ("Actual bit rate: %lu bit/s, sample point %.1f %%\n",
                   (unsigned long) settings.actualArbitrationBitRate (),
                   settings.arbitrationSamplePointFromBitStart ()) ;
  }
}

void loop () {
  CANMessage frame ;
  if (can0.receive (frame)) {
    // ... handle it ...
  }
  static uint32_t last = 0 ;
  if (millis () - last >= 1000) {
    last = millis () ;
    CANMessage frame ;
    frame.id = 0x123 ;
    frame.len = 2 ;
    frame.data [0] = 0xDE ;
    frame.data [1] = 0xAD ;
    can0.tryToSend (frame) ;
  }
}
```

## Quick start — CAN FD with an explicit sample point (ESP32-C5 / ESP32-S31)

```cpp
#include <ACAN_ESP32FD.h>

ACAN_ESP32FD can0 ;

void setup () {
  Serial.begin (115200) ;
  // Arbitration: 1 Mbit/s @ 80.0 % sample point. Data: x4 -> 4 Mbit/s @ 75.0 %.
  ACAN_ESP32FD_Settings settings (1000UL * 1000UL, 800, DataBitRateFactor::x4, 750) ;
  settings.mTxPin = GPIO_NUM_4 ;
  settings.mRxPin = GPIO_NUM_5 ;

  // Full manual override is always possible after construction, exactly like
  // ACANFD_STM32_Settings, e.g. to force a specific SJW:
  // settings.mArbitrationSJW = 4 ;

  const uint32_t errorCode = can0.beginFD (settings) ;
  Serial.printf ("beginFD: 0x%08lX\n", (unsigned long) errorCode) ;
  Serial.printf ("Arbitration: %lu bit/s (%.1f %% s.p.), Data: %lu bit/s (%.1f %% s.p.)\n",
                 (unsigned long) settings.actualArbitrationBitRate (), settings.arbitrationSamplePointFromBitStart (),
                 (unsigned long) settings.actualDataBitRate (),        settings.dataSamplePointFromBitStart ()) ;
}

void loop () {
  CANFDMessage frame ;
  if (can0.receiveFD (frame)) {
    // ...
  }
}
```

## Settings object

`ACAN_ESP32FD_Settings` has four constructors:

```cpp
ACAN_ESP32FD_Settings (bitRate, tolerancePPM = 1000) ;                                       // classic, default 80.0% s.p.
ACAN_ESP32FD_Settings (bitRate, samplePointPermill, tolerancePPM = 1000) ;                    // classic, explicit s.p.
ACAN_ESP32FD_Settings (arbitrationBitRate, DataBitRateFactor, tolerancePPM = 1000) ;          // FD, default 80.0% s.p. both phases
ACAN_ESP32FD_Settings (arbitrationBitRate, arbSamplePointPermill,
                       DataBitRateFactor, dataSamplePointPermill, tolerancePPM = 1000) ;      // FD, explicit s.p. both phases
```

Sample points are in **permill** (1/1000 of the bit time), so 800 = 80.0 %. After construction, check:

- `mBitSettingOk` — overall pass/fail
- `checkBitSettingConsistency ()` — returns 0 if OK, else a bit mask of `kArbitration...`/`kData...` flags telling you exactly what's out of range
- `actualArbitrationBitRate () / actualDataBitRate ()` — what will actually be programmed
- `arbitrationSamplePointFromBitStart () / dataSamplePointFromBitStart ()` — the actual resulting sample point, in %
- `ppmFromWishedBitRate ()` — how far off the arbitration bit rate is from what you asked for

All of `mArbitrationBitRatePrescaler`, `mArbitrationPropagationSegment`, `mArbitrationPhaseSegment1/2`, `mArbitrationSJW` (and the `mData...` equivalents) are public and can be hand-edited after construction for full manual control — the constructor's calculator is just a convenience.

`mClockFrequency` defaults to 80 MHz, the default `TWAI_CLK_SRC_DEFAULT` on both chips. If you pick a different `mClockSource` (e.g. `TWAI_CLK_SRC_XTAL` on ESP32-C5), set `mClockFrequency` to match before relying on the computed fields.

### Filters

```cpp
settings.mFilters.addMaskFilter (0x100, 0x700, false) ;      // standard IDs 0x100-0x1FF
settings.mFilters.addRangeFilter (0x200, 0x2FF, false) ;      // ESP32-C5 only (1 range filter available)
settings.mFilters.addDualMaskFilter (0x10, 0x7F0, 0x20, 0x7F0, false) ; // split one filter into two 16-bit ones
```

Leave `mFilters` empty to receive every frame (the default).

### Module mode

`settings.mModuleMode` is one of `NORMAL`, `LOOP_BACK_NO_ACK` (single-node bench testing, no transceiver needed), `LOOP_BACK` (still needs a real bus/ACK), `LISTEN_ONLY`.

## Driver object (`ACAN_ESP32FD`)

- `begin(settings)` / `beginFD(settings)` — return `0` on success, else a bit mask (`ACAN_ESP32FD::kSettingsError`, `kControllerDoesNotSupportFD`, `kNodeCreationFailed`, `kTimingConfigurationFailed`, `kFilterConfigurationFailed`, `kCallbackRegistrationFailed`, `kEnableFailed`, `kOutOfMemory`, `kAlreadyRunning`)
- `tryToSendFD (CANFDMessage)` / `tryToSend (CANMessage)` — non-blocking; `false` if invalid, if it needs FD support you didn't enable, or if both the hardware transmit slots and the software transmit FIFO (`mDriverTransmitFIFOSize`) are full
- `receiveFD (CANFDMessage&)` / `receive (CANMessage&)` / `availableFD()` / `available()` — pop from the software receive FIFO (`mDriverReceiveFIFOSize`); frames are copied out of the hardware/ISR path into this FIFO automatically
- `dispatchReceivedMessage (callback)` — ACAN2517FD-style callback dispatch instead of polling
- `driverReceiveFIFOCount/Size/PeakCount`, `driverTransmitFIFOCount/Size/PeakCount`, `hardwareTransmitQueueRemaining()`
- `isBusOff()`, `txErrorCounter()`, `rxErrorCounter()`, `busErrorCount()`, `recoverFromBusOff()`
- `handle()` — the raw `twai_node_handle_t`, in case you want to call ESP-IDF functions (e.g. `twai_node_reconfig_timing()`) directly, e.g. to change bit rate at run time without a full `end()`/`begin()` cycle
- `ACAN_ESP32FD::controllerSupportsFD()` — static, compile-time (`SOC_TWAI_SUPPORT_FD`) check

`tryToSendFD`/`receiveFD` work with both classic and FD-format messages (selected by `CANFDMessage::type`); `tryToSend`/`receive` are thin convenience wrappers for classic-only code and simply construct/consume a `CANFDMessage` under the hood — mixing both accessors while `beginFD()` is active and FD frames are flowing is your responsibility (see the doc comments in `ACAN_ESP32FD.h`).

## Examples

- `LoopBackDemoClassic` — classic CAN, internal loopback, no transceiver/second board needed, works on S3, C5, and S31
- `LoopBackDemoFD` — CAN FD with independent arbitration/data sample points, internal loopback, ESP32-C5 / ESP32-S31 only. **Known issue:** `LOOP_BACK_NO_ACK` self-test mode combined with FD framing currently drives the controller into a repeated bus-off/recovery loop on both C5 and S31 (see [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md#can-fd-self-testloopback-loop_back_no_ack--beginfd-goes-bus-off-immediately)) — this looks like an upstream `esp_driver_twai` limitation with FD self-test, not a bug in this wrapper. Real-bus FD communication in `NORMAL` mode (verified on ESP32-S31 with a CAN analyzer) is unaffected.
- `TwoBoards_Send` / `TwoBoards_Receive` — classic CAN across two real boards/transceivers

## Further documentation

- [docs/API.md](docs/API.md) — full reference for every public class, method, and field
- [docs/BIT_TIMING.md](docs/BIT_TIMING.md) — CAN bit-timing concepts, sample points, and how to read `checkBitSettingConsistency()`
- [docs/FILTERS.md](docs/FILTERS.md) — mask/dual/range filter deep-dive with worked examples
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — `begin()`/`beginFD()` error codes, bus-off recovery, and common gotchas

## Notes / limitations

- Bit-timing math and hardware register ranges were derived from the ESP-IDF v5.5.5 sources (`hal/twai_ll.h`, `hal/twaifd_ll.h`, `soc/soc_caps.h`, `soc/twaifd_struct.h`) rather than from datasheet copy — double-check `actualArbitrationBitRate()` / `arbitrationSamplePointFromBitStart()` against a scope/analyzer on first bring-up of a new bit rate, as usual with any CAN bit-timing calculator.
- This library has had **limited testing on real hardware** (basic tests with S3 and C5, plus ESP32-S31 real-bus CAN FD confirmed working with a CAN analyzer) — treat it more like bleeding edge rather than a drop-in production ready.
- `SOC_TWAI_SUPPORT_FD` is resolved at **compile time** from the target chip, so a single sketch source can target any supported board, but `beginFD()` will only actually configure FD hardware when built for an FD-capable target (ESP32-C5, ESP32-S31, and other future FD-capable chips); on ESP32-S3 it returns `kControllerDoesNotSupportFD` without touching hardware. On IDF 6.x-based cores this library transparently falls back to the renamed `SOC_TWAI_FD_SUPPORTED` macro (see above) so this detection stays correct without any user-visible change.
- **CAN FD internal self-test loopback (`LOOP_BACK_NO_ACK` + `beginFD()`) currently goes bus-off** on both ESP32-C5 and ESP32-S31 — see [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md#can-fd-self-testloopback-loop_back_no_ack--beginfd-goes-bus-off-immediately). Classic self-test loopback and real-bus FD communication both work correctly; only the FD+self-test combination is affected.

## License

MIT — see [LICENSE](LICENSE). The overall API design (a `Settings` object computing explicit bit-timing from a desired bit rate/sample point) is deliberately modeled after Pierre Molinaro's [`ACANFD_STM32`](https://github.com/pierremolinaro/acanfd-stm32) / [`ACAN2517FD`](https://github.com/pierremolinaro/acan2517FD), and `ACAN_ESP32FD_CANMessage.h` / `ACAN_ESP32FD_CANFDMessage.h` are reused verbatim from that same ACAN family (also MIT-licensed) — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
