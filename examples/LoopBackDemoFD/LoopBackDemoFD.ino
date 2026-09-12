//------------------------------------------------------------------------------
// ACAN_ESP32FD - LoopBackDemoFD
//
// CAN FD, internal loopback + self-test: no transceiver, no second board,
// no bus wiring needed. Demonstrates independent arbitration / data phase
// bit rates and sample points. ESP32-C5 only (or any other TWAI-FD capable
// target) -- running this on ESP32-S3 will print a clear error and stop,
// since beginFD() cannot succeed there.
//
// Wiring: none required (loopback is fully internal), but mTxPin/mRxPin
// still need to name two valid, otherwise-unused GPIOs.
//------------------------------------------------------------------------------

#include <ACAN_ESP32FD.h>

//------------------------------------------------------------------------------

static const gpio_num_t CAN_TX_PIN = GPIO_NUM_4 ;
static const gpio_num_t CAN_RX_PIN = GPIO_NUM_5 ;

ACAN_ESP32FD can0 ;

//------------------------------------------------------------------------------

void setup () {
  Serial.begin (115200) ;
  while (! Serial) { delay (10) ; }
  delay (500) ;

  Serial.println ("ACAN_ESP32FD - LoopBackDemoFD") ;

  if (! ACAN_ESP32FD::controllerSupportsFD ()) {
    Serial.println ("This target's TWAI controller does not support CAN FD (SOC_TWAI_SUPPORT_FD not set).") ;
    Serial.println ("Build for ESP32-C5 (or another TWAI-FD capable target) to run this example.") ;
    while (true) { delay (1000) ; }
  }

  // Arbitration: 1 Mbit/s @ 80.0 % sample point.
  // Data:        x4 -> 4 Mbit/s @ 75.0 % sample point.
  ACAN_ESP32FD_Settings settings (1000UL * 1000UL, 800, DataBitRateFactor::x4, 750) ;
  settings.mTxPin = CAN_TX_PIN ;
  settings.mRxPin = CAN_RX_PIN ;
  settings.mModuleMode = ACAN_ESP32FD_Settings::LOOP_BACK_NO_ACK ;

  Serial.printf ("Arbitration: requested %lu bit/s -> actual %lu bit/s (%.1f %% s.p., %lu ppm off)\n",
                 (unsigned long) settings.mDesiredArbitrationBitRate,
                 (unsigned long) settings.actualArbitrationBitRate (),
                 settings.arbitrationSamplePointFromBitStart (),
                 (unsigned long) settings.ppmFromWishedBitRate ()) ;
  Serial.printf ("Data       : actual %lu bit/s (%.1f %% s.p.)\n",
                 (unsigned long) settings.actualDataBitRate (),
                 settings.dataSamplePointFromBitStart ()) ;
  Serial.printf ("Arbitration segments: BRP=%lu PROP=%lu PS1=%lu PS2=%lu SJW=%lu\n",
                 (unsigned long) settings.mArbitrationBitRatePrescaler,
                 (unsigned long) settings.mArbitrationPropagationSegment,
                 (unsigned long) settings.mArbitrationPhaseSegment1,
                 (unsigned long) settings.mArbitrationPhaseSegment2,
                 (unsigned long) settings.mArbitrationSJW) ;
  Serial.printf ("Data segments       : BRP=%lu PROP=%lu PS1=%lu PS2=%lu SJW=%lu\n",
                 (unsigned long) settings.mDataBitRatePrescaler,
                 (unsigned long) settings.mDataPropagationSegment,
                 (unsigned long) settings.mDataPhaseSegment1,
                 (unsigned long) settings.mDataPhaseSegment2,
                 (unsigned long) settings.mDataSJW) ;

  const uint32_t errorCode = can0.beginFD (settings) ;
  if (errorCode == 0) {
    Serial.printf ("can0.beginFD: OK (FD enabled: %s)\n", can0.isFDEnabled () ? "yes" : "no") ;
  }else{
    Serial.printf ("can0.beginFD FAILED, error code 0x%08lX\n", (unsigned long) errorCode) ;
    Serial.printf ("  checkBitSettingConsistency () = 0x%08lX\n", (unsigned long) settings.checkBitSettingConsistency ()) ;
  }
}

//------------------------------------------------------------------------------

static uint32_t gSendCount = 0 ;
static uint32_t gLastSendDate = 0 ;

//------------------------------------------------------------------------------

void loop () {
  if (can0.isRunning ()) {
    if ((millis () - gLastSendDate) >= 500) {
      gLastSendDate = millis () ;
      CANFDMessage frame ;
      frame.id = 0x456 ;
      frame.ext = false ;
      frame.type = CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH ;
      frame.len = 16 ; // valid FD length
      for (uint8_t i = 0 ; i < frame.len ; i++) {
        frame.data [i] = uint8_t (gSendCount + i) ;
      }
      const bool ok = can0.tryToSendFD (frame) ;
      Serial.printf ("Sent FD frame #%lu (len=%u): %s\n", (unsigned long) gSendCount, frame.len, ok ? "OK" : "FAILED") ;
      if (ok) {
        gSendCount += 1 ;
      }
    }

    CANFDMessage received ;
    while (can0.receiveFD (received)) {
      Serial.printf ("Received: id=0x%03lX %s len=%u data[0..3]=%02X %02X %02X %02X\n",
                     (unsigned long) received.id,
                     (received.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH) ? "FD+BRS" :
                     (received.type == CANFDMessage::CANFD_NO_BIT_RATE_SWITCH) ? "FD" : "classic",
                     received.len,
                     received.data [0], received.data [1], received.data [2], received.data [3]) ;
    }

    if (can0.isBusOff ()) {
      Serial.println ("Bus-off! Attempting recovery...") ;
      can0.recoverFromBusOff () ;
    }
  }
}

//------------------------------------------------------------------------------
