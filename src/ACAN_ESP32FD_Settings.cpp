//------------------------------------------------------------------------------

#include <ACAN_ESP32FD_Settings.h>

//------------------------------------------------------------------------------
//   HARDWARE BIT DECOMPOSITION CONSTRAINTS
//
//   Classic (SJA1000-compatible) TWAI controller (e.g. ESP32-S3):
//     - BRP is even, 2 ... 16384 (register stores brp/2 - 1)
//     - No separate PROP_SEG register: PROP_SEG is always 0, everything before
//       the sample point (other than the fixed 1 Tq SYNC_SEG) is PHASE_SEG1.
//     - PHASE_SEG1 (TSEG1) : 1 ... 16
//     - PHASE_SEG2 (TSEG2) : 1 ... 8
//     - SJW                : 1 ... 4
//     (source: hal/twai_ll.h, TWAI_LL_* constants)
//
//   TWAI-FD controller (e.g. ESP32-C5), arbitration phase:
//     - BRP      : 1 ... 255      (8-bit field)
//     - PROP_SEG : 0 ... 127      (7-bit field)
//     - PHASE_SEG1 (ph1): 0 ... 63 (6-bit field)
//     - PHASE_SEG2 (ph2): 1 ... 63 (6-bit field, TWAI_LL_TSEG2_MIN = 1)
//     - SJW      : 0 ... 31       (5-bit field)
//
//   TWAI-FD controller, data phase:
//     - BRP      : 1 ... 255
//     - PROP_SEG : 0 ... 63       (6-bit field)
//     - PHASE_SEG1 (ph1_fd): 0 ... 31 (5-bit field)
//     - PHASE_SEG2 (ph2_fd): 1 ... 31 (5-bit field)
//     - SJW      : 0 ... 31       (5-bit field)
//   (source: soc/twaifd_struct.h register bit-field widths, hal/twaifd_ll.h)
//------------------------------------------------------------------------------

namespace {

  struct TimingConstraint {
    uint32_t brpMin ;
    uint32_t brpMax ;
    bool     brpMustBeEven ;
    uint32_t propSegMax ;      // 0 on classic controllers (no such field)
    uint32_t phaseSegment1Max ;
    uint32_t phaseSegment2Min ;
    uint32_t phaseSegment2Max ;
    uint32_t sjwMax ;
  } ;

  const TimingConstraint kClassicConstraint = {
    /* brpMin            */ 2,
    /* brpMax            */ 16384,
    /* brpMustBeEven     */ true,
    /* propSegMax        */ 0,
    /* phaseSegment1Max  */ 16,
    /* phaseSegment2Min  */ 1,
    /* phaseSegment2Max  */ 8,
    /* sjwMax            */ 4
  } ;

  const TimingConstraint kFDArbitrationConstraint = {
    /* brpMin            */ 1,
    /* brpMax            */ 255,
    /* brpMustBeEven     */ false,
    /* propSegMax        */ 127,
    /* phaseSegment1Max  */ 63,
    /* phaseSegment2Min  */ 1,
    /* phaseSegment2Max  */ 63,
    /* sjwMax            */ 31
  } ;

  const TimingConstraint kFDDataConstraint = {
    /* brpMin            */ 1,
    /* brpMax            */ 255,
    /* brpMustBeEven     */ false,
    /* propSegMax        */ 63,
    /* phaseSegment1Max  */ 31,
    /* phaseSegment2Min  */ 1,
    /* phaseSegment2Max  */ 31,
    /* sjwMax            */ 31
  } ;

  const TimingConstraint & constraintFor (const bool inFDCapableController, const bool inDataPhase) {
    if (!inFDCapableController) {
      return kClassicConstraint ;
    }
    return inDataPhase ? kFDDataConstraint : kFDArbitrationConstraint ;
  }

}

