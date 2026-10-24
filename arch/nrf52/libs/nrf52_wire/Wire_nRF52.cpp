/*
 * TWI/I2C library for nRF5x
 * MeshCore: timeouts, bus recovery and error counters (see Wire.h)
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

#if defined(NRF52) || defined(NRF52_SERIES)

extern "C" {
#include <string.h>
}

#include <Arduino.h>
#include <wiring_private.h>

#include "Wire.h"

#include <Adafruit_TinyUSB.h> // for Serial

static volatile uint32_t* pincfg_reg(uint32_t pin)
{
  NRF_GPIO_Type * port = nrf_gpio_pin_port_decode(&pin);
  return &port->PIN_CNF[pin];
}

TwoWire::TwoWire(NRF_TWIM_Type * p_twim, NRF_TWIS_Type * p_twis, IRQn_Type IRQn, uint8_t pinSDA, uint8_t pinSCL)
{
  this->_p_twim = p_twim;
  this->_p_twis = p_twis;
  this->_IRQn = IRQn;
  this->_uc_pinSDA = g_ADigitalPinMap[pinSDA];
  this->_uc_pinSCL = g_ADigitalPinMap[pinSCL];
  transmissionBegun = false;
}

void TwoWire::configPins() {
  *pincfg_reg(_uc_pinSCL) = ((uint32_t)GPIO_PIN_CNF_DIR_Input         << GPIO_PIN_CNF_DIR_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect    << GPIO_PIN_CNF_INPUT_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup      << GPIO_PIN_CNF_PULL_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0D1       << GPIO_PIN_CNF_DRIVE_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);

  *pincfg_reg(_uc_pinSDA) = ((uint32_t)GPIO_PIN_CNF_DIR_Input         << GPIO_PIN_CNF_DIR_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect    << GPIO_PIN_CNF_INPUT_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_PULL_Pullup      << GPIO_PIN_CNF_PULL_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0D1       << GPIO_PIN_CNF_DRIVE_Pos)
                           | ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);
}

// MeshCore: an event left by an earlier transfer (an ERROR after a NACK) must not be read as
// this transfer's
void TwoWire::clearEvents() {
  _p_twim->EVENTS_TXSTARTED = 0x0UL;
  _p_twim->EVENTS_RXSTARTED = 0x0UL;
  _p_twim->EVENTS_LASTTX = 0x0UL;
  _p_twim->EVENTS_LASTRX = 0x0UL;
  _p_twim->EVENTS_STOPPED = 0x0UL;
  _p_twim->EVENTS_SUSPENDED = 0x0UL;
  _p_twim->EVENTS_ERROR = 0x0UL;
  _p_twim->ERRORSRC = _p_twim->ERRORSRC;  // write 1 to clear
}

bool TwoWire::waitFor(volatile uint32_t& ev, volatile uint32_t& or_ev) {
  uint32_t start = millis();
  while (!ev && !or_ev) {
    if (millis() - start >= WIRE_TIMEOUT_MS) return false;
  }
  return true;
}

// With the TWIM disabled (the pins are GPIO): 9 SCL clocks, all of them, so a device that was
// sending gets to the end of its byte and sees a NACK (stopping at the first high SDA could
// catch a 1 bit and the next clock would start the device's following 0), then a STOP.
// Lines still low after that: stuck.
void TwoWire::freeBus() {
  nrf_gpio_pin_set(_uc_pinSCL);
  nrf_gpio_pin_set(_uc_pinSDA);
  nrf_gpio_cfg(_uc_pinSCL, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_cfg(_uc_pinSDA, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  delayMicroseconds(10);
  for (int i = 0; i < 9; i++) {
    nrf_gpio_pin_clear(_uc_pinSCL);
    delayMicroseconds(5);
    nrf_gpio_pin_set(_uc_pinSCL);
    delayMicroseconds(5);
  }
  // STOP: SDA low to high while SCL is high
  nrf_gpio_pin_clear(_uc_pinSCL);
  delayMicroseconds(5);
  nrf_gpio_pin_clear(_uc_pinSDA);
  delayMicroseconds(5);
  nrf_gpio_pin_set(_uc_pinSCL);
  delayMicroseconds(5);
  nrf_gpio_pin_set(_uc_pinSDA);
  delayMicroseconds(10);
  _stuck = !nrf_gpio_pin_read(_uc_pinSDA) || !nrf_gpio_pin_read(_uc_pinSCL);
  _stuck_at = millis();
}

// The TWIM did not finish: reset it, free the bus, start it again
void TwoWire::recover() {
  _timeouts++;
  _errors++;
  uint32_t frequency = _p_twim->FREQUENCY;
  _p_twim->TASKS_STOP = 0x1UL;
  _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);  // the pins go back to GPIO
  // a TWIM stopped mid-transfer is only fully reset by a power cycle of the peripheral
  volatile uint32_t* power = (volatile uint32_t*)((uint8_t*)_p_twim + 0xFFC);
  *power = 0;
  *power;
  *power = 1;

  freeBus();

  configPins();
  _p_twim->FREQUENCY = frequency;
  _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  _p_twim->PSEL.SCL = _uc_pinSCL;
  _p_twim->PSEL.SDA = _uc_pinSDA;
  clearEvents();
}

// A stuck bus fails at once, without the timeout each time; every 10 s it is freed again.
// A TWIM not started (no begin(), or after end()) fails at once too.
bool TwoWire::busReady() {
  if (_p_twim->ENABLE != (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos)) return false;
  if (!_stuck) return true;
  if (millis() - _stuck_at < 10000) return false;
  _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  freeBus();
  configPins();
  _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  clearEvents();
  return !_stuck;
}

void TwoWire::begin(void) {
  //Main Mode
  master = true;

  configPins();

  _p_twim->FREQUENCY = TWIM_FREQUENCY_FREQUENCY_K100;
  _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  _p_twim->PSEL.SCL = _uc_pinSCL;
  _p_twim->PSEL.SDA = _uc_pinSDA;

  NVIC_ClearPendingIRQ(_IRQn);
  NVIC_SetPriority(_IRQn, 3);
  NVIC_EnableIRQ(_IRQn);
}

void TwoWire::begin(uint8_t address) {
  //Secondary mode
  master = false;

  *pincfg_reg(_uc_pinSCL) = ((uint32_t)GPIO_PIN_CNF_DIR_Input        << GPIO_PIN_CNF_DIR_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);

  *pincfg_reg(_uc_pinSDA) = ((uint32_t)GPIO_PIN_CNF_DIR_Input        << GPIO_PIN_CNF_DIR_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos)
                          | ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled   << GPIO_PIN_CNF_SENSE_Pos);

  _p_twis->ADDRESS[0] = address;
  _p_twis->CONFIG = TWIS_CONFIG_ADDRESS0_Msk;
  _p_twis->PSEL.SCL = _uc_pinSCL;
  _p_twis->PSEL.SDA = _uc_pinSDA;

  _p_twis->ORC = 0xff;

  _p_twis->INTENSET = TWIS_INTEN_STOPPED_Msk | TWIS_INTEN_ERROR_Msk | TWIS_INTEN_WRITE_Msk | TWIS_INTEN_READ_Msk;

  NVIC_ClearPendingIRQ(_IRQn);
  NVIC_SetPriority(_IRQn, 3);
  NVIC_EnableIRQ(_IRQn);

  _p_twis->ENABLE = (TWIS_ENABLE_ENABLE_Enabled << TWIS_ENABLE_ENABLE_Pos);
}

void TwoWire::setClock(uint32_t baudrate) {
  if (master) {
    _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);

    uint32_t frequency;

    if (baudrate <= 100000)
    {
      frequency = TWIM_FREQUENCY_FREQUENCY_K100;
    }
    else if (baudrate <= 250000)
    {
      frequency = TWIM_FREQUENCY_FREQUENCY_K250;
    }
    else
    {
      frequency = TWIM_FREQUENCY_FREQUENCY_K400;
    }

    _p_twim->FREQUENCY = frequency;
    _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  }
}

void TwoWire::setPins(uint8_t pinSDA, uint8_t pinSCL)
{
    this->_uc_pinSDA = g_ADigitalPinMap[pinSDA];
    this->_uc_pinSCL = g_ADigitalPinMap[pinSCL];
}

void TwoWire::end() {
  if (master)
  {
    _p_twim->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  }
  else
  {
    _p_twis->ENABLE = (TWIS_ENABLE_ENABLE_Disabled << TWIS_ENABLE_ENABLE_Pos);
  }
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool stopBit)
{
  if(quantity == 0)
  {
    return 0;
  }

  size_t byteRead = 0;
  rxBuffer.clear();

  if (!busReady()) { _errors++; return 0; }
  clearEvents();
  _p_twim->ADDRESS = address;

  _p_twim->TASKS_RESUME = 0x1UL;
  _p_twim->RXD.PTR = (uint32_t)rxBuffer._aucBuffer;
  _p_twim->RXD.MAXCNT = quantity;
  _p_twim->TASKS_STARTRX = 0x1UL;

  if (!waitFor(_p_twim->EVENTS_RXSTARTED, _p_twim->EVENTS_ERROR)) { recover(); return 0; }
  _p_twim->EVENTS_RXSTARTED = 0x0UL;

  if (!waitFor(_p_twim->EVENTS_LASTRX, _p_twim->EVENTS_ERROR)) { recover(); return 0; }
  _p_twim->EVENTS_LASTRX = 0x0UL;

  if (stopBit || _p_twim->EVENTS_ERROR)
  {
    _p_twim->TASKS_STOP = 0x1UL;
    if (!waitFor(_p_twim->EVENTS_STOPPED, _p_twim->EVENTS_STOPPED)) { recover(); return 0; }
    _p_twim->EVENTS_STOPPED = 0x0UL;
  }
  else
  {
    _p_twim->TASKS_SUSPEND = 0x1UL;
    if (!waitFor(_p_twim->EVENTS_SUSPENDED, _p_twim->EVENTS_SUSPENDED)) { recover(); return 0; }
    _p_twim->EVENTS_SUSPENDED = 0x0UL;
  }

  if (_p_twim->EVENTS_ERROR)
  {
    _p_twim->EVENTS_ERROR = 0x0UL;
    _p_twim->ERRORSRC = _p_twim->ERRORSRC;
    _errors++;
  }

  byteRead = rxBuffer._iHead = _p_twim->RXD.AMOUNT;

  return byteRead;
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity)
{
  return requestFrom(address, quantity, true);
}

void TwoWire::beginTransmission(uint8_t address) {
  // save address of target and clear buffer
  txAddress = address;
  txBuffer.clear();

  transmissionBegun = true;
}

// Errors:
//  0 : Success
//  1 : Data too long
//  2 : NACK on transmit of address
//  3 : NACK on transmit of data
//  4 : Other error
uint8_t TwoWire::endTransmission(bool stopBit)
{
  transmissionBegun = false ;

  if (!busReady()) { _errors++; return _stuck ? 5 : 4; }
  clearEvents();

  // Start I2C transmission
  _p_twim->ADDRESS = txAddress;

  // just in case twi is stopped by bus error such as secondary device reset/stalled without replying ACK/NACK
  _p_twim->EVENTS_STOPPED = 0x0UL;
  _p_twim->TASKS_RESUME = 0x1UL;

  _p_twim->TXD.PTR = (uint32_t)txBuffer._aucBuffer;
  _p_twim->TXD.MAXCNT = txBuffer.available();

  _p_twim->TASKS_STARTTX = 0x1UL;

  if (!waitFor(_p_twim->EVENTS_TXSTARTED, _p_twim->EVENTS_ERROR)) { recover(); return 5; }
  _p_twim->EVENTS_TXSTARTED = 0x0UL;

  if (txBuffer.available()) {
    if (!waitFor(_p_twim->EVENTS_LASTTX, _p_twim->EVENTS_ERROR)) { recover(); return 5; }
  }
  _p_twim->EVENTS_LASTTX = 0x0UL;

  if (stopBit || _p_twim->EVENTS_ERROR)
  {
    _p_twim->TASKS_STOP = 0x1UL;
    if (!waitFor(_p_twim->EVENTS_STOPPED, _p_twim->EVENTS_STOPPED)) { recover(); return 5; }
    _p_twim->EVENTS_STOPPED = 0x0UL;
  }
  else
  {
    _p_twim->TASKS_SUSPEND = 0x1UL;
    if (!waitFor(_p_twim->EVENTS_SUSPENDED, _p_twim->EVENTS_SUSPENDED)) { recover(); return 5; }
    _p_twim->EVENTS_SUSPENDED = 0x0UL;
  }

  if (_p_twim->EVENTS_ERROR)
  {
    _p_twim->EVENTS_ERROR = 0x0UL;
    _errors++;

    uint32_t error = _p_twim->ERRORSRC;

    _p_twim->ERRORSRC = error;

    if (error == TWIM_ERRORSRC_ANACK_Msk)
    {
      return 2;
    }
    else if (error == TWIM_ERRORSRC_DNACK_Msk)
    {
      return 3;
    }
    else
    {
      return 4;
    }
  }

  return 0;
}

uint8_t TwoWire::endTransmission()
{
  return endTransmission(true);
}

size_t TwoWire::write(uint8_t ucData)
{
  // No writing, without begun transmission or a full buffer
  if ( !transmissionBegun || txBuffer.isFull() )
  {
    return 0 ;
  }

  txBuffer.store_char( ucData ) ;

  return 1 ;
}

size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
  //Try to store all data
  for(size_t i = 0; i < quantity; ++i)
  {
    //Return the number of data stored, when the buffer is full (if write return 0)
    if(!write(data[i]))
      return i;
  }

  //All data stored
  return quantity;
}

int TwoWire::available(void)
{
  return rxBuffer.available();
}

int TwoWire::read(void)
{
  return rxBuffer.read_char();
}

int TwoWire::peek(void)
{
  return rxBuffer.peek();
}

void TwoWire::flush(void)
{
  // Do nothing, use endTransmission(..) to force
  // data transfer.
}

void TwoWire::onReceive(void(*function)(int))
{
  onReceiveCallback = function;
}

void TwoWire::onRequest(void(*function)(void))
{
  onRequestCallback = function;
}

void TwoWire::onService(void)
{
  if (_p_twis->EVENTS_WRITE)
  {
    _p_twis->EVENTS_WRITE = 0x0UL;

    receiving = true;

    rxBuffer.clear();

    _p_twis->RXD.PTR = (uint32_t)rxBuffer._aucBuffer;
    _p_twis->RXD.MAXCNT = sizeof(rxBuffer._aucBuffer);

    _p_twis->TASKS_PREPARERX = 0x1UL;
  }

  if (_p_twis->EVENTS_READ)
  {
    _p_twis->EVENTS_READ = 0x0UL;

    receiving = false;
    transmissionBegun = true;

    txBuffer.clear();

    if (onRequestCallback)
    {
      onRequestCallback();
    }

    transmissionBegun = false;

    _p_twis->TXD.PTR = (uint32_t)txBuffer._aucBuffer;
    _p_twis->TXD.MAXCNT = txBuffer.available();

    _p_twis->TASKS_PREPARETX = 0x1UL;
  }

  if (_p_twis->EVENTS_STOPPED)
  {
    _p_twis->EVENTS_STOPPED = 0x0UL;

    if (receiving)
    {
      int rxAmount = _p_twis->RXD.AMOUNT;

      rxBuffer._iHead = rxAmount;

      if (onReceiveCallback)
      {
        onReceiveCallback(rxAmount);
      }
    }
  }

  if (_p_twis->EVENTS_ERROR)
  {
    _p_twis->EVENTS_ERROR = 0x0UL;

    uint32_t error = _p_twis->ERRORSRC;
    _p_twis->ERRORSRC = error;

    _p_twis->TASKS_STOP = 0x1UL;
  }
}

#if WIRE_INTERFACES_COUNT > 0
TwoWire Wire(NRF_TWIM0, NRF_TWIS0, SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn, PIN_WIRE_SDA, PIN_WIRE_SCL);

extern "C"
{
  void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler(void)
  {
    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordEnterISR();
    #endif

    Wire.onService();

    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordExitISR();
    #endif
  }
}
#endif

#if WIRE_INTERFACES_COUNT > 1
TwoWire Wire1(NRF_TWIM1, NRF_TWIS1, SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn, PIN_WIRE1_SDA, PIN_WIRE1_SCL);

extern "C"
{
  void SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler(void)
  {
    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordEnterISR();
    #endif

    Wire1.onService();

    #if CFG_SYSVIEW
    SEGGER_SYSVIEW_RecordExitISR();
    #endif
  }
}
#endif

#endif // NRF52_SERIES
