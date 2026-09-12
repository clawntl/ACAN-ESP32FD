//------------------------------------------------------------------------------
// ACAN_ESP32FD - TwoBoards_Send
//
// Classic CAN across a real bus: this sketch runs on the sending board.
// Requires an external CAN transceiver (e.g. TJA1051, SN65HVD230) wired to
// mTxPin/mRxPin, and a second board running TwoBoards_Receive on the same
// bus (120 ohm termination at each physical end of the bus, as usual).
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

  Serial.println ("ACAN_ESP32FD - TwoBoards_Send") ;

  ACAN_ESP32FD_Settings settings (500UL * 1000UL, 875) ; // 500 kbit/s @ 87.5 % sample point
  settings.mTxPin = CAN_TX_PIN ;
  settings.mRxPin = CAN_RX_PIN ;

  const uint32_t errorCode = can0.begin (settings) ;
  Serial.printf ("can0.begin: %s (0x%08lX), actual bit rate %lu bit/s, sample point %.1f %%\n",
                 (errorCode == 0) ? "OK" : "FAILED", (unsigned long) errorCode,
                 (unsigned long) settings.actualArbitrationBitRate (),
                 settings.arbitrationSamplePointFromBitStart ()) ;
}

//------------------------------------------------------------------------------

static uint32_t gSendCount = 0 ;

//------------------------------------------------------------------------------

void loop () {
  if (can0.isRunning ()) {
    CANMessage frame ;
    frame.id = 0x123 ;
    frame.len = 4 ;
    frame.data32 [0] = gSendCount ;
    const bool ok = can0.tryToSend (frame) ;
    Serial.printf ("Sent #%lu: %s (tx FIFO: %lu/%lu)\n",
                   (unsigned long) gSendCount, ok ? "OK" : "FAILED",
                   (unsigned long) can0.driverTransmitFIFOCount (), (unsigned long) can0.driverTransmitFIFOSize ()) ;
    if (ok) {
      gSendCount += 1 ;
    }
  }
  delay (200) ;
}

//------------------------------------------------------------------------------