//------------------------------------------------------------------------------
//   Bit timing calculator
//
//   Strategy (identical in spirit to ACANFD_STM32_Settings): iterate over
//   every valid BRP, compute the resulting total Tq count for the desired
//   bit rate, and keep the candidate that gets closest to the desired bit
//   rate (ties broken in favour of the larger Tq count, which gives finer
//   control over the sample point). Once the best BRP/Tq pair is chosen, the
//   Tq budget before/after the sample point is split into
//   PROP_SEG + PHASE_SEG1 (maximizing PHASE_SEG1 first) and PHASE_SEG2.
//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Settings::computeSegments (const uint32_t inClockFrequency,
                                             const uint32_t inBitRate,
                                             const uint32_t inSamplePointPermill,
                                             const bool inFDCapableController,
                                             const bool inDataPhase,
                                             uint32_t & outBRP,
                                             uint32_t & outPropSeg,
                                             uint32_t & outPhaseSegment1,
                                             uint32_t & outPhaseSegment2,
                                             uint32_t & outSJW,
                                             uint32_t & outActualBitRate) {
  const TimingConstraint & c = constraintFor (inFDCapableController, inDataPhase) ;
  const uint32_t minTotalTq = 1 /* sync */ + 1 /* min phase1 (or prop) */ + c.phaseSegment2Min ;
  const uint32_t maxTotalTq = 1 + c.propSegMax + c.phaseSegment1Max + c.phaseSegment2Max ;

  //--- Search strategy: try every possible total Tq count, from the LARGEST
  //  (finest possible sample-point resolution) down to the smallest, and for
  //  each one keep the BRP that reproduces inClockFrequency most closely
  //  (absolute Hz error, exactly as ACANFD_STM32_Settings does). Because the
  //  outer loop visits totalTq from large to small, a later candidate only
  //  overwrites the current best on a STRICT improvement, so among several
  //  totalTq choices that are equally close to the desired bit rate, the
  //  largest (finest) one wins automatically -- this is what actually fixes
  //  "exotic" bit rates that don't divide the clock evenly, where a naive
  //  search tends to settle for a tiny, imprecise Tq count.
  //------------------------------------------------------------------------

  bool found = false ;
  uint64_t bestError = 0 ; // |inClockFrequency - brp * totalTq * inBitRate|, in Hz
  uint32_t bestBRP = c.brpMin ;
  uint32_t bestTotalTq = minTotalTq ;
  uint32_t bestActualBitRate = 0 ;

  if ((inBitRate != 0) && (inClockFrequency != 0)) {
    for (uint32_t totalTq = maxTotalTq ; totalTq >= minTotalTq ; totalTq--) {
      const uint64_t denominator = uint64_t (inBitRate) * uint64_t (totalTq) ;
      if (denominator == 0) {
        continue ;
      }
      uint32_t idealBRP = uint32_t (inClockFrequency / denominator) ; // may be 0
      for (uint32_t candidateBRP = idealBRP ; candidateBRP <= idealBRP + 1 ; candidateBRP++) {
        uint32_t brp = candidateBRP ;
        if (c.brpMustBeEven && ((brp & 1U) != 0)) {
          brp += 1 ; // round up to the nearest even value
        }
        if ((brp < c.brpMin) || (brp > c.brpMax)) {
          continue ;
        }
        const uint64_t reconstructedFrequency = uint64_t (brp) * denominator ;
        const uint64_t error =
          (reconstructedFrequency > inClockFrequency)
          ? (reconstructedFrequency - inClockFrequency)
          : (inClockFrequency - reconstructedFrequency) ;
        const uint32_t actualBitRate = uint32_t (inClockFrequency / (uint64_t (brp) * totalTq)) ;
        if ((actualBitRate > 0) && (!found || (error < bestError))) {
          found = true ;
          bestError = error ;
          bestBRP = brp ;
          bestTotalTq = totalTq ;
          bestActualBitRate = actualBitRate ;
        }
      }
      if (found && (bestError == 0) && (bestTotalTq == totalTq)) {
        break ; // exact match at this (largest remaining) totalTq: cannot do better
      }
    }
  }

  if (!found) {
    outBRP = c.brpMin ;
    outPropSeg = 0 ;
    outPhaseSegment1 = 1 ;
    outPhaseSegment2 = c.phaseSegment2Min ;
    outSJW = 1 ;
    outActualBitRate = 0 ;
    return false ;
  }

  //--- Split the Tq budget around the sample point ---------------------------
  uint32_t beforeSample = ((bestTotalTq * inSamplePointPermill) + 500) / 1000 ; // Tq from bit start (incl. sync) to sample point
  if (beforeSample < 2) {
    beforeSample = 2 ; // at least sync(1) + 1 Tq before the sample point
  }
  if (beforeSample > bestTotalTq - c.phaseSegment2Min) {
    beforeSample = bestTotalTq - c.phaseSegment2Min ;
  }
  uint32_t combinedSeg1 = beforeSample - 1 ; // PROP_SEG + PHASE_SEG1, excluding the fixed sync Tq
  const uint32_t combinedSeg1Max = c.propSegMax + c.phaseSegment1Max ;
  if (combinedSeg1 > combinedSeg1Max) {
    combinedSeg1 = combinedSeg1Max ;
  }
  uint32_t phaseSegment2 = bestTotalTq - 1 - combinedSeg1 ;
  if (phaseSegment2 > c.phaseSegment2Max) {
    const uint32_t excess = phaseSegment2 - c.phaseSegment2Max ;
    phaseSegment2 = c.phaseSegment2Max ;
    combinedSeg1 = (combinedSeg1 + excess <= combinedSeg1Max) ? (combinedSeg1 + excess) : combinedSeg1Max ;
  }
  if (phaseSegment2 < c.phaseSegment2Min) {
    phaseSegment2 = c.phaseSegment2Min ;
  }

  //--- Fill PHASE_SEG1 first, overflow (FD controllers only) into PROP_SEG ---
  uint32_t phaseSegment1 = (combinedSeg1 <= c.phaseSegment1Max) ? combinedSeg1 : c.phaseSegment1Max ;
  uint32_t propSeg = combinedSeg1 - phaseSegment1 ;
  if (propSeg > c.propSegMax) { // should not happen given the clamp above, kept as a safety net
    propSeg = c.propSegMax ;
  }
  if ((phaseSegment1 == 0) && (propSeg == 0) && (combinedSeg1Max > 0)) {
    phaseSegment1 = 1 ; // never emit a zero-length segment before the sample point
  }

  outBRP = bestBRP ;
  outPropSeg = propSeg ;
  outPhaseSegment1 = phaseSegment1 ;
  outPhaseSegment2 = phaseSegment2 ;
  outSJW = (phaseSegment2 < c.sjwMax) ? phaseSegment2 : c.sjwMax ; // full resync range by default, like ACANFD_STM32
  if (outSJW == 0) {
    outSJW = 1 ;
  }
  outActualBitRate = bestActualBitRate ;
  return true ;
}

