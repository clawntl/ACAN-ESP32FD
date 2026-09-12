//------------------------------------------------------------------------------

#include <ACAN_ESP32FD_FIFO.h>

//------------------------------------------------------------------------------

ACAN_ESP32FD_FIFO::ACAN_ESP32FD_FIFO (void) {
}

//------------------------------------------------------------------------------

ACAN_ESP32FD_FIFO::~ ACAN_ESP32FD_FIFO (void) {
  delete [] mBuffer ;
}

//------------------------------------------------------------------------------

void ACAN_ESP32FD_FIFO::initWithSize (const uint16_t inSize) {
  delete [] mBuffer ;
  mBuffer = (inSize > 0) ? (new CANFDMessage [inSize]) : nullptr ;
  mSize = inSize ;
  mReadIndex = 0 ;
  mCount = 0 ;
  mPeakCount = 0 ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_FIFO::append (const CANFDMessage & inMessage) {
  const bool ok = mCount < mSize ;
  if (ok) {
    uint16_t writeIndex = mReadIndex + mCount ;
    if (writeIndex >= mSize) {
      writeIndex -= mSize ;
    }
    mBuffer [writeIndex] = inMessage ;
    mCount += 1 ;
    if (mPeakCount < mCount) {
      mPeakCount = mCount ;
    }
  }else{
    mPeakCount = mSize + 1 ; // marks overflow
  }
  return ok ;
}

//------------------------------------------------------------------------------

bool ACAN_ESP32FD_FIFO::remove (CANFDMessage & outMessage) {
  const bool ok = mCount > 0 ;
  if (ok) {
    outMessage = mBuffer [mReadIndex] ;
    mCount -= 1 ;
    mReadIndex += 1 ;
    if (mReadIndex == mSize) {
      mReadIndex = 0 ;
    }
  }
  return ok ;
}

//------------------------------------------------------------------------------

void ACAN_ESP32FD_FIFO::free (void) {
  delete [] mBuffer ; mBuffer = nullptr ;
  mSize = 0 ;
  mReadIndex = 0 ;
  mCount = 0 ;
  mPeakCount = 0 ;
}

//------------------------------------------------------------------------------
