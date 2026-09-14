//------------------------------------------------------------------------------
// ACAN_ESP32FD_Settings
//
// ACAN-style bit-timing settings object for the ESP-IDF v5.5+ "twai_node"
// driver (esp_twai.h / esp_twai_onchip.h), targeting:
//    - ESP32-S3 (and any other classic-only TWAI controller): classic CAN only
//    - ESP32-C5 (and any other TWAI-FD controller)          : CAN FD capable
//
// Exactly like ACANFD_STM32_Settings, you give a *desired* bit rate and a
// *desired* sample point (in permill, i.e. 1/1000 of the bit time, so 800
// means 80.0 %), and the constructor computes an explicit BRP / PROP_SEG /
// PHASE_SEG1 / PHASE_SEG2 / SJW quintet. Every one of those fields is public
// and can be overridden by hand afterwards if you want full manual control,
// exactly like the STM32 version. Once you are happy with the settings, call
//     actualArbitrationBitRate() / arbitrationSamplePointFromBitStart()
//     actualDataBitRate()        / dataSamplePointFromBitStart()
// to double-check what will actually be programmed into the hardware before
// calling ACAN_ESP32FD::begin() / beginFD().
//------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------

#include <Arduino.h>
#include "esp_twai_types.h"
#include "esp_twai_onchip.h"
#include "soc/soc_caps.h"

#include <ACAN_ESP32FD_DataBitRateFactor.h>
#include <ACAN_ESP32FD_Filters.h>

//------------------------------------------------------------------------------
// Some newer IDF releases (v5.5+ on some targets, and all IDF 6.x branches
// seen so far, e.g. ESP32-S31) renamed the FD capability macro from
// SOC_TWAI_SUPPORT_FD to SOC_TWAI_FD_SUPPORTED. Bridge the old name to the
// new one when only the new one is present, so this library keeps detecting
// FD-capable controllers regardless of which IDF version defines it.
//------------------------------------------------------------------------------

#if !defined(SOC_TWAI_SUPPORT_FD) && defined(SOC_TWAI_FD_SUPPORTED)
  #define SOC_TWAI_SUPPORT_FD SOC_TWAI_FD_SUPPORTED
#endif

//------------------------------------------------------------------------------
// SOC_TWAI_SUPPORT_FD is not defined at all on classic-only targets, so make
// it safe to test with #if everywhere in this library.
//------------------------------------------------------------------------------

#ifndef SOC_TWAI_SUPPORT_FD
  #define SOC_TWAI_SUPPORT_FD 0
#endif

//------------------------------------------------------------------------------

class ACAN_ESP32FD_Settings {

  //············································································
  //   Module mode
  //············································································

  public: typedef enum : uint8_t {
    NORMAL,            // Normal operation on the bus
    LOOP_BACK_NO_ACK,  // Internal loopback + self-test: no external bus/transceiver/ack needed
    LOOP_BACK,         // Loopback while still driving/needing ack from the real bus
    LISTEN_ONLY        // Listen only: never transmits, not even ACK/error frames
  } ModuleMode ;

  //············································································
  //   Constructor: classic bit rate only (single phase, no bit rate switch).
  //   Works identically on ESP32-S3 and ESP32-C5. Sample point defaults to
  //   80.0 % (800 permill) if not given.
  //············································································

  public: explicit ACAN_ESP32FD_Settings (const uint32_t inDesiredBitRate,
                                          const uint32_t inDesiredSamplePointPermill = 800,
                                          const uint32_t inTolerancePPM = 1000) ;

  //············································································
  //   Constructors: CAN FD (arbitration + data phase). On a classic-only
  //   controller (e.g. ESP32-S3) the data-phase fields are computed but
  //   never used: beginFD() will fail at run time on such a target -- use
  //   begin() there, or write portable code and check CANFDIsEnabled() /
  //   ACAN_ESP32FD::controllerSupportsFD().
  //············································································

  public: ACAN_ESP32FD_Settings (const uint32_t inDesiredArbitrationBitRate,
                                 const DataBitRateFactor inDataBitRateFactor,
                                 const uint32_t inTolerancePPM = 1000) ;

  public: ACAN_ESP32FD_Settings (const uint32_t inDesiredArbitrationBitRate,
                                 const uint32_t inDesiredArbitrationSamplePointPermill,
                                 const DataBitRateFactor inDataBitRateFactor,
                                 const uint32_t inDesiredDataSamplePointPermill,
                                 const uint32_t inTolerancePPM = 1000) ;

