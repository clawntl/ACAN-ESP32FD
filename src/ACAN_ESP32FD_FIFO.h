//------------------------------------------------------------------------------
// ACAN_ESP32FD_FIFO: a small fixed-capacity ring buffer of CANFDMessage.
//
// The buffer itself performs NO locking: the caller (ACAN_ESP32FD driver) is
// responsible for protecting concurrent access between task context and the
// TWAI ISR context (see ACAN_ESP32FD.cpp, which uses a portMUX spinlock).
// This mirrors the ACANFD_STM32 / ACAN2517FD FIFO design.
//------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------

#include <ACAN_ESP32FD_CANFDMessage.h>

//------------------------------------------------------------------------------

class ACAN_ESP32FD_FIFO {

  //············································································
  public: ACAN_ESP32FD_FIFO (void) ;
  public: ~ ACAN_ESP32FD_FIFO (void) ;

  //············································································
  // Private properties
  //············································································

  private: CANFDMessage * mBuffer = nullptr ;
  private: uint16_t mSize = 0 ;
  private: uint16_t mReadIndex = 0 ;
  private: uint16_t mCount = 0 ;
  private: uint16_t mPeakCount = 0 ; // > mSize if overflow did occur

  //············································································
  // Accessors
  //············································································

  public: inline uint16_t size (void) const { return mSize ; }
  public: inline uint16_t count (void) const { return mCount ; }
  public: inline bool isEmpty (void) const { return (mCount == 0) && (mSize > 0) ; }
  public: inline bool isFull (void) const { return mCount == mSize ; }
  public: inline bool didOverflow (void) const { return mPeakCount > mSize ; }
  public: inline uint16_t peakCount (void) const { return mPeakCount ; }
  public: inline void resetPeakCount (void) { mPeakCount = mCount ; }

  //············································································
  // initWithSize / free
  //············································································

  public: void initWithSize (const uint16_t inSize) ;
  public: void free (void) ;

  //············································································
  // append / remove
  //············································································

  public: bool append (const CANFDMessage & inMessage) ;
  public: bool remove (CANFDMessage & outMessage) ;

  //············································································
  // No copy
  //············································································

  private: ACAN_ESP32FD_FIFO (const ACAN_ESP32FD_FIFO &) = delete ;
  private: ACAN_ESP32FD_FIFO & operator = (const ACAN_ESP32FD_FIFO &) = delete ;
} ;

//------------------------------------------------------------------------------
