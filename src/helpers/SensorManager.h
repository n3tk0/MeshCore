#pragma once

#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"
#include "BatteryGauge.h"

class BQ25798;
class SolarMppt;

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual const char* getSettingValue(int i) const { return NULL; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }
  virtual bool isGPSDetected() const { return false; }
  // the battery as a sensor measures it (e.g. an INA219 in the battery line), 0: none does, the board's own reading counts
  virtual uint16_t getBatteryMilliVolts() { return 0; }
  // counts the battery's charge when a sensor measures its current (INA219); NULL: none does
  virtual BatteryGauge* getBatteryGauge() { return NULL; }
  // a charger chip on I2C (BQ25798 on xiao-solar-lite); NULL: none
  virtual BQ25798* getCharger() { return NULL; }
  // the panel voltage set-point control of a CN3791 MPPT charger (SOLAR_MPPT_PWM_PIN); NULL: none
  virtual SolarMppt* getSolarMppt() { return NULL; }
  // 1 / 0: a charger chip sees a panel or adapter (or not); -1: no such chip, ask the board
  virtual int externalPower() { return -1; }
  // Solar board (SOLAR_BOARD_AUTO builds): what the I2C scan found (SOLAR_BOARD_*), and what runs after
  // solarBoardLock(stored) with the board kept in /board (0: none kept yet): the CN3791 PWM on D0 runs only
  // when the kept board agrees, since on a BQ25798 board D0 is the charger's ~CE.
  virtual uint8_t solarBoardDetected() const { return 0; }
  virtual uint8_t solarBoardActive() const { return 0; }
  virtual void solarBoardLock(uint8_t stored) { (void)stored; }
  // true: the battery is read only from a sensor (the XIAO's BAT pad is open there): 0 means unknown,
  // the board's own ADC (noise) is not taken instead
  virtual bool batteryOnlyFromSensor() const { return false; }
  // the gauge's 'today' follows this clock when it is set (else days since boot)
  void setDayClock(uint32_t (*now)()) { day_clock = now; }
protected:
  uint32_t (*day_clock)() = NULL;
public:

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