  //············································································
  //   Desired values (as given to the constructor)
  //············································································

  public: const uint32_t mDesiredArbitrationBitRate ; // In bit/s
  public: const DataBitRateFactor mDataBitRateFactor ; // x1 --> no bit rate switch (or classic CAN)

  //············································································
  //   Clock source. Both ESP32-S3 (APB) and ESP32-C5 (PLL_F80M, the default
  //   clock source) run their TWAI peripheral from an 80 MHz clock out of
  //   reset, so mClockFrequency defaults to that. If you select a different
  //   mClockSource (e.g. TWAI_CLK_SRC_XTAL on ESP32-C5), set mClockFrequency
  //   accordingly *before* the bit-timing fields below are relied upon --
  //   easiest is to just construct the Settings object after setting these.
  //············································································

  public: twai_clock_source_t mClockSource = (twai_clock_source_t) 0 ; // 0 --> TWAI_CLK_SRC_DEFAULT
  public: uint32_t mClockFrequency = 80UL * 1000UL * 1000UL ; // Hz

  //············································································
  //   Arbitration phase bit timing (always used, classic or FD)
  //············································································

  public: uint32_t mArbitrationBitRatePrescaler = 1 ;      // BRP
  public: uint32_t mArbitrationPropagationSegment = 0 ;    // PROP_SEG (FD controllers only, else always 0)
  public: uint32_t mArbitrationPhaseSegment1 = 1 ;         // PHASE_SEG1 (a.k.a. TSEG1 / PH1)
  public: uint32_t mArbitrationPhaseSegment2 = 1 ;         // PHASE_SEG2 (a.k.a. TSEG2 / PH2)
  public: uint32_t mArbitrationSJW = 1 ;                   // Synchronization Jump Width

  //············································································
  //   Data phase bit timing (only meaningful if CANFDIsEnabled())
  //············································································

  public: uint32_t mDataBitRatePrescaler = 1 ;
  public: uint32_t mDataPropagationSegment = 0 ;
  public: uint32_t mDataPhaseSegment1 = 1 ;
  public: uint32_t mDataPhaseSegment2 = 1 ;
  public: uint32_t mDataSJW = 1 ;

  //············································································
  //   True if the above bit-timing configuration matches the desired bit
  //   rate(s) within the requested tolerance (see checkBitSettingConsistency()
  //   for details about *why* it failed, if it did).
  //············································································

  public: bool mBitSettingOk = true ;

  //············································································
  //   Module behaviour
  //············································································

  public: ModuleMode mModuleMode = NORMAL ;

  //············································································
  //   Pins.  GPIO_NUM_NC (-1) means "not set" and begin()/beginFD() will
  //   report an error if mTxPin / mRxPin are left unset.
  //············································································

  public: gpio_num_t mTxPin = GPIO_NUM_NC ;
  public: gpio_num_t mRxPin = GPIO_NUM_NC ;
  public: gpio_num_t mQuantaClockOutPin = GPIO_NUM_NC ; // optional debug: outputs the internal Tq clock
  public: gpio_num_t mBusOffIndicatorPin = GPIO_NUM_NC ; // optional: driven low while bus-off

  //············································································
  //   Software (driver-side) FIFOs, exactly like the other ACAN libraries.
  //   Received frames are copied out of the (tiny) hardware path inside the
  //   RX ISR callback into this FIFO; receive()/receiveFD() then pop from it
  //   in task context.
  //············································································

  public: uint16_t mDriverReceiveFIFOSize = 32 ;
  public: uint16_t mDriverTransmitFIFOSize = 16 ;

  //············································································
  //   Hardware transmit queue depth: number of frames the driver can track
  //   "in flight" concurrently (each one costs a small heap allocation, see
  //   the ESP-IDF TWAI documentation: ~4 bytes/frame plus a per-node fixed
  //   overhead). Extra frames queue up in the software FIFO above.
  //············································································

  public: uint32_t mHardwareTxQueueDepth = 8 ;

  //············································································
  //   Hardware retransmission limit: -1 retries forever (default, matches
  //   normal CAN behaviour), 0 means single-shot (no automatic retry), N
  //   retries N times before giving up on a frame.
  //············································································

  public: int8_t mHardwareRetransmitLimit = -1 ;

  //············································································
  //   Optional hardware RX timestamping (0 = disabled). See CANFDMessage /
  //   the driver source if you need access to the timestamp; the default
  //   ACAN-style receive()/receiveFD() calls do not expose it.
  //············································································

  public: uint32_t mTimestampResolutionHz = 0 ;

