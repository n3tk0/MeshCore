#pragma once

#include <Arduino.h>
#include <Wire.h>

// Is anything at this I2C address? A probe that cannot hang the boot.
//
// On nRF52 the Adafruit Wire driver waits for the TWIM events with no timeout, and it does not
// clear an ERROR left by an earlier transfer before starting a new one. A loose cable, an
// unpowered module or a line held low is then enough to stop a repeater in setup() for good.
// This probe drives the TWIM itself with a timeout, leaves its events clear for Wire, and once
// the bus has failed it says so for every later probe, so the callers go on without I2C.
// Elsewhere it is Wire's own probe (ESP32 and STM32 Wire have timeouts).

enum I2CProbeResult : uint8_t {
  I2C_PROBE_NACK = 0,
  I2C_PROBE_ACK = 1,
  I2C_PROBE_STUCK = 2,
};

#ifdef NRF52_PLATFORM

inline bool& i2cBusStuck() {
  static bool stuck = false;
  return stuck;
}

inline NRF_TWIM_Type* i2cTwimOf(TwoWire& wire) {
  if (&wire == &Wire) return NRF_TWIM0;
#if WIRE_INTERFACES_COUNT > 1
  if (&wire == &Wire1) return NRF_TWIM1;
#endif
  return NULL;
}

inline void i2cTwimClearEvents(NRF_TWIM_Type* t) {
  t->EVENTS_TXSTARTED = 0;
  t->EVENTS_LASTTX = 0;
  t->EVENTS_STOPPED = 0;
  t->EVENTS_ERROR = 0;
  t->EVENTS_SUSPENDED = 0;
  t->ERRORSRC = t->ERRORSRC;  // write 1 to clear
}

inline I2CProbeResult i2cProbe(TwoWire& wire, uint8_t addr, uint32_t timeout_ms = 50) {
  if (i2cBusStuck()) return I2C_PROBE_STUCK;
  NRF_TWIM_Type* t = i2cTwimOf(wire);
  if (t == NULL) {
    wire.beginTransmission(addr);
    return wire.endTransmission() == 0 ? I2C_PROBE_ACK : I2C_PROBE_NACK;
  }
  if (t->ENABLE != (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos)) return I2C_PROBE_NACK;  // no Wire.begin()

  static uint8_t none[1];
  i2cTwimClearEvents(t);
  t->ADDRESS = addr;
  t->TXD.PTR = (uint32_t)none;
  t->TXD.MAXCNT = 0;  // the address only
  t->TASKS_RESUME = 1;
  t->TASKS_STARTTX = 1;
  uint32_t start = millis();
  while (!t->EVENTS_TXSTARTED && !t->EVENTS_ERROR && millis() - start < timeout_ms) { }
  bool started = t->EVENTS_TXSTARTED || t->EVENTS_ERROR;
  t->TASKS_STOP = 1;
  start = millis();
  while (!t->EVENTS_STOPPED && millis() - start < timeout_ms) { }
  bool stopped = t->EVENTS_STOPPED;
  bool ack = started && stopped && !t->EVENTS_ERROR;

  if (!started || !stopped) {
    // SCL held low, or the TWIM never let go: reset the peripheral so it is not left mid-transfer
    t->ENABLE = TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos;
    t->ENABLE = TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos;
    i2cTwimClearEvents(t);
    i2cBusStuck() = true;
    Serial.printf("I2C bus stuck at 0x%02X: going on without I2C devices\n", addr);
    return I2C_PROBE_STUCK;
  }
  i2cTwimClearEvents(t);  // Wire reads a stale ERROR as its own
  return ack ? I2C_PROBE_ACK : I2C_PROBE_NACK;
}

#else

inline bool& i2cBusStuck() {
  static bool stuck = false;
  return stuck;
}

inline I2CProbeResult i2cProbe(TwoWire& wire, uint8_t addr, uint32_t timeout_ms = 50) {
  (void)timeout_ms;
  wire.beginTransmission(addr);
  return wire.endTransmission() == 0 ? I2C_PROBE_ACK : I2C_PROBE_NACK;
}

#endif
