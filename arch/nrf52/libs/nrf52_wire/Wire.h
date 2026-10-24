/*
 * TWI/I2C library for mRF5x
 * Copyright (c) 2015 Arduino LLC. All rights reserved.
 * Copyright (c) 2016 Sandeep Mistry All right reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef TwoWire_h
#define TwoWire_h

#include "nrf.h"

#include "Stream.h"
#include "variant.h"
#include "RingBuffer.h"

// WIRE_HAS_END means Wire has end()
#define WIRE_HAS_END 1

// MeshCore: every wait on the TWIM has a timeout (the framework's Wire waits forever, so a
// loose cable or a module holding a line could stop the firmware for good). On a timeout the
// TWIM is reset and the bus freed (9 SCL clocks and a STOP); endTransmission() returns 5 and
// requestFrom() 0 bytes. The counters let a caller tell which device made the errors.
#define WIRE_MESHCORE_TIMEOUT 1
#ifndef WIRE_TIMEOUT_MS
#define WIRE_TIMEOUT_MS 50
#endif

class TwoWire : public Stream
{
  public:
    TwoWire(NRF_TWIM_Type * p_twim, NRF_TWIS_Type * p_twis, IRQn_Type IRQn, uint8_t pinSDA, uint8_t pinSCL);
    void begin();
    void begin(uint8_t);
    void end();
    void setClock(uint32_t);
    void setPins(uint8_t pinSDA, uint8_t pinSCL);

    void beginTransmission(uint8_t);
    uint8_t endTransmission(bool stopBit);
    uint8_t endTransmission(void);

    uint8_t requestFrom(uint8_t address, size_t quantity, bool stopBit);
    uint8_t requestFrom(uint8_t address, size_t quantity);

    size_t write(uint8_t data);
    size_t write(const uint8_t * data, size_t quantity);

    virtual int available(void);
    virtual int read(void);
    virtual int peek(void);
    virtual void flush(void);

    void onReceive(void(*)(int));
    void onRequest(void(*)(void));
    void onService(void);

    uint32_t errorCount() const { return _errors; }      // NACKs, bus errors and timeouts
    uint32_t timeoutCount() const { return _timeouts; }
    bool busStuck() const { return _stuck; }             // a line still low after a recovery

    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n)          { return write((uint8_t)n); }
    inline size_t write(unsigned int n)  { return write((uint8_t)n); }
    inline size_t write(int n)           { return write((uint8_t)n); }
    using Print::write;

  private:
#if defined(NRF52) || defined(NRF52_SERIES)
    NRF_TWIM_Type * _p_twim;
    NRF_TWIS_Type * _p_twis;
#else
    NRF_TWI_Type * _p_twi;
#endif

    IRQn_Type _IRQn;

    uint8_t _uc_pinSDA;
    uint8_t _uc_pinSCL;

    bool master;
    bool receiving;
    bool transmissionBegun;
    bool suspended;

    // RX Buffer
    RingBuffer rxBuffer;

    // TX buffer
    RingBuffer txBuffer;
    uint8_t txAddress;

    // Callback user functions
    void (*onRequestCallback)(void);
    void (*onReceiveCallback)(int);

    volatile uint32_t _errors = 0;
    volatile uint32_t _timeouts = 0;
    bool _stuck = false;
    uint32_t _stuck_at = 0;

    void configPins();
    void clearEvents();
    bool waitFor(volatile uint32_t& ev, volatile uint32_t& or_ev);
    void freeBus();
    void recover();
    bool busReady();

    // TWI clock frequency
    static const uint32_t TWI_CLOCK = 100000;
};

#if WIRE_INTERFACES_COUNT > 0
extern TwoWire Wire;
#endif

#if WIRE_INTERFACES_COUNT > 1
extern TwoWire Wire1;
#endif

#endif