  //············································································
  //   Interrupt priority, [0:3], higher is more urgent. 0 = default.
  //············································································

  public: int mInterruptPriority = 0 ;

  //············································································
  //   Hardware acceptance filters. Leave empty to receive every frame.
  //············································································

  public: ACAN_ESP32FD_Filters mFilters ;

  //············································································
  //   Accessors
  //············································································

  public: inline bool CANFDIsEnabled (void) const { return mDataBitRateFactor != DataBitRateFactor::x1 ; }

  public: uint32_t actualArbitrationBitRate (void) const ;
  public: bool exactArbitrationBitRate (void) const ;
  public: uint32_t ppmFromWishedBitRate (void) const ; // ppm error on the arbitration bit rate
  public: float arbitrationSamplePointFromBitStart (void) const ; // in %, e.g. 80.0

  public: uint32_t actualDataBitRate (void) const ;
  public: bool exactDataBitRate (void) const ;
  public: float dataSamplePointFromBitStart (void) const ; // in %, e.g. 80.0

  //············································································
  //   Bit settings are consistent? (returns 0 if ok)
  //············································································

  public: uint32_t checkBitSettingConsistency (void) const ;

  public: static const uint32_t kArbitrationBitRatePrescalerIsZero        = 1UL <<  0 ;
  public: static const uint32_t kArbitrationBitRatePrescalerTooLarge      = 1UL <<  1 ;
  public: static const uint32_t kArbitrationPhaseSegment1TooLarge         = 1UL <<  2 ;
  public: static const uint32_t kArbitrationPhaseSegment2IsZero           = 1UL <<  3 ;
  public: static const uint32_t kArbitrationPhaseSegment2TooLarge         = 1UL <<  4 ;
  public: static const uint32_t kArbitrationSJWIsZero                     = 1UL <<  5 ;
  public: static const uint32_t kArbitrationSJWTooLarge                   = 1UL <<  6 ;
  public: static const uint32_t kArbitrationSJWGreaterThanPhaseSegment2   = 1UL <<  7 ;
  public: static const uint32_t kArbitrationPropagationSegmentTooLarge    = 1UL <<  8 ;

  public: static const uint32_t kDataBitRatePrescalerIsZero               = 1UL << 10 ;
  public: static const uint32_t kDataBitRatePrescalerTooLarge             = 1UL << 11 ;
  public: static const uint32_t kDataPhaseSegment1TooLarge                = 1UL << 12 ;
  public: static const uint32_t kDataPhaseSegment2IsZero                  = 1UL << 13 ;
  public: static const uint32_t kDataPhaseSegment2TooLarge                = 1UL << 14 ;
  public: static const uint32_t kDataSJWIsZero                            = 1UL << 15 ;
  public: static const uint32_t kDataSJWTooLarge                          = 1UL << 16 ;
  public: static const uint32_t kDataSJWGreaterThanPhaseSegment2          = 1UL << 17 ;
  public: static const uint32_t kDataPropagationSegmentTooLarge           = 1UL << 18 ;

  public: static const uint32_t kBitRateNotAchievedWithinTolerance        = 1UL << 20 ;
  public: static const uint32_t kMissingTxPin                             = 1UL << 21 ;
  public: static const uint32_t kMissingRxPin                             = 1UL << 22 ;

  //············································································
  //   Internal: build the ESP-IDF advanced timing structs. Used by
  //   ACAN_ESP32FD::begin()/beginFD(); exposed publicly in case you want to
  //   call twai_node_reconfig_timing() yourself (e.g. to switch bit rate at
  //   run time without a full end()/begin() cycle).
  //············································································

  public: void buildArbitrationTiming (twai_timing_advanced_config_t & outTiming) const ;
  public: void buildDataTiming (twai_timing_advanced_config_t & outTiming) const ;

  //············································································
  //   Internal calculator, exposed as a static utility in case you want to
  //   experiment with sample points interactively (e.g. from a Serial menu)
  //   without constructing a whole new Settings object.
  //············································································

  public: static bool computeSegments (const uint32_t inClockFrequency,
                                       const uint32_t inBitRate,
                                       const uint32_t inSamplePointPermill,
                                       const bool inFDCapableController,
                                       const bool inDataPhase,
                                       uint32_t & outBRP,
                                       uint32_t & outPropSeg,
                                       uint32_t & outPhaseSegment1,
                                       uint32_t & outPhaseSegment2,
                                       uint32_t & outSJW,
                                       uint32_t & outActualBitRate) ;
} ;

//------------------------------------------------------------------------------
