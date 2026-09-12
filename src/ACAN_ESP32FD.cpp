//------------------------------------------------------------------------------

#include <ACAN_ESP32FD.h>
#include <string.h>
#include <new>

//------------------------------------------------------------------------------
//   Destructor
//------------------------------------------------------------------------------

ACAN_ESP32FD::~ ACAN_ESP32FD (void) {
  end () ;
}

//------------------------------------------------------------------------------
//   Message <-> twai_frame_t conversion helpers
//------------------------------------------------------------------------------

namespace {

  void frameHeaderToCANFDMessage (const twai_frame_header_t & inHeader,
                                  const uint8_t * inBuffer,
                                  CANFDMessage & outMessage) {
    outMessage.id = inHeader.id ;
    outMessage.ext = inHeader.ide != 0 ;
    const uint16_t len = twaifd_dlc2len (inHeader.dlc) ;
    outMessage.len = uint8_t (len) ;
    if (inHeader.fdf != 0) {
      outMessage.type = (inHeader.brs != 0) ? CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH : CANFDMessage::CANFD_NO_BIT_RATE_SWITCH ;
    }else{
      outMessage.type = (inHeader.rtr != 0) ? CANFDMessage::CAN_REMOTE : CANFDMessage::CAN_DATA ;
    }
    if ((inHeader.rtr == 0) && (inBuffer != nullptr) && (len > 0)) {
      memcpy (outMessage.data, inBuffer, len) ;
    }
  }

}

//------------------------------------------------------------------------------

void ACAN_ESP32FD::buildFrame (const CANFDMessage & inMessage, TxSlot & outSlot) {
  outSlot.frame.header = {} ;
  outSlot.frame.header.id = inMessage.id ;
  outSlot.frame.header.ide = inMessage.ext ? 1 : 0 ;
  const bool isFDFormat = (inMessage.type == CANFDMessage::CANFD_NO_BIT_RATE_SWITCH)
                        || (inMessage.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH) ;
  outSlot.frame.header.fdf = isFDFormat ? 1 : 0 ;
  outSlot.frame.header.brs = (inMessage.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH) ? 1 : 0 ;
  outSlot.frame.header.rtr = (inMessage.type == CANFDMessage::CAN_REMOTE) ? 1 : 0 ;
  outSlot.frame.header.dlc = twaifd_len2dlc (inMessage.len) ;
  if (outSlot.frame.header.rtr != 0) {
    outSlot.frame.buffer = nullptr ;
    outSlot.frame.buffer_len = 0 ;
  }else{
    const uint8_t len = (inMessage.len > 64) ? 64 : inMessage.len ;
    memcpy (outSlot.data, inMessage.data, len) ;
    outSlot.frame.buffer = outSlot.data ;
    outSlot.frame.buffer_len = len ;
  }
}

