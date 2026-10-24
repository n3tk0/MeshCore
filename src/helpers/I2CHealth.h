#pragma once

#include <Arduino.h>
#include <Wire.h>

// What is on the I2C bus, and which devices were turned off because they made it fail.
//
// On nRF52 our Wire (arch/nrf52/libs/nrf52_wire) has a timeout on every wait, frees a stuck bus and counts
// errors; a probe or a sensor read can then fail but not hang. The sensor manager turns off a
// device that timed out at boot, or failed 3 reads in a row, and keeps it off until a reboot,
// so one bad module does not take the others with it. The `i2c` CLI command lists them.
// Elsewhere Wire has its own timeouts and no counters: devices are only listed.

enum I2CProbeResult : uint8_t {
  I2C_PROBE_NACK = 0,
  I2C_PROBE_ACK = 1,
  I2C_PROBE_TIMEOUT = 2,
};

I2CProbeResult i2cProbe(TwoWire& wire, uint8_t addr);
uint32_t i2cErrorCount(TwoWire& wire);  // grows with every NACK, bus error or timeout
uint32_t i2cTimeoutCount(TwoWire& wire);
bool i2cBusStuck(TwoWire& wire);        // a line is still low after a recovery

enum I2CDeviceState : uint8_t {
  I2C_DEV_OK = 0,
  I2C_DEV_OFF_BOOT_TIMEOUT,   // its probe or its init timed out
  I2C_DEV_OFF_INIT_FAILED,    // it answered, its driver did not start
  I2C_DEV_OFF_ERRORS,         // 3 failed reads in a row
};

// The list `i2c` shows. With name NULL and I2C_DEV_OFF_BOOT_TIMEOUT (a probe that timed out,
// nothing known at that address) it is only counted
void i2cListDevice(uint8_t addr, const char* name, I2CDeviceState state);
void i2cSetBusStuck(TwoWire& wire);  // `i2c` then shows its state as it is now
// e.g. "40 INA219 ok; 76 BMP280 off: 3 errors at 125m", cut to fit
void i2cFormatList(char* reply, size_t size);
