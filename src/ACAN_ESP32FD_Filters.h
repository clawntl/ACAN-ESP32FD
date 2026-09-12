//------------------------------------------------------------------------------
// ACAN_ESP32FD_Filters
//
// Thin, chip-aware wrapper around the hardware acceptance filters of the new
// ESP-IDF TWAI / TWAI-FD driver (esp_twai.h / esp_twai_onchip.h).
//
// Hardware budget (fixed by the SoC, see soc_caps.h):
//   - ESP32-S3            : 1  mask filter  (can be split into 2 x 16-bit "dual" filters)
//   - ESP32-C5            : 3  mask filters + 1 range filter
//
// If you never add a filter, the node is configured to receive everything
// (this mirrors the "no filter -> receive all" default of the other ACAN
// libraries). Filters are configured once, right before the node is enabled
// in begin()/beginFD(), so add them to the ACAN_ESP32FD_Settings object
// before calling begin()/beginFD().
//------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------

#include "esp_twai_types.h"
#include "soc/soc_caps.h"

//------------------------------------------------------------------------------

#ifndef SOC_TWAI_MASK_FILTER_NUM
  #error "SOC_TWAI_MASK_FILTER_NUM is not defined for this target"
#endif

//------------------------------------------------------------------------------

class ACAN_ESP32FD_Filters {

  public: ACAN_ESP32FD_Filters (void) { }

  //············································································
  // Add a standard 32-bit (or 11-bit) mask filter.
  //   inID / inMask : '1' bits in inMask must match the corresponding inID bits,
  //                    '0' bits in inMask are "don't care".
  //   inExtended    : true for 29-bit identifiers, false for 11-bit identifiers.
  //   inNoClassic / inNoFD: optionally restrict the filter to FD-only or
  //                    classic-only frames (ESP32-C5 only, ignored elsewhere).
  // Returns false if the hardware filter budget (SOC_TWAI_MASK_FILTER_NUM) is exhausted.
  //············································································

  public: bool addMaskFilter (const uint32_t inID,
                              const uint32_t inMask,
                              const bool inExtended,
                              const bool inNoClassic = false,
                              const bool inNoFD = false) ;

  //············································································
  // Add a "dual" filter: splits one hardware mask filter into two independent
  // 16-bit sub-filters (matches twai_make_dual_filter()). Available on every
  // TWAI controller. For 29-bit IDs, only the upper 16 bits of id/mask are used.
  //············································································

  public: bool addDualMaskFilter (const uint32_t inID1, const uint32_t inMask1,
                                  const uint32_t inID2, const uint32_t inMask2,
                                  const bool inExtended) ;

  //············································································
  // Add the (single) range filter, only present on FD-capable controllers
  // (e.g. ESP32-C5). Returns false on controllers without a range filter,
  // or if it has already been configured.
  //············································································

  public: bool addRangeFilter (const uint32_t inRangeLow,
                               const uint32_t inRangeHigh,
                               const bool inExtended,
                               const bool inNoClassic = false,
                               const bool inNoFD = false) ;

  //············································································
  // Access (used internally by ACAN_ESP32FD::begin / beginFD)
  //············································································

  public: inline uint32_t maskFilterCount (void) const { return mMaskFilterCount ; }
  public: inline const twai_mask_filter_config_t & maskFilterAtIndex (const uint32_t inIndex) const {
    return mMaskFilters [inIndex] ;
  }

  public: inline bool hasRangeFilter (void) const { return mHasRangeFilter ; }
  public: inline const twai_range_filter_config_t & rangeFilter (void) const { return mRangeFilter ; }

  //············································································
  // Private properties
  //············································································

  private: twai_mask_filter_config_t mMaskFilters [SOC_TWAI_MASK_FILTER_NUM] = {} ;
  private: uint32_t mMaskFilterCount = 0 ;

  private: twai_range_filter_config_t mRangeFilter = {} ;
  private: bool mHasRangeFilter = false ;

  //············································································
  // No copy
  //············································································

  private: ACAN_ESP32FD_Filters (const ACAN_ESP32FD_Filters &) = delete ;
  private: ACAN_ESP32FD_Filters & operator = (const ACAN_ESP32FD_Filters &) = delete ;
} ;

//------------------------------------------------------------------------------