//------------------------------------------------------------------------------
//   Constructors
//------------------------------------------------------------------------------

ACAN_ESP32FD_Settings::ACAN_ESP32FD_Settings (const uint32_t inDesiredBitRate,
                                              const uint32_t inDesiredSamplePointPermill,
                                              const uint32_t inTolerancePPM) :
ACAN_ESP32FD_Settings (inDesiredBitRate, inDesiredSamplePointPermill, DataBitRateFactor::x1, inDesiredSamplePointPermill, inTolerancePPM) {
}

//------------------------------------------------------------------------------

ACAN_ESP32FD_Settings::ACAN_ESP32FD_Settings (const uint32_t inDesiredArbitrationBitRate,
                                              const DataBitRateFactor inDataBitRateFactor,
                                              const uint32_t inTolerancePPM) :
ACAN_ESP32FD_Settings (inDesiredArbitrationBitRate, 800, inDataBitRateFactor, 800, inTolerancePPM) {
}

//------------------------------------------------------------------------------

ACAN_ESP32FD_Settings::ACAN_ESP32FD_Settings (const uint32_t inDesiredArbitrationBitRate,
                                              const uint32_t inDesiredArbitrationSamplePointPermill,
                                              const DataBitRateFactor inDataBitRateFactor,
                                              const uint32_t inDesiredDataSamplePointPermill,
                                              const uint32_t inTolerancePPM) :
mDesiredArbitrationBitRate (inDesiredArbitrationBitRate),
mDataBitRateFactor (inDataBitRateFactor) {
  #if SOC_TWAI_SUPPORT_FD
    const bool fdCapable = true ;
  #else
    const bool fdCapable = false ;
  #endif

  uint32_t actualArbitration = 0 ;
  const bool arbitrationOk = computeSegments (
    mClockFrequency, mDesiredArbitrationBitRate, inDesiredArbitrationSamplePointPermill,
    fdCapable, false,
    mArbitrationBitRatePrescaler, mArbitrationPropagationSegment,
    mArbitrationPhaseSegment1, mArbitrationPhaseSegment2, mArbitrationSJW,
    actualArbitration
  ) ;

  bool dataOk = true ;
  if (fdCapable && CANFDIsEnabled ()) {
    const uint32_t desiredDataBitRate = mDesiredArbitrationBitRate * uint32_t (mDataBitRateFactor) ;
    uint32_t actualData = 0 ;
    dataOk = computeSegments (
      mClockFrequency, desiredDataBitRate, inDesiredDataSamplePointPermill,
      fdCapable, true,
      mDataBitRatePrescaler, mDataPropagationSegment,
      mDataPhaseSegment1, mDataPhaseSegment2, mDataSJW,
      actualData
    ) ;
    if (dataOk) {
      const uint64_t diff = (actualData > desiredDataBitRate) ? (actualData - desiredDataBitRate) : (desiredDataBitRate - actualData) ;
      dataOk = (diff * 1000ULL * 1000ULL) <= (uint64_t (desiredDataBitRate) * inTolerancePPM) ;
    }
  }else{
    // Classic-only controller, or FD not requested: mirror arbitration settings
    // into the data-phase fields so buildDataTiming()/actualDataBitRate() stay
    // meaningful even though they won't be used by begin() (only beginFD() on
    // an FD-capable chip uses the data-phase fields).
    mDataBitRatePrescaler = mArbitrationBitRatePrescaler ;
    mDataPropagationSegment = mArbitrationPropagationSegment ;
    mDataPhaseSegment1 = mArbitrationPhaseSegment1 ;
    mDataPhaseSegment2 = mArbitrationPhaseSegment2 ;
    mDataSJW = mArbitrationSJW ;
  }

  mBitSettingOk = arbitrationOk && dataOk && (ppmFromWishedBitRate () <= inTolerancePPM) ;
}

