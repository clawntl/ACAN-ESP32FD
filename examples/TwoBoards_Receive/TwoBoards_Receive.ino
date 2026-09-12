//------------------------------------------------------------------------------
// ACAN_ESP32FD - TwoBoards_Receive
//
// Classic CAN across a real bus: this sketch runs on the receiving board.
// Requires an external CAN transceiver (e.g. TJA1051, SN65HVD230) wired to
// mTxPin/mRxPin, and a second board running TwoBoards_Send on the same bus
// (120 ohm termination at each physical end of the bus, as usual).
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

  Serial.println ("ACAN_ESP32FD - TwoBoards_Receive") ;

  ACAN_ESP32FD_Settings settings (500UL * 1000UL, 875) ; // must match the sender: 500 kbit/s @ 87.5 %
  settings.mTxPin = CAN_TX_PIN ;
  settings.mRxPin = CAN_RX_PIN ;

  // Only accept 0x123, as an example of the mask filter API.
  settings.mFilters.addMaskFilter (0x123, 0x7FF, false) ;

  const uint32_t errorCode = can0.begin (settings) ;
  Serial.printf ("can0.begin: %s (0x%08lX), actual bit rate %lu bit/s, sample point %.1f %%\n",
                 (errorCode == 0) ? "OK" : "FAILED", (unsigned long) errorCode,
                 (unsigned long) settings.actualArbitrationBitRate (),
                 settings.arbitrationSamplePointFromBitStart ()) ;
}

//------------------------------------------------------------------------------

void loop () {
  CANMessage frame ;
  if (can0.receive (frame)) {
    Serial.printf ("Received: id=0x%03lX ext=%d rtr=%d len=%u data32[0]=%lu  (errors: tx=%u rx=%u, bus-off=%s)\n",
                   (unsigned long) frame.id, frame.ext, frame.rtr, frame.len,
                   (unsigned long) frame.data32 [0],
                   can0.txErrorCounter (), can0.rxErrorCounter (),
                   can0.isBusOff () ? "yes" : "no") ;
  }
}

//------------------------------------------------------------------------------
