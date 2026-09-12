# Bit Timing & CAN FD Concepts

This is the background you need to *reason about* the numbers
`ACAN_ESP32FD_Settings` computes for you — you rarely need to hand-edit them,
but understanding what they mean makes the difference between "it works" and
"it works reliably at the edge of your wiring/tolerance budget." For the
class/method signatures themselves, see [API.md](API.md#acan_esp32fd_settings).

## Why bit timing exists at all

Every node on a CAN bus samples the bus voltage at (ideally) the exact same
instant, without a shared clock line — each node derives its own bit timing
from its own oscillator and re-synchronizes on every recessive-to-dominant
edge. To make that work, a CAN "bit" is divided into a fixed number of time
slices called **time quanta (Tq)**, allocated across four segments:

```
|<--------------------------- 1 bit time --------------------------->|
| SYNC_SEG | PROP_SEG | PHASE_SEG1 |         PHASE_SEG2               |
|  (1 Tq)  |          |            |                                  |
                                    ^
                              sample point
```

- **SYNC_SEG** — always exactly 1 Tq; this is where an edge is expected.
- **PROP_SEG** — compensates for physical propagation delay (transceiver +
  wire round-trip). On this library's non-FD-capable path it's fixed at 0
  (folded into PHASE_SEG1 by the hardware); on FD-capable controllers
  (ESP32-C5) it's a separate, explicit field.
- **PHASE_SEG1** / **PHASE_SEG2** — surround the sample point and absorb
  clock drift between nodes. Every re-sync can shrink/stretch these by up to
  **SJW** quanta to pull the local bit boundary back in line with the bus.
- **Sample point** — the instant, expressed as a percentage of the bit time,
  where the bus level is latched as the bit's value. It sits at the boundary
  between PHASE_SEG1 and PHASE_SEG2 (plus the sync/prop segments before it).

**BRP** (Bit Rate Prescaler) sets how long one Tq lasts, in units of the
peripheral clock (`mClockFrequency`, 80 MHz by default on both supported
chips): `Tq = BRP / mClockFrequency`. Bit rate and sample point are both
functions of `BRP` and the four segment lengths — that's why you specify the
two things you actually care about (rate, sample point) and let a calculator
work backward to the integer segment values, rather than picking segments
directly.

## What `ACAN_ESP32FD_Settings` computes for you

Given `mClockFrequency`, a desired bit rate, and a desired sample point (in
permill), the constructor's calculator (`computeSegments()`) searches for an
integer `BRP` / `PROP_SEG` / `PHASE_SEG1` / `PHASE_SEG2` / `SJW` quintet that:

1. Produces the *actual* bit rate as close as possible to what you asked,
   within `inTolerancePPM`.
2. Lands the resulting sample point as close as possible to what you asked.
3. Stays within the hardware's register width limits for each field (these
   differ between the classic path and the FD path — see
   [Hardware limits](#hardware-limits-by-chip) below).

The result is written into the public fields
(`mArbitrationBitRatePrescaler`, `mArbitrationPropagationSegment`,
`mArbitrationPhaseSegment1/2`, `mArbitrationSJW`, and the `mData...`
equivalents for the FD data phase), and `mBitSettingOk` is set to whether it
succeeded. You can override any of these fields by hand afterwards — the
calculator is a convenience, not a gate.

## Tolerance

`inTolerancePPM` (default `1000`, i.e. 0.1%) bounds how far the *achievable*
bit rate may drift from your desired one, because `BRP` must be an integer —
not every bit rate is exactly reachable from an 80 MHz clock. If no
quintet gets within tolerance, `mBitSettingOk` is `false` and
`checkBitSettingConsistency()` reports `kBitRateNotAchievedWithinTolerance`.

After construction, always check:

```cpp
settings.actualArbitrationBitRate () ; // what will actually be programmed
settings.ppmFromWishedBitRate ()     ; // how far off, in ppm
settings.exactArbitrationBitRate ()  ; // true iff ppmFromWishedBitRate() == 0
```

and the data-phase equivalents (`actualDataBitRate()`, `exactDataBitRate()`)
if CAN FD is enabled. A few hundred ppm of error is normal and harmless; if
you need an exact rate, either loosen nothing and just accept the closest
achievable one, or pick a bit rate that divides the clock frequency evenly
(e.g. 500 kbit/s from 80 MHz gives an exact BRP).

## Reading `checkBitSettingConsistency()`

Returns `0` if everything is consistent, otherwise a bit mask. Each flag
tells you exactly which computed (or hand-edited) field is the problem:

| Flag | What it means |
|---|---|
| `kArbitrationBitRatePrescalerIsZero` | `BRP` came out as 0 — desired bit rate too high for `mClockFrequency` |
| `kArbitrationBitRatePrescalerTooLarge` | `BRP` exceeds the hardware register width — desired bit rate too low |
| `kArbitrationPhaseSegment1TooLarge` | `PHASE_SEG1` (+`PROP_SEG` on FD controllers) exceeds the hardware limit |
| `kArbitrationPhaseSegment2IsZero` / `...TooLarge` | `PHASE_SEG2` out of the valid range — usually from an extreme sample point (very low or very high %) |
| `kArbitrationSJWIsZero` / `...TooLarge` | `SJW` out of range |
| `kArbitrationSJWGreaterThanPhaseSegment2` | `SJW` cannot exceed `PHASE_SEG2` — a hardware/spec rule, not just a preference |
| `kArbitrationPropagationSegmentTooLarge` | `PROP_SEG` exceeds the hardware limit (FD controllers only) |
| `kBitRateNotAchievedWithinTolerance` | No integer quintet got the bit rate within `inTolerancePPM` |
| `kMissingTxPin` / `kMissingRxPin` | `mTxPin`/`mRxPin` left as `GPIO_NUM_NC` |

The `kData...` flags (`kDataBitRatePrescalerIsZero`, etc.) mirror the above
one-for-one for the data phase, and only matter if `CANFDIsEnabled()`.

If you hit one of the segment/prescaler flags on a *desired sample point*
you picked yourself, the fix is almost always to relax the sample point
slightly (e.g. 87.5% is a common "problem" value at some bit rates/clocks —
try 80.0% or 75.0% first) rather than to hand-tune segments.

## Sample point: what value to pick

There's no universally "correct" sample point — it's a tradeoff:

- **Higher sample point** (e.g. 87.5%) leaves more of the bit time before the
  sample for the signal to settle after propagation delay — good for
  longer/noisier buses or slower transceivers, but leaves less phase margin
  (smaller effective `PHASE_SEG2`/`SJW` room) for clock drift correction.
- **Lower sample point** (e.g. 75–80%) gives more resynchronization margin —
  good for higher bit rates or less accurate oscillators — at the cost of
  less settling time before the sample.

`ACAN_ESP32FD_Settings`'s default of 80.0% is a reasonable general-purpose
choice and matches the other ACAN libraries' defaults. Classic CAN commonly
uses 75–87.5% depending on the bus; CAN FD's much faster data phase often
pushes toward the lower end of that range because there's proportionally
less time per bit to work with.

If you're integrating with other nodes on the bus (not just other boards
running this library), match sample points with whatever tooling/stack those
nodes use — a sample-point mismatch between nodes at the same nominal bit
rate is a classic source of intermittent bus errors that only show up under
specific traffic patterns or temperature.

## CAN FD: arbitration vs. data phase

CAN FD frames start and end (arbitration, ACK) at the classic bit rate, but
switch to a faster **data phase** rate for the payload + CRC when
`CANFD_WITH_BIT_RATE_SWITCH` is used (`DataBitRateFactor` other than `x1`).
This library configures the two phases completely independently — separate
bit rate, separate sample point, separate segment/SJW fields
(`mArbitration...` vs `mData...`) — because the data phase, running faster,
typically needs a *lower* sample point and tighter tolerance to leave enough
resynchronization margin.

`CANFD_NO_BIT_RATE_SWITCH` frames use FD framing (so payloads up to 64 bytes)
but stay at the arbitration bit rate throughout — useful for testing FD
framing/DLC handling over a link that can't yet reliably run the faster data
phase.

## Hardware limits by chip

The exact maximum values for `BRP`/`PROP_SEG`/`PHASE_SEG1`/`PHASE_SEG2`/`SJW`
differ between the classic-only register layout (ESP32-S3,
`hal/twai_ll.h`) and the FD-capable register layout (ESP32-C5,
`hal/twaifd_ll.h`, `soc/twaifd_struct.h`) — notably, `PROP_SEG` is folded
into `PHASE_SEG1` on the classic path (always reported as 0) but is a
distinct field on the FD path. `computeSegments()` already knows which limits
apply (via its `inFDCapableController` parameter) and picks a valid quintet
accordingly — you only need to know this if you're interpreting a
`checkBitSettingConsistency()` failure on hand-edited fields, or reading the
source.

## Verifying on real hardware

Bit-timing math here was derived from ESP-IDF v5.5.5 source (register
definitions and the reference formulas in `hal/twai_ll.h` / `hal/twaifd_ll.h`
/ `soc/soc_caps.h`), not measured on a scope. As with any CAN bit-timing
calculator, on first bring-up of a new bit rate/sample-point combination:

1. Print `actualArbitrationBitRate()` / `arbitrationSamplePointFromBitStart()`
   (and the data-phase equivalents) and sanity-check them against what you
   expected.
2. If you have a scope or CAN analyzer, verify the actual bit time and
   sample-point location against the reported values.
3. Watch `busErrorCount()` / `txErrorCounter()` / `rxErrorCounter()` under
   real traffic for a while — a subtly wrong sample point often looks fine
   at idle and only produces errors under load or over a longer/noisier bus
   than your bench setup.

See also [TROUBLESHOOTING.md](TROUBLESHOOTING.md#bus-off--rising-error-counters)
for what to do when error counters climb.
