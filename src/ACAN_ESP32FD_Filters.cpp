//------------------------------------------------------------------------------

#include <ACAN_ESP32FD_Filters.h>

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Filters::addMaskFilter (const uint32_t inID,
                                          const uint32_t inMask,
                                          const bool inExtended,
                                          const bool inNoClassic,
                                          const bool inNoFD) {
  const bool ok = mMaskFilterCount < SOC_TWAI_MASK_FILTER_NUM ;
  if (ok) {
    twai_mask_filter_config_t & f = mMaskFilters [mMaskFilterCount] ;
    f.id = inID ;
    f.mask = inMask ;
    f.is_ext = inExtended ? 1 : 0 ;
    f.no_classic = inNoClassic ? 1 : 0 ;
    f.no_fd = inNoFD ? 1 : 0 ;
    f.dual_filter = 0 ;
    mMaskFilterCount += 1 ;
  }
  return ok ;
}

//------------------------------------------------------------------------------
// Reproduces the bit layout used by twai_make_dual_filter() (esp_twai_onchip.h)
// without requiring the caller to have that header pulled in first.
//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Filters::addDualMaskFilter (const uint32_t inID1, const uint32_t inMask1,
                                              const uint32_t inID2, const uint32_t inMask2,
                                              const bool inExtended) {
  const bool ok = mMaskFilterCount < SOC_TWAI_MASK_FILTER_NUM ;
  if (ok) {
    twai_mask_filter_config_t & f = mMaskFilters [mMaskFilterCount] ;
    if (inExtended) {
      f.id   = (((inID1  & TWAI_EXT_ID_MASK) >> 13) << 16) | ((inID2  & TWAI_EXT_ID_MASK) >> 13) ;
      f.mask = (((inMask1 & TWAI_EXT_ID_MASK) >> 13) << 16) | ((inMask2 & TWAI_EXT_ID_MASK) >> 13) ;
    }else{
      f.id   = ((inID1  & TWAI_STD_ID_MASK) << 21) | ((inID2  & TWAI_STD_ID_MASK) << 5) ;
      f.mask = ((inMask1 & TWAI_STD_ID_MASK) << 21) | ((inMask2 & TWAI_STD_ID_MASK) << 5) ;
    }
    f.is_ext = inExtended ? 1 : 0 ;
    f.no_classic = 0 ;
    f.no_fd = 0 ;
    f.dual_filter = 1 ;
    if ((inID1 & inMask1 & inID2 & inMask2) == 0xFFFFFFFFUL) {
      f.id = 0xFFFFFFFFUL ; // recover the "reject everything" special code
      f.mask = 0xFFFFFFFFUL ;
    }
    mMaskFilterCount += 1 ;
  }
  return ok ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Filters::addRangeFilter (const uint32_t inRangeLow,
                                           const uint32_t inRangeHigh,
                                           const bool inExtended,
                                           const bool inNoClassic,
                                           const bool inNoFD) {
  #if defined (SOC_TWAI_RANGE_FILTER_NUM) && (SOC_TWAI_RANGE_FILTER_NUM > 0)
    const bool ok = ! mHasRangeFilter ;
    if (ok) {
      mRangeFilter.range_low = inRangeLow ;
      mRangeFilter.range_high = inRangeHigh ;
      mRangeFilter.is_ext = inExtended ? 1 : 0 ;
      mRangeFilter.no_classic = inNoClassic ? 1 : 0 ;
      mRangeFilter.no_fd = inNoFD ? 1 : 0 ;
      mHasRangeFilter = true ;
    }
    return ok ;
  #else
    (void) inRangeLow ; (void) inRangeHigh ; (void) inExtended ; (void) inNoClassic ; (void) inNoFD ;
    return false ; // no range filter on this controller
  #endif
}

//------------------------------------------------------------------------------
