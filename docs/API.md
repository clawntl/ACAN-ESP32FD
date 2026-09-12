# API Reference

Full reference for every public class, method, and field in ACAN_ESP32FD. For a
task-oriented introduction, see the [README](../README.md); for how to choose
bit-timing numbers, see [BIT_TIMING.md](BIT_TIMING.md); for acceptance filters,
see [FILTERS.md](FILTERS.md).

## Contents

- [`ACAN_ESP32FD`](#acan_esp32fd) — the driver object
- [`ACAN_ESP32FD_Settings`](#acan_esp32fd_settings) — bit-timing / pin / FIFO configuration
- [`ACAN_ESP32FD_Filters`](#acan_esp32fd_filters) — hardware acceptance filters
- [`CANMessage`](#canmessage) — classic CAN frame
- [`CANFDMessage`](#canfdmessage) — classic + CAN FD frame
- [`DataBitRateFactor`](#databitratefactor) — data/arbitration bit rate ratio

---

## `ACAN_ESP32FD`

One instance drives one hardware TWAI controller. Declare it as a global /
static object (its destructor calls `end()`, but on most sketches it simply
lives for the lifetime of the program).

```cpp
ACAN_ESP32FD can0 ;
```

### Starting and stopping

```cpp
uint32_t begin   (const ACAN_ESP32FD_Settings & inSettings) ;
uint32_t beginFD (const ACAN_ESP32FD_Settings & inSettings) ;
void     end     (void) ;
bool     isRunning   (void) const ;
bool     isFDEnabled (void) const ;
```

- `begin()` configures classic CAN only — works on every supported chip
  (ESP32-S3 and ESP32-C5). `inSettings` may come from any of the four
  `ACAN_ESP32FD_Settings` constructors; if it was built with a
  `DataBitRateFactor` other than `x1`, that data-phase timing is simply
  ignored by `begin()`.
- `beginFD()` configures classic **and** CAN FD framing, with independent
  arbitration/data-phase bit timing. It fails with `kControllerDoesNotSupportFD`
  on a target where `SOC_TWAI_SUPPORT_FD` is false (ESP32-S3) — this check
  happens *before* any hardware is touched, so it's safe to call
  speculatively in portable code.
- Both return `0` (`kNoError`) on success, otherwise a bit mask — see
  [Error codes](#error-codes--beginbeginfd) below. Check the return value; a
  hardware-level failure (`kNodeCreationFailed`, `kTimingConfigurationFailed`,
  `kFilterConfigurationFailed`, `kCallbackRegistrationFailed`, `kEnableFailed`)
  leaves the driver in a clean, not-running state (all partially-allocated
  hardware resources are released automatically).
- Calling `begin()`/`beginFD()` while already running returns
  `kAlreadyRunning` and does nothing — call `end()` first if you want to
  reconfigure from scratch (e.g. a different bit rate). For an in-place bit
  rate change with no teardown, see [`handle()`](#low-level-escape-hatch)
  instead.
- `end()` disables and deletes the underlying `twai_node_handle_t`, frees the
  software FIFOs and the transmit-slot pool, and resets `isRunning()` /
  `isFDEnabled()` to `false`. Safe to call even if never started.
- `isFDEnabled()` reflects whether `beginFD()` actually applied FD timing —
  it is `false` after a plain `begin()`, and also `false` after a `beginFD()`
  call whose settings had `CANFDIsEnabled() == false` (i.e.
  `DataBitRateFactor::x1`).

### Static capability check

```cpp
static bool controllerSupportsFD (void) ;
```

Compile-time check (`SOC_TWAI_SUPPORT_FD`) for whether *this build target*
has FD-capable hardware at all — independent of whether `beginFD()` was
called or succeeded. Use it to branch portable code:

```cpp
if (ACAN_ESP32FD::controllerSupportsFD ()) {
  // safe to build FD settings / call beginFD()
}
```

### Transmission

```cpp
bool tryToSendFD (const CANFDMessage & inMessage) ;
bool tryToSend   (const CANMessage & inMessage) ; // wrapper: tryToSendFD (CANFDMessage (inMessage))
```

Both are non-blocking and return immediately.

- Returns `false` without queuing anything if: the driver isn't running,
  `inMessage.isValid()` is false (see [`CANFDMessage::isValid()`](#methods)),
  or the message needs CAN FD framing / `len > 8` while `isFDEnabled()` is
  `false`.
- Otherwise, the driver first tries to hand the frame straight to a free
  hardware transmit slot (`mHardwareTxQueueDepth` slots, tracked in
  software); if none is free, it queues into the software transmit FIFO
  (`mDriverTransmitFIFOSize`). Returns `false` only if *both* are full.
- Queued frames are drained automatically: each time a hardware slot
  finishes transmitting (from the `on_tx_done` ISR callback), the next
  frame waiting in the software FIFO is immediately started from that slot.
  You never need to "pump" transmission yourself.
- `tryToSend` is a thin convenience wrapper for classic-only code — it just
  constructs a `CANFDMessage` from the `CANMessage` and calls `tryToSendFD`.

### Reception

```cpp
bool availableFD (void) const ;
bool receiveFD   (CANFDMessage & outMessage) ;

bool available (void) const ; // == availableFD()
bool receive   (CANMessage & outMessage) ;
```

Every received frame — classic or FD — is copied out of the `on_rx_done` ISR
callback into a single software receive FIFO
(`ACAN_ESP32FD_Settings::mDriverReceiveFIFOSize`); these accessors pop from
that FIFO in task context, at whatever pace your `loop()` calls them.

- `receiveFD()` returns any frame type (classic data/remote, FD with or
  without bit-rate-switch) via `CANFDMessage`. Use this if your application
  ever sends or receives FD frames, or frames longer than 8 bytes.
- `receive()` is a **classic-only convenience wrapper**. It still pops one
  frame from the (single, shared) FIFO regardless of type. If that frame
  turns out to be FD-format or longer than 8 bytes, it is discarded (already
  removed from the FIFO) and `receive()` returns `false` — the frame is
  *not* left for a subsequent `receiveFD()` call. **Do not mix `receive()`
  and `receiveFD()` on a driver that might see FD traffic** — you will
  silently lose frames one way or the other. Pick one accessor per driver
  instance based on `isFDEnabled()`.

### Callback-style reception

```cpp
bool dispatchReceivedMessage (const ACANFDCallBackRoutine inCallBack = nullptr) ;
```

ACAN2517FD-style alternative to polling `receiveFD()` yourself: pops at most
one frame from the receive FIFO and, if one was available, invokes
`inCallBack` with it (a `void (*)(const CANFDMessage &)`). Returns `true` iff
a frame was popped (even if `inCallBack` is `nullptr`). Call it repeatedly —
typically once per `loop()` iteration, or in a tight `while` to fully drain
the FIFO before returning to other work.

### Software FIFO introspection

```cpp
uint32_t driverReceiveFIFOSize        (void) const ;
uint32_t driverReceiveFIFOCount       (void) const ;
uint32_t driverReceiveFIFOPeakCount   (void) const ;
bool     driverReceiveFIFODidOverflow (void) const ;

uint32_t driverTransmitFIFOSize      (void) const ;
uint32_t driverTransmitFIFOCount     (void) const ;
uint32_t driverTransmitFIFOPeakCount (void) const ;
```

`...Size()` is the configured capacity (from `mDriverReceiveFIFOSize` /
`mDriverTransmitFIFOSize`), `...Count()` the current fill level, and
`...PeakCount()` the highest fill level ever observed — useful for sizing the
FIFO correctly: if `driverReceiveFIFOPeakCount()` is close to
`driverReceiveFIFOSize()`, your `loop()` isn't draining `receiveFD()` often
enough, or the FIFO is too small for your bus load. `driverReceiveFIFODidOverflow()`
latches `true` the first time an incoming frame had to be dropped because the
receive FIFO was full (there is no transmit-side overflow flag — `tryToSendFD`
already reports transmit-FIFO-full via its own return value).

There is no reset for `driverReceiveFIFODidOverflow()` /
`...PeakCount()` short of calling `end()` + `begin()`/`beginFD()` again.

### Status / error handling

```cpp
bool     isBusOff        (void) const ;
uint16_t txErrorCounter  (void) const ;
uint16_t rxErrorCounter  (void) const ;
uint32_t busErrorCount   (void) const ;
uint32_t hardwareTransmitQueueRemaining (void) const ;
bool     recoverFromBusOff (void) ;
```

These wrap `twai_node_get_info()` / `twai_node_recover()` and all return a
harmless zero/`false` if called before `begin()`/`beginFD()` (or after `end()`).

- `isBusOff()` — `true` when the controller has entered the bus-off state
  (TX error counter exceeded 255, per the CAN spec). While bus-off, no frames
  are transmitted or received.
- `txErrorCounter()` / `rxErrorCounter()` — the live CAN error counters (0–255
  in normal operation; TX can go higher transiently right at the bus-off
  transition).
- `busErrorCount()` — a cumulative count of bus errors (bit/stuff/CRC/form/ACK
  errors) observed since `begin()`/`beginFD()`; does not reset on bus-off.
- `hardwareTransmitQueueRemaining()` — free slots left in the driver's own
  hardware transmit queue (distinct from `driverTransmitFIFOCount()`, which is
  the *software* overflow FIFO in this library).
- `recoverFromBusOff()` — requests bus-off recovery. This is **asynchronous**:
  per the CAN spec, the controller only actually leaves bus-off after
  observing 129 consecutive recessive bits on the bus (i.e. the bus must be
  quiet/healthy). Poll `isBusOff()` afterwards — don't assume recovery is
  immediate. Returns `false` if the request itself couldn't be issued (e.g.
  driver not running).

### Low-level escape hatch

```cpp
twai_node_handle_t handle (void) const ;
```

Returns the raw ESP-IDF `twai_node_handle_t` so you can call `esp_driver_twai`
functions directly when this wrapper doesn't expose something you need — the
motivating example is `twai_node_reconfig_timing()`, which lets you change bit
rate at run time without a full `end()`/`begin()` cycle (which would also
drop the software FIFOs and any in-flight frames). Build the
`twai_timing_advanced_config_t` yourself, or reuse
`ACAN_ESP32FD_Settings::buildArbitrationTiming()` /
`buildDataTiming()` on a freshly-constructed `Settings` object with your new
desired bit rate. `nullptr` before `begin()`/`beginFD()` succeeds.

### Error codes — `begin`/`beginFD`

All are independent bits, `kNoError == 0`:

| Constant | Meaning |
|---|---|
| `kNoError` | Success |
| `kAlreadyRunning` | `begin()`/`beginFD()` called while already running; call `end()` first |
| `kSettingsError` | `inSettings.checkBitSettingConsistency() != 0` — see [BIT_TIMING.md](BIT_TIMING.md) |
| `kControllerDoesNotSupportFD` | `beginFD()` called on a classic-only target (e.g. ESP32-S3) |
| `kNodeCreationFailed` | `twai_new_node_onchip()` failed — usually bad/conflicting pins, or the controller is already claimed elsewhere |
| `kTimingConfigurationFailed` | `twai_node_reconfig_timing()` rejected the computed BRP/segment/SJW values |
| `kFilterConfigurationFailed` | A mask or range filter was rejected by the driver (should not happen if `ACAN_ESP32FD_Filters` accepted it — see [FILTERS.md](FILTERS.md)) |
| `kCallbackRegistrationFailed` | `twai_node_register_event_callbacks()` failed |
| `kEnableFailed` | `twai_node_enable()` failed |
| `kOutOfMemory` | Heap allocation of the transmit-slot pool failed |

On any failure from `kNodeCreationFailed` onward, all hardware resources
acquired so far are released before returning — you can safely retry
`begin()`/`beginFD()` with different settings without calling `end()` first.

---

## `ACAN_ESP32FD_Settings`

Holds the desired/actual bit timing, pins, FIFO sizes, module mode, and
filters. Construct one, optionally tweak it, then check
`checkBitSettingConsistency()` and pass it to `begin()`/`beginFD()`.

### Constructors

```cpp
explicit ACAN_ESP32FD_Settings (uint32_t inDesiredBitRate,
                                 uint32_t inDesiredSamplePointPermill = 800,
                                 uint32_t inTolerancePPM = 1000) ;

ACAN_ESP32FD_Settings (uint32_t inDesiredArbitrationBitRate,
                        DataBitRateFactor inDataBitRateFactor,
                        uint32_t inTolerancePPM = 1000) ;

ACAN_ESP32FD_Settings (uint32_t inDesiredArbitrationBitRate,
                        uint32_t inDesiredArbitrationSamplePointPermill,
                        DataBitRateFactor inDataBitRateFactor,
                        uint32_t inDesiredDataSamplePointPermill,
                        uint32_t inTolerancePPM = 1000) ;
```

The first is classic-CAN-only (single phase). The other two are for CAN FD:
the arbitration bit rate is separate from the data-phase bit rate, which is
given as a multiple (`DataBitRateFactor`) of the arbitration rate. All sample
points are in **permill** (1/1000 of the bit time — `800` = 80.0%).
`inTolerancePPM` sets how far off (in parts-per-million) the *actual*
achievable bit rate is allowed to be from what you asked, given the fixed
input clock — see [BIT_TIMING.md](BIT_TIMING.md#tolerance) for how this
interacts with `checkBitSettingConsistency()`.

Building FD settings on a classic-only controller (e.g. ESP32-S3) does not
fail or warn — the data-phase fields are simply computed and unused; call
`begin()` there instead of `beginFD()`, or check
`ACAN_ESP32FD::controllerSupportsFD()` before deciding which to call.

### Desired values (read-only, as given to the constructor)

```cpp
const uint32_t mDesiredArbitrationBitRate ;
const DataBitRateFactor mDataBitRateFactor ;
```

### Clock source

```cpp
twai_clock_source_t mClockSource = (twai_clock_source_t) 0 ; // 0 == TWAI_CLK_SRC_DEFAULT
uint32_t mClockFrequency = 80'000'000 ; // Hz
```

Both ESP32-S3 (APB clock) and ESP32-C5 (`PLL_F80M`, the default source) clock
their TWAI peripheral at 80 MHz out of reset, hence the default. If you pick a
non-default `mClockSource` (e.g. `TWAI_CLK_SRC_XTAL` on ESP32-C5), set
`mClockFrequency` to match **before** relying on any of the computed fields
below — easiest is to set both before constructing the `Settings` object, or
to reconstruct it after changing them, since the bit-timing calculator runs
in the constructor.

### Bit timing fields (public, hand-editable)

```cpp
// Arbitration phase (always used, classic or FD)
uint32_t mArbitrationBitRatePrescaler ;    // BRP
uint32_t mArbitrationPropagationSegment ;  // PROP_SEG (FD controllers only; else always 0)
uint32_t mArbitrationPhaseSegment1 ;       // PHASE_SEG1 / TSEG1 / PH1
uint32_t mArbitrationPhaseSegment2 ;       // PHASE_SEG2 / TSEG2 / PH2
uint32_t mArbitrationSJW ;                 // Synchronization Jump Width

// Data phase (only meaningful if CANFDIsEnabled())
uint32_t mDataBitRatePrescaler ;
uint32_t mDataPropagationSegment ;
uint32_t mDataPhaseSegment1 ;
uint32_t mDataPhaseSegment2 ;
uint32_t mDataSJW ;

bool mBitSettingOk ; // overall pass/fail, set by the constructor's calculator
```

The constructor's calculator fills all ten timing fields (and
`mBitSettingOk`) for you from the desired bit rate(s)/sample point(s)/clock.
Every field is public and may be hand-edited afterwards for full manual
control — exactly like `ACANFD_STM32_Settings` / `ACAN2517FDSettings`. If you
hand-edit them, re-run `checkBitSettingConsistency()` yourself; it is not
re-run automatically. See [BIT_TIMING.md](BIT_TIMING.md) for what each field
means physically and how to reason about picking values by hand.

### Module mode

```cpp
typedef enum : uint8_t { NORMAL, LOOP_BACK_NO_ACK, LOOP_BACK, LISTEN_ONLY } ModuleMode ;
ModuleMode mModuleMode = NORMAL ;
```

| Mode | Transceiver/bus needed? | Transmits? | Notes |
|---|---|---|---|
| `NORMAL` | Yes | Yes | Normal bus operation |
| `LOOP_BACK_NO_ACK` | No | Internally only | Self-test: frames loop back internally, no external ACK required — see `LoopBackDemoClassic`/`LoopBackDemoFD` |
| `LOOP_BACK` | Yes | Yes | Loops back internally *and* still requires a real ACK from the bus |
| `LISTEN_ONLY` | Yes (RX only) | Never — not even ACK/error frames | Pure bus monitor/sniffer |

### Pins

```cpp
gpio_num_t mTxPin = GPIO_NUM_NC ;
gpio_num_t mRxPin = GPIO_NUM_NC ;
gpio_num_t mQuantaClockOutPin = GPIO_NUM_NC ; // optional: outputs the internal Tq clock, for scope debugging
gpio_num_t mBusOffIndicatorPin = GPIO_NUM_NC ; // optional: driven low while bus-off
```

`GPIO_NUM_NC` (-1) means "unset". `begin()`/`beginFD()` reports
`kSettingsError` (via `checkBitSettingConsistency()`'s `kMissingTxPin` /
`kMissingRxPin`) if `mTxPin`/`mRxPin` are left unset.

### FIFOs and hardware queue

```cpp
uint16_t mDriverReceiveFIFOSize = 32 ;
uint16_t mDriverTransmitFIFOSize = 16 ;
uint32_t mHardwareTxQueueDepth = 8 ;
int8_t   mHardwareRetransmitLimit = -1 ; // -1 = retry forever, 0 = single-shot, N = retry N times
```

`mHardwareTxQueueDepth` is how many frames the driver tracks "in flight"
concurrently in hardware slots (each costs a small heap allocation — see the
ESP-IDF TWAI docs: roughly 4 bytes/frame plus a fixed per-node overhead);
anything beyond that queues in the software `mDriverTransmitFIFOSize` FIFO.
Size the receive FIFO (`mDriverReceiveFIFOSize`) for how long your `loop()`
might go between calls to `receive()`/`receiveFD()`/`dispatchReceivedMessage()`
relative to your incoming frame rate — watch `driverReceiveFIFOPeakCount()`
and `driverReceiveFIFODidOverflow()` at run time to tell if it's big enough.

### Timestamping and interrupt priority

```cpp
uint32_t mTimestampResolutionHz = 0 ; // 0 = disabled
int mInterruptPriority = 0 ;          // [0:3], higher = more urgent, 0 = default
```

Hardware RX timestamping; the default ACAN-style `receive()`/`receiveFD()`
accessors don't expose the timestamp value themselves — consult
`ACAN_ESP32FD.cpp` / the ESP-IDF `twai_frame_header_t` if you need it.

### Filters

```cpp
ACAN_ESP32FD_Filters mFilters ;
```

Populate before calling `begin()`/`beginFD()`; empty means "receive every
frame". See [FILTERS.md](FILTERS.md).

### Accessors

```cpp
bool CANFDIsEnabled (void) const ; // mDataBitRateFactor != DataBitRateFactor::x1

uint32_t actualArbitrationBitRate (void) const ;
bool     exactArbitrationBitRate (void) const ;
uint32_t ppmFromWishedBitRate (void) const ; // ppm error on the arbitration bit rate
float    arbitrationSamplePointFromBitStart (void) const ; // %, e.g. 80.0

uint32_t actualDataBitRate (void) const ;
bool     exactDataBitRate (void) const ;
float    dataSamplePointFromBitStart (void) const ; // %, e.g. 80.0
```

Always call these (or at least check `mBitSettingOk` /
`checkBitSettingConsistency()`) after construction, and again after any
manual edits, before trusting the settings enough to call
`begin()`/`beginFD()` — the *actual* programmed bit rate can differ slightly
from what you asked for, depending on the clock frequency and tolerance.

### Consistency check

```cpp
uint32_t checkBitSettingConsistency (void) const ; // 0 == OK
```

Returns a bit mask built from constants such as
`kArbitrationBitRatePrescalerIsZero`, `kArbitrationPhaseSegment1TooLarge`,
`kArbitrationSJWGreaterThanPhaseSegment2`, `kDataPropagationSegmentTooLarge`,
`kBitRateNotAchievedWithinTolerance`, `kMissingTxPin`, `kMissingRxPin`, and
their `kData...` equivalents for the data phase (full list in
[`ACAN_ESP32FD_Settings.h`](../src/ACAN_ESP32FD_Settings.h)). See
[BIT_TIMING.md](BIT_TIMING.md#reading-checkbitsettingconsistency) for how to
interpret each flag.

### Internals exposed for advanced use

```cpp
void buildArbitrationTiming (twai_timing_advanced_config_t & outTiming) const ;
void buildDataTiming        (twai_timing_advanced_config_t & outTiming) const ;

static bool computeSegments (uint32_t inClockFrequency,
                              uint32_t inBitRate,
                              uint32_t inSamplePointPermill,
                              bool inFDCapableController,
                              bool inDataPhase,
                              uint32_t & outBRP,
                              uint32_t & outPropSeg,
                              uint32_t & outPhaseSegment1,
                              uint32_t & outPhaseSegment2,
                              uint32_t & outSJW,
                              uint32_t & outActualBitRate) ;
```

`buildArbitrationTiming`/`buildDataTiming` translate the public fields into
the ESP-IDF `twai_timing_advanced_config_t` struct; used internally by
`begin()`/`beginFD()`, exposed publicly so you can call
`twai_node_reconfig_timing()` yourself at run time (see
[`ACAN_ESP32FD::handle()`](#low-level-escape-hatch)). `computeSegments()` is
the calculator itself, exposed as a static utility if you want to experiment
with sample points interactively (e.g. from a Serial menu) without
constructing a whole new `Settings` object each time.

---

## `ACAN_ESP32FD_Filters`

See [FILTERS.md](FILTERS.md) for a full guide with worked examples; quick
signature reference:

```cpp
bool addMaskFilter (uint32_t inID, uint32_t inMask, bool inExtended,
                    bool inNoClassic = false, bool inNoFD = false) ;

bool addDualMaskFilter (uint32_t inID1, uint32_t inMask1,
                        uint32_t inID2, uint32_t inMask2, bool inExtended) ;

bool addRangeFilter (uint32_t inRangeLow, uint32_t inRangeHigh, bool inExtended,
                     bool inNoClassic = false, bool inNoFD = false) ;

uint32_t maskFilterCount (void) const ;
bool     hasRangeFilter  (void) const ;
```

All three `add...` methods return `false` if the hardware budget is
exhausted (or, for `addRangeFilter`, if a range filter was already
configured) — check the return value at setup time; a dropped filter silently
falls back to matching nothing narrower, which usually means "receives more
than intended," not less.

---

## `CANMessage`

Classic-CAN-only frame, shared verbatim across the whole ACAN family
(`acan`, `acan2515`, `acan2517`, `acan2517FD`, `acanfd-stm32`).

```cpp
class CANMessage {
  public: uint32_t id  = 0 ;     // identifier
  public: bool     ext = false ; // false = standard (11-bit), true = extended (29-bit)
  public: bool     rtr = false ; // false = data frame, true = remote frame
  public: uint8_t  idx = 0 ;     // used internally by the driver
  public: uint8_t  len = 0 ;     // data length, 0..8
  public: union {
    uint64_t data64 ; int64_t data_s64 ;
    uint32_t data32 [2] ; int32_t data_s32 [2] ;
    uint16_t data16 [4] ; int16_t data_s16 [4] ;
    float    dataFloat [2] ;
    int8_t   data_s8 [8] ; uint8_t data [8] ;
  } ;
} ;
```

The union lets you read/write the payload as raw bytes or as wider
little/big-endian-dependent integers/floats — "Caution: subject to
endianness" in the source comments means: do this only when you control both
ends of the link and know the MCU's endianness, otherwise stick to `data[]`
byte-by-byte with your own explicit packing.

## `CANFDMessage`

Superset of `CANMessage` that also represents CAN FD frames (up to 64 bytes,
with or without bit-rate switch). Used by every `...FD` method in this
library; `tryToSend`/`receive` construct/consume one under the hood even for
classic-only code.

```cpp
class CANFDMessage {
  public: CANFDMessage (void) ;
  public: CANFDMessage (const CANMessage & inMessage) ; // implicit upcast

  public: typedef enum : uint8_t {
    CAN_REMOTE, CAN_DATA, CANFD_NO_BIT_RATE_SWITCH, CANFD_WITH_BIT_RATE_SWITCH
  } Type ;

  public: uint32_t id ;
  public: bool     ext ;
  public: Type     type ;
  public: uint8_t  idx ; // used internally by the driver
  public: uint8_t  len ; // 0..64
  public: union { /* data64[8], data_s64[8], data32[16], data_s32[16],
                     dataFloat[16], data16[32], data_s16[32],
                     data_s8[64], data[64] */ } ;
} ;
```

### `Type`

| Value | Meaning |
|---|---|
| `CAN_REMOTE` | Classic remote frame |
| `CAN_DATA` | Classic data frame |
| `CANFD_NO_BIT_RATE_SWITCH` | CAN FD framing, data phase at the *same* rate as arbitration |
| `CANFD_WITH_BIT_RATE_SWITCH` | CAN FD framing, data phase switches to the faster data-phase bit rate (`DataBitRateFactor`) |

The `CANFDMessage(const CANMessage &)` constructor maps `rtr` to
`CAN_REMOTE`/`CAN_DATA`; there's no implicit conversion the other way (use
`ACAN_ESP32FD::receive()`, which does the equivalent downcast and reports
failure via its `bool` return if the frame doesn't fit).

### Methods

```cpp
void pad (void) ;       // zero-pads len up to the next valid CAN FD length
bool isValid (void) const ;
```

- `isValid()` — for `CAN_REMOTE`/`CAN_DATA`, valid iff `len <= 8`. For
  `CANFD_NO_BIT_RATE_SWITCH`/`CANFD_WITH_BIT_RATE_SWITCH`, valid iff `len` is
  one of `0..8, 12, 16, 20, 24, 32, 48, 64` (the fixed set of CAN FD DLC
  lengths) — `tryToSendFD` refuses to send an invalid message, so call
  `pad()` first if you built a payload with an in-between length.
- `pad()` rounds `len` up to the next valid CAN FD length (e.g. 10 → 12) and
  zero-fills the new bytes. No-op if `len` is already valid or `≤ 8`.

## `DataBitRateFactor`

```cpp
enum class DataBitRateFactor : uint8_t {
  x1 = 1, x2, x3, x4, x5, x6, x7, x8, x9, x10
} ;
```

Ratio of the CAN FD data-phase bit rate to the arbitration bit rate.
`x1` means "no bit rate switch" (data phase = arbitration phase) and is also
what a classic-CAN-only `ACAN_ESP32FD_Settings` constructor sets internally —
`CANFDIsEnabled()` is defined as `mDataBitRateFactor != DataBitRateFactor::x1`.
`x4` in the README's FD example means a 4× faster data phase than
arbitration (e.g. 1 Mbit/s arbitration → 4 Mbit/s data).