//------------------------------------------------------------------------------
//   begin / beginFD
//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD::begin (const ACAN_ESP32FD_Settings & inSettings) {
  return beginImplementation (inSettings, false) ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD::beginFD (const ACAN_ESP32FD_Settings & inSettings) {
  return beginImplementation (inSettings, true) ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD::beginImplementation (const ACAN_ESP32FD_Settings & inSettings, const bool inRequestFD) {
  if (mRunning) {
    return kAlreadyRunning ;
  }
  if (inSettings.checkBitSettingConsistency () != 0) {
    return kSettingsError ;
  }
  const bool fdCapable = controllerSupportsFD () ;
  if (inRequestFD && ! fdCapable) {
    return kControllerDoesNotSupportFD ;
  }
  const bool wantFDTiming = inRequestFD && fdCapable && inSettings.CANFDIsEnabled () ;

  //--- Build the on-chip node configuration -----------------------------------
  twai_onchip_node_config_t nodeConfig = {} ;
  nodeConfig.io_cfg.tx = inSettings.mTxPin ;
  nodeConfig.io_cfg.rx = inSettings.mRxPin ;
  nodeConfig.io_cfg.quanta_clk_out = inSettings.mQuantaClockOutPin ;
  nodeConfig.io_cfg.bus_off_indicator = inSettings.mBusOffIndicatorPin ;
  nodeConfig.clk_src = inSettings.mClockSource ;
  nodeConfig.bit_timing.bitrate = inSettings.mDesiredArbitrationBitRate ; // coarse initial value, refined below
  nodeConfig.timestamp_resolution_hz = inSettings.mTimestampResolutionHz ;
  nodeConfig.fail_retry_cnt = inSettings.mHardwareRetransmitLimit ;
  nodeConfig.tx_queue_depth = inSettings.mHardwareTxQueueDepth ;
  nodeConfig.intr_priority = inSettings.mInterruptPriority ;
  switch (inSettings.mModuleMode) {
    case ACAN_ESP32FD_Settings::LOOP_BACK_NO_ACK :
      nodeConfig.flags.enable_loopback = 1 ;
      nodeConfig.flags.enable_self_test = 1 ;
      break ;
    case ACAN_ESP32FD_Settings::LOOP_BACK :
      nodeConfig.flags.enable_loopback = 1 ;
      break ;
    case ACAN_ESP32FD_Settings::LISTEN_ONLY :
      nodeConfig.flags.enable_listen_only = 1 ;
      break ;
    case ACAN_ESP32FD_Settings::NORMAL :
    default :
      break ;
  }

  esp_err_t err = twai_new_node_onchip (& nodeConfig, & mHandle) ;
  if (err != ESP_OK) {
    mHandle = nullptr ;
    return kNodeCreationFailed ;
  }

  //--- Apply the exact, user-controlled bit timing (overrides the coarse one) --
  twai_timing_advanced_config_t arbitrationTiming ;
  inSettings.buildArbitrationTiming (arbitrationTiming) ;
  twai_timing_advanced_config_t dataTiming ;
  if (wantFDTiming) {
    inSettings.buildDataTiming (dataTiming) ;
  }
  err = twai_node_reconfig_timing (mHandle, & arbitrationTiming, wantFDTiming ? & dataTiming : nullptr) ;
  if (err != ESP_OK) {
    releaseHardwareResources () ;
    return kTimingConfigurationFailed ;
  }

  //--- Acceptance filters ------------------------------------------------------
  for (uint32_t i = 0 ; i < inSettings.mFilters.maskFilterCount () ; i++) {
    const twai_mask_filter_config_t & f = inSettings.mFilters.maskFilterAtIndex (i) ;
    err = twai_node_config_mask_filter (mHandle, uint8_t (i), & f) ;
    if (err != ESP_OK) {
      releaseHardwareResources () ;
      return kFilterConfigurationFailed ;
    }
  }
  if (inSettings.mFilters.hasRangeFilter ()) {
    const twai_range_filter_config_t & f = inSettings.mFilters.rangeFilter () ;
    err = twai_node_config_range_filter (mHandle, 0, & f) ;
    if (err != ESP_OK) {
      releaseHardwareResources () ;
      return kFilterConfigurationFailed ;
    }
  }

  //--- Software FIFOs and hardware transmit slot pool --------------------------
  mDriverReceiveFIFO.initWithSize (inSettings.mDriverReceiveFIFOSize) ;
  mDriverTransmitFIFO.initWithSize (inSettings.mDriverTransmitFIFOSize) ;

  mTxSlotCount = (inSettings.mHardwareTxQueueDepth > 0) ? inSettings.mHardwareTxQueueDepth : 1 ;
  mTxSlots = new (std::nothrow) TxSlot [mTxSlotCount] ;
  if (mTxSlots == nullptr) {
    releaseHardwareResources () ;
    return kOutOfMemory ;
  }

  //--- Event callbacks -----------------------------------------------------------
  twai_event_callbacks_t callbacks = {} ;
  callbacks.on_rx_done = & ACAN_ESP32FD::onRxDoneCallback ;
  callbacks.on_tx_done = & ACAN_ESP32FD::onTxDoneCallback ;
  err = twai_node_register_event_callbacks (mHandle, & callbacks, this) ;
  if (err != ESP_OK) {
    releaseHardwareResources () ;
    return kCallbackRegistrationFailed ;
  }

  //--- Go live -------------------------------------------------------------------
  err = twai_node_enable (mHandle) ;
  if (err != ESP_OK) {
    releaseHardwareResources () ;
    return kEnableFailed ;
  }

  mFDEnabled = wantFDTiming ;
  mRunning = true ;
  return kNoError ;
}

//------------------------------------------------------------------------------

void ACAN_ESP32FD::releaseHardwareResources (void) {
  if (mHandle != nullptr) {
    twai_node_disable (mHandle) ; // no-op / harmless if already disabled
    twai_node_delete (mHandle) ;
    mHandle = nullptr ;
  }
  mDriverReceiveFIFO.free () ;
  mDriverTransmitFIFO.free () ;
  delete [] mTxSlots ;
  mTxSlots = nullptr ;
  mTxSlotCount = 0 ;
  mFDEnabled = false ;
  mRunning = false ;
}

//------------------------------------------------------------------------------

void ACAN_ESP32FD::end (void) {
  if (mRunning) {
    releaseHardwareResources () ;
  }
}

//------------------------------------------------------------------------------
//   Transmission
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::tryToSendFD (const CANFDMessage & inMessage) {
  if (! mRunning) {
    return false ;
  }
  if (! inMessage.isValid ()) {
    return false ;
  }
  const bool isFDFormat = (inMessage.type == CANFDMessage::CANFD_NO_BIT_RATE_SWITCH)
                        || (inMessage.type == CANFDMessage::CANFD_WITH_BIT_RATE_SWITCH) ;
  if ((isFDFormat || (inMessage.len > 8)) && ! mFDEnabled) {
    return false ; // needs CAN FD support that beginFD() did not enable (or is unavailable on this controller)
  }

  portENTER_CRITICAL (& mTxSpinlock) ;
  TxSlot * freeSlot = nullptr ;
  for (uint32_t i = 0 ; (freeSlot == nullptr) && (i < mTxSlotCount) ; i++) {
    if (! mTxSlots [i].busy) {
      freeSlot = & mTxSlots [i] ;
    }
  }
  bool queuedInSoftwareFIFO = false ;
  if (freeSlot != nullptr) {
    buildFrame (inMessage, * freeSlot) ;
    freeSlot->busy = true ;
  }else{
    queuedInSoftwareFIFO = mDriverTransmitFIFO.append (inMessage) ;
  }
  portEXIT_CRITICAL (& mTxSpinlock) ;

  if (freeSlot != nullptr) {
    const esp_err_t err = twai_node_transmit (mHandle, & freeSlot->frame, 0) ;
    if (err != ESP_OK) {
      portENTER_CRITICAL (& mTxSpinlock) ;
      freeSlot->busy = false ;
      portEXIT_CRITICAL (& mTxSpinlock) ;
      return false ;
    }
    return true ;
  }
  return queuedInSoftwareFIFO ;
}

//------------------------------------------------------------------------------
//   Reception
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::receiveFD (CANFDMessage & outMessage) {
  portENTER_CRITICAL (& mRxSpinlock) ;
  const bool ok = mDriverReceiveFIFO.remove (outMessage) ;
  portEXIT_CRITICAL (& mRxSpinlock) ;
  return ok ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD::receive (CANMessage & outMessage) {
  CANFDMessage fdMessage ;
  const bool gotMessage = receiveFD (fdMessage) ;
  bool ok = false ;
  if (gotMessage) {
    const bool isClassicCompatible =
      ((fdMessage.type == CANFDMessage::CAN_DATA) || (fdMessage.type == CANFDMessage::CAN_REMOTE))
      && (fdMessage.len <= 8) ;
    if (isClassicCompatible) {
      outMessage.id = fdMessage.id ;
      outMessage.ext = fdMessage.ext ;
      outMessage.rtr = (fdMessage.type == CANFDMessage::CAN_REMOTE) ;
      outMessage.idx = fdMessage.idx ;
      outMessage.len = fdMessage.len ;
      memcpy (outMessage.data, fdMessage.data, fdMessage.len) ;
      ok = true ;
    }
    // else: an FD-format (or >8 byte) frame was at the head of the FIFO; it has
    // been consumed, but cannot be represented as a classic CANMessage. Use
    // receiveFD() instead if your application mixes classic and FD frames.
  }
  return ok ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD::dispatchReceivedMessage (const ACANFDCallBackRoutine inCallBack) {
  CANFDMessage message ;
  const bool hasReceivedMessage = receiveFD (message) ;
  if (hasReceivedMessage && (inCallBack != nullptr)) {
    inCallBack (message) ;
  }
  return hasReceivedMessage ;
}

//------------------------------------------------------------------------------
//   Status
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::isBusOff (void) const {
  if (mHandle == nullptr) {
    return false ;
  }
  twai_node_status_t status = {} ;
  twai_node_record_t record = {} ;
  const esp_err_t err = twai_node_get_info (mHandle, & status, & record) ;
  return (err == ESP_OK) && (status.state == TWAI_ERROR_BUS_OFF) ;
}

//------------------------------------------------------------------------------

uint16_t ACAN_ESP32FD::txErrorCounter (void) const {
  if (mHandle == nullptr) {
    return 0 ;
  }
  twai_node_status_t status = {} ;
  twai_node_record_t record = {} ;
  const esp_err_t err = twai_node_get_info (mHandle, & status, & record) ;
  return (err == ESP_OK) ? status.tx_error_count : 0 ;
}

//------------------------------------------------------------------------------

uint16_t ACAN_ESP32FD::rxErrorCounter (void) const {
  if (mHandle == nullptr) {
    return 0 ;
  }
  twai_node_status_t status = {} ;
  twai_node_record_t record = {} ;
  const esp_err_t err = twai_node_get_info (mHandle, & status, & record) ;
  return (err == ESP_OK) ? status.rx_error_count : 0 ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD::busErrorCount (void) const {
  if (mHandle == nullptr) {
    return 0 ;
  }
  twai_node_status_t status = {} ;
  twai_node_record_t record = {} ;
  const esp_err_t err = twai_node_get_info (mHandle, & status, & record) ;
  return (err == ESP_OK) ? record.bus_err_num : 0 ;
}

//------------------------------------------------------------------------------

uint32_t ACAN_ESP32FD::hardwareTransmitQueueRemaining (void) const {
  if (mHandle == nullptr) {
    return 0 ;
  }
  twai_node_status_t status = {} ;
  twai_node_record_t record = {} ;
  const esp_err_t err = twai_node_get_info (mHandle, & status, & record) ;
  return (err == ESP_OK) ? status.tx_queue_remaining : 0 ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD::recoverFromBusOff (void) {
  if (mHandle == nullptr) {
    return false ;
  }
  return twai_node_recover (mHandle) == ESP_OK ;
}

//------------------------------------------------------------------------------
//   ISR callbacks
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::onRxDoneCallback (twai_node_handle_t inHandle, const twai_rx_done_event_data_t * inEventData, void * inUserCtx) {
  (void) inHandle ;
  (void) inEventData ;
  ACAN_ESP32FD * driver = static_cast <ACAN_ESP32FD *> (inUserCtx) ;
  return driver->handleRxDoneFromISR () ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD::onTxDoneCallback (twai_node_handle_t inHandle, const twai_tx_done_event_data_t * inEventData, void * inUserCtx) {
  (void) inHandle ;
  ACAN_ESP32FD * driver = static_cast <ACAN_ESP32FD *> (inUserCtx) ;
  return driver->handleTxDoneFromISR (inEventData->done_tx_frame) ;
}

//------------------------------------------------------------------------------
// Called from ISR context only (per twai_node_receive_from_isr() contract).
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::handleRxDoneFromISR (void) {
  twai_frame_t rxFrame = {} ;
  rxFrame.buffer = mRxScratchBuffer ;
  rxFrame.buffer_len = sizeof (mRxScratchBuffer) ;
  const bool higherPriorityTaskWoken = false ; // this driver does not use task notifications
  if (twai_node_receive_from_isr (mHandle, & rxFrame) == ESP_OK) {
    CANFDMessage message ;
    frameHeaderToCANFDMessage (rxFrame.header, mRxScratchBuffer, message) ;
    portENTER_CRITICAL_ISR (& mRxSpinlock) ;
    mDriverReceiveFIFO.append (message) ;
    portEXIT_CRITICAL_ISR (& mRxSpinlock) ;
  }
  return higherPriorityTaskWoken ;
}

//------------------------------------------------------------------------------
// Called from ISR context. Frees the slot that just finished transmitting
// and, if the software transmit FIFO has a message waiting, immediately
// starts it from the now-free slot (transmit is safe to call from an ISR).
//------------------------------------------------------------------------------

bool ACAN_ESP32FD::handleTxDoneFromISR (const twai_frame_t * inDoneFrame) {
  TxSlot * freedSlot = nullptr ;
  CANFDMessage nextMessage ;
  bool haveNextMessage = false ;

  portENTER_CRITICAL_ISR (& mTxSpinlock) ;
  for (uint32_t i = 0 ; (freedSlot == nullptr) && (i < mTxSlotCount) ; i++) {
    if (& mTxSlots [i].frame == inDoneFrame) {
      freedSlot = & mTxSlots [i] ;
    }
  }
  if (freedSlot != nullptr) {
    haveNextMessage = mDriverTransmitFIFO.remove (nextMessage) ;
    if (haveNextMessage) {
      buildFrame (nextMessage, * freedSlot) ; // still marked busy: reused immediately
    }else{
      freedSlot->busy = false ;
    }
  }
  portEXIT_CRITICAL_ISR (& mTxSpinlock) ;

  if (haveNextMessage && (freedSlot != nullptr)) {
    twai_node_transmit (mHandle, & freedSlot->frame, 0) ; // safe to call from ISR context
  }
  return false ;
}

//------------------------------------------------------------------------------
