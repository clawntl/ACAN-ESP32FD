//------------------------------------------------------------------------------
// ACAN_ESP32FD
//
// ACAN-style CAN / CAN-FD driver for the ESP-IDF v5.5+ "twai_node" API
// (esp_twai.h / esp_twai_onchip.h), targeting ESP32-S3 (classic CAN only)
// and ESP32-C5 (CAN FD capable), with the same source-level API on both.
//
// Typical usage (see examples/ for complete sketches):
//
//   ACAN_ESP32FD can0 ;
//
//   void setup () {
//     ACAN_ESP32FD_Settings settings (500UL * 1000UL, DataBitRateFactor::x4) ; // 500 kbit/s arbitration, 2 Mbit/s data
//     settings.mTxPin = GPIO_NUM_4 ;
//     settings.mRxPin = GPIO_NUM_5 ;
//     const uint32_t errorCode = can0.beginFD (settings) ; // or can0.begin (settings) for classic CAN
//     if (errorCode != 0) {
//       Serial.print ("CAN begin error 0x") ; Serial.println (errorCode, HEX) ;
//     }
//   }
//
//   void loop () {
//     CANFDMessage frame ;
//     if (can0.receiveFD (frame)) { ... }
//     can0.tryToSendFD (frame) ;
//   }
//
// One instance manages one TWAI hardware controller. ESP32-S3 has a single
// controller; ESP32-C5 has two, so you may instantiate two ACAN_ESP32FD
// objects on a C5 (each with its own pins) instead of a single fixed global.
//------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------

#include <Arduino.h>

#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_twai_types.h"

#include <freertos/FreeRTOS.h>

#include <ACAN_ESP32FD_Settings.h>
#include <ACAN_ESP32FD_FIFO.h>
#include <ACAN_ESP32FD_CANMessage.h>
#include <ACAN_ESP32FD_CANFDMessage.h>

//------------------------------------------------------------------------------

class ACAN_ESP32FD {

  //············································································
  //   Construction / destruction
  //············································································

  public: ACAN_ESP32FD (void) = default ;
  public: ~ ACAN_ESP32FD (void) ;

  //············································································
  //   begin (classic CAN only -- works on ESP32-S3 and ESP32-C5) /
  //   beginFD (CAN FD, arbitration + data phase -- ESP32-C5 only).
  //
  //   Returns 0 (kNoError) on success, otherwise a bit mask built from the
  //   kXxx constants below telling you what went wrong, mirroring the
  //   ACANFD_STM32::beginFD() convention.
  //············································································

  public: uint32_t begin (const ACAN_ESP32FD_Settings & inSettings) ;
  public: uint32_t beginFD (const ACAN_ESP32FD_Settings & inSettings) ;

  public: void end (void) ;

  public: inline bool isRunning (void) const { return mRunning ; }
  public: inline bool isFDEnabled (void) const { return mFDEnabled ; }

  //············································································
  //   true if this build targets an FD-capable TWAI controller
  //   (SOC_TWAI_SUPPORT_FD), regardless of whether beginFD() was called.
  //············································································

  public: static inline bool controllerSupportsFD (void) {
    #if SOC_TWAI_SUPPORT_FD
      return true ;
    #else
      return false ;
    #endif
  }

  //············································································
  //   Transmission. tryToSendFD accepts classic and FD-format messages
  //   (selected by inMessage.type); tryToSend is a convenience wrapper for
  //   classic-only code (identical to calling tryToSendFD (CANFDMessage (inMessage))).
  //   Returns false if the message is invalid, if it needs FD/BRS support
  //   that was not enabled by beginFD(), or if both the hardware transmit
  //   slots and the software transmit FIFO are full.
  //············································································

  public: bool tryToSendFD (const CANFDMessage & inMessage) ;
  public: inline bool tryToSend (const CANMessage & inMessage) { return tryToSendFD (CANFDMessage (inMessage)) ; }

  //············································································
  //   Reception. Frames are copied out of the (ISR) driver callback into a
  //   software FIFO (ACAN_ESP32FD_Settings::mDriverReceiveFIFOSize); these
  //   accessors pop from that FIFO in task context, at your own pace.
  //   `receive` is a convenience wrapper for classic-only code: if a CAN FD
  //   format frame (or a frame longer than 8 bytes) is at the head of the
  //   FIFO, it is still removed from the FIFO but `receive` returns false --
  //   use receiveFD if you might be sending/receiving FD frames.
  //············································································

  public: inline bool availableFD (void) const { return ! mDriverReceiveFIFO.isEmpty () ; }
  public: bool receiveFD (CANFDMessage & outMessage) ;

  public: inline bool available (void) const { return availableFD () ; }
  public: bool receive (CANMessage & outMessage) ;

  //············································································
  //   Callback-style reception, as in ACAN2515 / ACAN2517FD: pops (at most)
  //   one frame from the receive FIFO and, if one was available, invokes
  //   inCallBack with it. Call repeatedly (e.g. once per loop() iteration,
  //   or drain it entirely) instead of / in addition to receiveFD().
  //············································································

  public: bool dispatchReceivedMessage (const ACANFDCallBackRoutine inCallBack = nullptr) ;