//------------------------------------------------------------------------------
//   Accessors
//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD_Settings::actualArbitrationBitRate (void) const {
  const uint32_t totalTq = 1 + mArbitrationPropagationSegment + mArbitrationPhaseSegment1 + mArbitrationPhaseSegment2 ;
  return (mArbitrationBitRatePrescaler == 0) ? 0 : (mClockFrequency / (mArbitrationBitRatePrescaler * totalTq)) ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Settings::exactArbitrationBitRate (void) const {
  const uint32_t totalTq = 1 + mArbitrationPropagationSegment + mArbitrationPhaseSegment1 + mArbitrationPhaseSegment2 ;
  return mClockFrequency == (mArbitrationBitRatePrescaler * mDesiredArbitrationBitRate * totalTq) ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD_Settings::ppmFromWishedBitRate (void) const {
  const uint32_t actual = actualArbitrationBitRate () ;
  if ((actual == 0) || (mDesiredArbitrationBitRate == 0)) {
    return 1000UL * 1000UL ; // 100 % error: unachievable
  }
  const uint64_t diff = (actual > mDesiredArbitrationBitRate) ? (actual - mDesiredArbitrationBitRate) : (mDesiredArbitrationBitRate - actual) ;
  return uint32_t ((diff * 1000ULL * 1000ULL) / mDesiredArbitrationBitRate) ;
}

//------------------------------------------------------------------------------

float ACAN_ESP32FD_Settings::arbitrationSamplePointFromBitStart (void) const {
  const uint32_t totalTq = 1 + mArbitrationPropagationSegment + mArbitrationPhaseSegment1 + mArbitrationPhaseSegment2 ;
  const uint32_t samplePoint = 1 + mArbitrationPropagationSegment + mArbitrationPhaseSegment1 ;
  return (totalTq == 0) ? 0.0f : ((float (samplePoint) * 100.0f) / float (totalTq)) ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD_Settings::actualDataBitRate (void) const {
  const uint32_t totalTq = 1 + mDataPropagationSegment + mDataPhaseSegment1 + mDataPhaseSegment2 ;
  return (mDataBitRatePrescaler == 0) ? 0 : (mClockFrequency / (mDataBitRatePrescaler * totalTq)) ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_Settings::exactDataBitRate (void) const {
  const uint32_t totalTq = 1 + mDataPropagationSegment + mDataPhaseSegment1 + mDataPhaseSegment2 ;
  const uint32_t desiredDataBitRate = mDesiredArbitrationBitRate * uint32_t (mDataBitRateFactor) ;
  return mClockFrequency == (mDataBitRatePrescaler * desiredDataBitRate * totalTq) ;
}

//------------------------------------------------------------------------------

float ACAN_ESP32FD_Settings::dataSamplePointFromBitStart (void) const {
  const uint32_t totalTq = 1 + mDataPropagationSegment + mDataPhaseSegment1 + mDataPhaseSegment2 ;
  const uint32_t samplePoint = 1 + mDataPropagationSegment + mDataPhaseSegment1 ;
  return (totalTq == 0) ? 0.0f : ((float (samplePoint) * 100.0f) / float (totalTq)) ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD_Settings::checkBitSettingConsistency (void) const {
  #if SOC_TWAI_SUPPORT_FD
    const bool fdCapable = true ;
  #else
    const bool fdCapable = false ;
  #endif
  const TimingConstraint & ac = constraintFor (fdCapable, false) ;
  const TimingConstraint & dc = constraintFor (fdCapable, true) ;

  uint32_t errorCode = 0 ;

  if (mTxPin == GPIO_NUM_NC) {
    errorCode |= kMissingTxPin ;
  }
  if (mRxPin == GPIO_NUM_NC) {
    errorCode |= kMissingRxPin ;
  }

  if (mArbitrationBitRatePrescaler == 0) {
    errorCode |= kArbitrationBitRatePrescalerIsZero ;
  }else if (mArbitrationBitRatePrescaler > ac.brpMax) {
    errorCode |= kArbitrationBitRatePrescalerTooLarge ;
  }
  if (mArbitrationPropagationSegment > ac.propSegMax) {
    errorCode |= kArbitrationPropagationSegmentTooLarge ;
  }
  if (mArbitrationPhaseSegment1 > ac.phaseSegment1Max) {
    errorCode |= kArbitrationPhaseSegment1TooLarge ;
  }
  if (mArbitrationPhaseSegment2 < ac.phaseSegment2Min) {
    errorCode |= kArbitrationPhaseSegment2IsZero ;
  }else if (mArbitrationPhaseSegment2 > ac.phaseSegment2Max) {
    errorCode |= kArbitrationPhaseSegment2TooLarge ;
  }
  if (mArbitrationSJW == 0) {
    errorCode |= kArbitrationSJWIsZero ;
  }else if (mArbitrationSJW > ac.sjwMax) {
    errorCode |= kArbitrationSJWTooLarge ;
  }
  if (mArbitrationSJW > mArbitrationPhaseSegment2) {
    errorCode |= kArbitrationSJWGreaterThanPhaseSegment2 ;
  }

  if (fdCapable && CANFDIsEnabled ()) {
    if (mDataBitRatePrescaler == 0) {
      errorCode |= kDataBitRatePrescalerIsZero ;
    }else if (mDataBitRatePrescaler > dc.brpMax) {
      errorCode |= kDataBitRatePrescalerTooLarge ;
    }
    if (mDataPropagationSegment > dc.propSegMax) {
      errorCode |= kDataPropagationSegmentTooLarge ;
    }
    if (mDataPhaseSegment1 > dc.phaseSegment1Max) {
      errorCode |= kDataPhaseSegment1TooLarge ;
    }
    if (mDataPhaseSegment2 < dc.phaseSegment2Min) {
      errorCode |= kDataPhaseSegment2IsZero ;
    }else if (mDataPhaseSegment2 > dc.phaseSegment2Max) {
      errorCode |= kDataPhaseSegment2TooLarge ;
    }
    if (mDataSJW == 0) {
      errorCode |= kDataSJWIsZero ;
    }else if (mDataSJW > dc.sjwMax) {
      errorCode |= kDataSJWTooLarge ;
    }
    if (mDataSJW > mDataPhaseSegment2) {
      errorCode |= kDataSJWGreaterThanPhaseSegment2 ;
    }
  }

  if (!mBitSettingOk) {
    errorCode |= kBitRateNotAchievedWithinTolerance ;
  }

  return errorCode ;
}

//------------------------------------------------------------------------------
//   Build ESP-IDF structs
//------------------------------------------------------------------------------

void ACAN_ESP32FD_Settings::buildArbitrationTiming (twai_timing_advanced_config_t & outTiming) const {
  outTiming = {} ;
  outTiming.brp = mArbitrationBitRatePrescaler ;
  outTiming.prop_seg = uint8_t (mArbitrationPropagationSegment) ;
  outTiming.tseg_1 = uint8_t (mArbitrationPhaseSegment1) ;
  outTiming.tseg_2 = uint8_t (mArbitrationPhaseSegment2) ;
  outTiming.sjw = uint8_t (mArbitrationSJW) ;
  outTiming.ssp_offset = 0 ;
}

//------------------------------------------------------------------------------

void ACAN_ESP32FD_Settings::buildDataTiming (twai_timing_advanced_config_t & outTiming) const {
  outTiming = {} ;
  outTiming.brp = mDataBitRatePrescaler ;
  outTiming.prop_seg = uint8_t (mDataPropagationSegment) ;
  outTiming.tseg_1 = uint8_t (mDataPhaseSegment1) ;
  outTiming.tseg_2 = uint8_t (mDataPhaseSegment2) ;
  outTiming.sjw = uint8_t (mDataSJW) ;
  outTiming.ssp_offset = 0 ;
}

//------------------------------------------------------------------------------
