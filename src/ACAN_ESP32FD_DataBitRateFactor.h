//------------------------------------------------------------------------------
// DataBitRateFactor: ratio between the CAN FD data phase bit rate and the
// arbitration phase bit rate. Shared across the ACANFD family of libraries.
//------------------------------------------------------------------------------

#ifndef ACANFD_DATA_BIT_RATE_FACTOR_DEFINED
#define ACANFD_DATA_BIT_RATE_FACTOR_DEFINED

//------------------------------------------------------------------------------

#include <stdint.h>

//------------------------------------------------------------------------------

enum class DataBitRateFactor : uint8_t {
  x1 = 1,  // No bit rate switch: data phase = arbitration phase (also used for classic CAN)
  x2 = 2,
  x3 = 3,
  x4 = 4,
  x5 = 5,
  x6 = 6,
  x7 = 7,
  x8 = 8,
  x9 = 9,
  x10 = 10
} ;

//------------------------------------------------------------------------------

#endif