  //············································································
  //   Software FIFO introspection
  //············································································

  public: inline uint32_t driverReceiveFIFOSize      (void) const { return mDriverReceiveFIFO.size () ; }
  public: inline uint32_t driverReceiveFIFOCount     (void) const { return mDriverReceiveFIFO.count () ; }
  public: inline uint32_t driverReceiveFIFOPeakCount (void) const { return mDriverReceiveFIFO.peakCount () ; }
  public: inline bool     driverReceiveFIFODidOverflow (void) const { return mDriverReceiveFIFO.didOverflow () ; }

  public: inline uint32_t driverTransmitFIFOSize      (void) const { return mDriverTransmitFIFO.size () ; }
  public: inline uint32_t driverTransmitFIFOCount     (void) const { return mDriverTransmitFIFO.count () ; }
  public: inline uint32_t driverTransmitFIFOPeakCount (void) const { return mDriverTransmitFIFO.peakCount () ; }

  //············································································
  //   Status / error handling (wraps twai_node_get_info() / twai_node_recover())
  //············································································

  public: bool     isBusOff        (void) const ;
  public: uint16_t txErrorCounter  (void) const ;
  public: uint16_t rxErrorCounter  (void) const ;
  public: uint32_t busErrorCount   (void) const ; // cumulative bus error count since begin()/beginFD()
  public: uint32_t hardwareTransmitQueueRemaining (void) const ; // free slots in the driver's own tx queue

  //············································································
  //   Start bus-off recovery (asynchronous: the controller reconnects after
  //   observing 129 consecutive recessive bits on the bus). Poll isBusOff()
  //   afterwards to know when recovery completed.
  //············································································

  public: bool recoverFromBusOff (void) ;

  //············································································
  //   Low-level escape hatch: the underlying ESP-IDF node handle, e.g. if you
  //   want to call twai_node_reconfig_timing() yourself at run time to change
  //   bit rate without a full end()/begin() cycle.
  //············································································

  public: inline twai_node_handle_t handle (void) const { return mHandle ; }

  //············································································
  //   begin()/beginFD() error codes (bit mask)
  //············································································

  public: static const uint32_t kNoError                    = 0 ;
  public: static const uint32_t kAlreadyRunning              = 1UL << 0 ;
  public: static const uint32_t kSettingsError                = 1UL << 1 ; // see inSettings.checkBitSettingConsistency ()
  public: static const uint32_t kControllerDoesNotSupportFD   = 1UL << 2 ; // beginFD() called on a classic-only target
  public: static const uint32_t kNodeCreationFailed            = 1UL << 3 ;
  public: static const uint32_t kTimingConfigurationFailed     = 1UL << 4 ;
  public: static const uint32_t kFilterConfigurationFailed     = 1UL << 5 ;
  public: static const uint32_t kCallbackRegistrationFailed    = 1UL << 6 ;
  public: static const uint32_t kEnableFailed                  = 1UL << 7 ;
  public: static const uint32_t kOutOfMemory                   = 1UL << 8 ;

  //············································································
  //   Internal implementation
  //············································································

  private: uint32_t beginImplementation (const ACAN_ESP32FD_Settings & inSettings, const bool inRequestFD) ;
  private: void releaseHardwareResources (void) ;

  private: struct TxSlot {
    twai_frame_t frame {} ;
    uint8_t data [64] = {} ;
    bool busy = false ;
  } ;

  private: static void buildFrame (const CANFDMessage & inMessage, TxSlot & outSlot) ;
  private: void startNextQueuedTransmitFromISR (TxSlot & inFreedSlot) ;

  private: static bool onRxDoneCallback (twai_node_handle_t inHandle, const twai_rx_done_event_data_t * inEventData, void * inUserCtx) ;
  private: static bool onTxDoneCallback (twai_node_handle_t inHandle, const twai_tx_done_event_data_t * inEventData, void * inUserCtx) ;

  private: bool handleRxDoneFromISR (void) ;
  private: bool handleTxDoneFromISR (const twai_frame_t * inDoneFrame) ;

  private: twai_node_handle_t mHandle = nullptr ;
  private: bool mRunning = false ;
  private: bool mFDEnabled = false ; // true if beginFD() configured & applied data-phase timing

  private: ACAN_ESP32FD_FIFO mDriverReceiveFIFO ;
  private: ACAN_ESP32FD_FIFO mDriverTransmitFIFO ;

  private: TxSlot * mTxSlots = nullptr ;
  private: uint32_t mTxSlotCount = 0 ;

  private: uint8_t mRxScratchBuffer [64] = {} ; // reused across on_rx_done invocations (ISR-serialized, single controller)

  private: portMUX_TYPE mRxSpinlock = portMUX_INITIALIZER_UNLOCKED ;
  private: portMUX_TYPE mTxSpinlock = portMUX_INITIALIZER_UNLOCKED ;

  //············································································
  //   No copy
  //············································································

  private: ACAN_ESP32FD (const ACAN_ESP32FD &) = delete ;
  private: ACAN_ESP32FD & operator = (const ACAN_ESP32FD &) = delete ;
} ;

//------------------------------------------------------------------------------
