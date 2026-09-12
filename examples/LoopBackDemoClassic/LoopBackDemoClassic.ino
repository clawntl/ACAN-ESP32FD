//------------------------------------------------------------------------------
// ACAN_ESP32FD - LoopBackDemoClassic
//
// Classic CAN, internal loopback + self-test: no transceiver, no second
// board, no bus wiring needed at all -- the controller receives its own
// transmitted frames internally. Works unmodified on ESP32-S3 and ESP32-C5.
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

  Serial.println ("ACAN_ESP32FD - LoopBackDemoClassic") ;

  ACAN_ESP32FD_Settings settings (500UL * 1000UL) ; // 500 kbit/s, default 80.0 % sample point
  settings.mTxPin = CAN_TX_PIN ;
  settings.mRxPin = CAN_RX_PIN ;
  settings.mModuleMode = ACAN_ESP32FD_Settings::LOOP_BACK_NO_ACK ;

  Serial.printf ("Requested bit rate: %lu bit/s\n", (unsigned long) settings.mDesiredArbitrationBitRate) ;
  Serial.printf ("Computed: BRP=%lu PHASE_SEG1=%lu PHASE_SEG2=%lu SJW=%lu\n",
                 (unsigned long) settings.mArbitrationBitRatePrescaler,
                 (unsigned long) settings.mArbitrationPhaseSegment1,
                 (unsigned long) settings.mArbitrationPhaseSegment2,
                 (unsigned long) settings.mArbitrationSJW) ;
  Serial.printf ("Actual bit rate: %lu bit/s (%lu ppm off), sample point %.1f %%\n",
                 (unsigned long) settings.actualArbitrationBitRate (),
                 (unsigned long) settings.ppmFromWishedBitRate (),
                 settings.arbitrationSamplePointFromBitStart ()) ;

  const uint32_t errorCode = can0.begin (settings) ;
  if (errorCode == 0) {
    Serial.println ("can0.begin: OK") ;
  }else{
    Serial.printf ("can0.begin FAILED, error code 0x%08lX\n", (unsigned long) errorCode) ;
    Serial.printf ("  checkBitSettingConsistency () = 0x%08lX\n", (unsigned long) settings.checkBitSettingConsistency ()) ;
  }
}

//------------------------------------------------------------------------------

static uint32_t gSendCount = 0 ;
static uint32_t gReceiveCount = 0 ;
static uint32_t gLastSendDate = 0 ;

//------------------------------------------------------------------------------

void loop () {
  if (can0.isRunning ()) {
    if ((millis () - gLastSendDate) >= 1000) {
      gLastSendDate = millis () ;
      CANMessage frame ;
      frame.id = 0x123 ;
      frame.len = 4 ;
      frame.data32 [0] = gSendCount ;
      const bool ok = can0.tryToSend (frame) ;
      Serial.printf ("Sent #%lu: %s\n", (unsigned long) gSendCount, ok ? "OK" : "FAILED (FIFO full?)") ;
      if (ok) {
        gSendCount += 1 ;
      }
    }

    CANMessage received ;
    while (can0.receive (received)) {
      gReceiveCount += 1 ;
      Serial.printf ("Received #%lu: id=0x%03lX len=%u data32[0]=%lu (rx total: %lu)\n",
                     (unsigned long) gReceiveCount - 1, (unsigned long) received.id, received.len,
                     (unsigned long) received.data32 [0], (unsigned long) gReceiveCount) ;
    }

    if (can0.isBusOff ()) {
      Serial.println ("Bus-off! Attempting recovery...") ;
      can0.recoverFromBusOff () ;
    }
  }
}

//------------------------------------------------------------------------------
