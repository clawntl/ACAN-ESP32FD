# Acceptance Filters

Hardware acceptance filters let the CAN controller silently drop frames you
don't care about *before* they ever reach your software receive FIFO — no
CPU time spent copying/queuing frames you'd just discard in `loop()`. This
doc covers `ACAN_ESP32FD_Filters` in depth; for the bare method signatures
see [API.md](API.md#acan_esp32fd_filters).

## Hardware budget by chip

| Chip | Mask filters | Range filters | Notes |
|---|:---:|:---:|---|
| ESP32-S3 | 1 | — | The single mask filter can be split into two independent 16-bit "dual" filters via `addDualMaskFilter` |
| ESP32-C5 | 3 | 1 | Each of the 3 mask filters can independently be split into a dual filter too |
| ESP32-S31 | 3 | 1 | Same TWAI-FD IP block as C5 — identical filter budget |

This budget is fixed by the SoC (`SOC_TWAI_MASK_FILTER_NUM` in
`soc_caps.h`) — `ACAN_ESP32FD_Filters` reads it at compile time, so
`addMaskFilter`/`addDualMaskFilter` simply return `false` once it's
exhausted, and `addRangeFilter` returns `false` on ESP32-S3 (no range filter
hardware at all) or if one has already been configured on ESP32-C5/ESP32-S31
(only one).

**Leave `mFilters` empty to receive every frame** — this is the default and
requires no filter hardware at all.

**Always check the return value** of `addMaskFilter`/`addDualMaskFilter`/
`addRangeFilter` at setup time. A dropped filter add doesn't narrow what you
receive at all — with no filter configured, the node falls back to "receive
everything," which usually means you'll see *more* traffic than intended,
not less, and can mask a bug (a message you thought was filtered out shows
up anyway).

## Mask filters

```cpp
bool addMaskFilter (uint32_t inID, uint32_t inMask, bool inExtended,
                    bool inNoClassic = false, bool inNoFD = false) ;
```

A mask filter matches an incoming identifier against `inID` wherever `inMask`
has a `1` bit; `0` bits in `inMask` are "don't care." This is the same
id/mask convention used across the whole ACAN family and mirrors classic CAN
controller acceptance filters generally.

```cpp
// Accept standard IDs 0x100-0x1FF (8 don't-care low bits):
settings.mFilters.addMaskFilter (0x100, 0x700, false) ;
```

Why `0x700` matches `0x100-0x1FF`: a standard identifier is 11 bits
(`0x000`-`0x7FF`). `0x700` = `111 0000 0000` in binary — the top 3 bits are
"must match," the bottom 8 are "don't care." `0x100` = `001 0000 0000`, so
any ID with top-3-bits `001` — i.e. `0x100` through `0x1FF` — matches.

```cpp
// Accept exactly one extended (29-bit) ID:
settings.mFilters.addMaskFilter (0x1ABCDEF0, 0x1FFFFFFF, true) ;
```

An all-`1`s mask (matching the full ID width for the chosen `inExtended`)
means "exact match only."

`inNoClassic` / `inNoFD` (ESP32-C5/ESP32-S31 only; ignored elsewhere) let you further
restrict a filter to reject classic-format or FD-format frames respectively,
independent of the ID match — e.g. `addMaskFilter (0x100, 0x700, false,
false, true)` accepts IDs `0x100-0x1FF` but only in classic framing, never
CAN FD framing.

## Dual (split) mask filters

```cpp
bool addDualMaskFilter (uint32_t inID1, uint32_t inMask1,
                        uint32_t inID2, uint32_t inMask2, bool inExtended) ;
```

Splits **one** hardware mask filter slot into **two** independent 16-bit
sub-filters (this is what `twai_make_dual_filter()` does under the hood, and
is available on every TWAI controller, including ESP32-S3's single filter).
Use this when you need to accept two disjoint, narrow ID ranges but only
have one mask-filter slot to spare.

```cpp
// One hardware filter slot, two independent 16-bit matches:
settings.mFilters.addDualMaskFilter (
  0x10, 0x7F0,   // sub-filter 1: standard IDs 0x10-0x1F
  0x20, 0x7F0,   // sub-filter 2: standard IDs 0x20-0x2F
  false
) ;
```

**Caveat for extended (29-bit) IDs**: because each sub-filter is only 16
bits wide, `inID1`/`inMask1`/`inID2`/`inMask2` only look at the *upper* 16
bits of a 29-bit identifier when `inExtended` is `true` — the lower 13 bits
are always "don't care" in dual mode. If you need a precise match on a full
29-bit ID, use a plain (non-dual) `addMaskFilter` instead, which uses the
full ID width.

## Range filter (ESP32-C5 / ESP32-S31 only)

```cpp
bool addRangeFilter (uint32_t inRangeLow, uint32_t inRangeHigh, bool inExtended,
                     bool inNoClassic = false, bool inNoFD = false) ;
```

Matches any ID with `inRangeLow <= id <= inRangeHigh` — a true numeric range,
not a power-of-two-aligned mask. Useful when your ID allocation doesn't fall
on a clean mask boundary (mask filters can only express ranges whose size is
a power of two and whose start is aligned to that size; a range filter has
no such restriction).

```cpp
// Accept any ID from 0x200 to 0x2FF (same result as a mask filter here,
// but works for ranges a mask can't express, e.g. 0x205-0x250):
settings.mFilters.addRangeFilter (0x200, 0x2FF, false) ;
```

Only one range filter exists per controller; a second `addRangeFilter` call
returns `false`. Not available at all on ESP32-S3
(`hasRangeFilter()`/`rangeFilter()` reflect this — see
[API.md](API.md#acan_esp32fd_filters)).

## Combining filters

Mask filters, dual filters, and the range filter can all be configured
simultaneously (subject to the per-chip budget above) — an incoming frame is
accepted if it matches **any** configured filter (logical OR across filter
slots), which mirrors how CAN controller acceptance filtering works in
general.

```cpp
ACAN_ESP32FD_Settings settings (500UL * 1000UL) ;
settings.mTxPin = GPIO_NUM_4 ;
settings.mRxPin = GPIO_NUM_5 ;

// ESP32-C5 / ESP32-S31: use all three mask-filter slots plus the range filter.
settings.mFilters.addMaskFilter     (0x100, 0x700, false) ;         // 0x100-0x1FF
settings.mFilters.addDualMaskFilter (0x10, 0x7F0, 0x20, 0x7F0, false) ; // 0x10-0x1F and 0x20-0x2F
settings.mFilters.addRangeFilter    (0x300, 0x3FF, false) ;         // 0x300-0x3FF

const uint32_t errorCode = can0.begin (settings) ;
```

Frames matching none of the above are dropped in hardware and never appear
via `receive()`/`receiveFD()`.

## Practical guidance

- **Start with no filters** while bringing up a new board/bus — see
  everything, confirm traffic looks right, *then* add filters to cut CPU
  load once you know exactly which IDs you need.
- **Prefer a plain mask filter** over a dual filter unless you're actually
  filter-slot-constrained — the 16-bit truncation on extended IDs in dual
  mode is an easy footgun if you forget it's there.
- **ESP32-S3 has only one filter slot total.** If you need more than two
  disjoint narrow ranges (via `addDualMaskFilter`) on S3, you cannot express
  that in hardware — filter more broadly in hardware and do the final
  narrowing in software after `receive()`.
- Filters are applied once, at `begin()`/`beginFD()` time, from whatever is
  in `settings.mFilters` at that moment — populate `mFilters` **before**
  calling `begin()`/`beginFD()`; there is no way to add or change filters
  on an already-running driver without `end()` + `begin()`/`beginFD()` again.
