#pragma once

#include <Wire.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// INA226_BATTERY=1: the primary INA226 (0x44) is the battery (voltage and charge gauge); else it is telemetry only.
// INA226_LOW_POWER=1: INA226s are read in triggered mode with averaging (they rest at ~2 uA in between).
#ifndef INA226_BATTERY
#define INA226_BATTERY 0
#endif
#ifndef INA226_LOW_POWER
#define INA226_LOW_POWER 0
#endif
// SOLAR_BOARD_AUTO=1: one firmware for the XIAO solar boards, the board is found at boot (see begin()):
// BQ25798 on 0x6B = xiao-solar-lite v0.5 (D0 = ~CE), INA226 on 0x44 and 0x45 = xiao-solar-lite v0.6 (CN3791,
// D0 = MPPT PWM, 0x44 = battery), MSPM0 on 0x42 = xiao-solar / solar-smart-mppt. Build with the flags of all
// of them (ENV_INCLUDE_BQ25798, ENV_INCLUDE_INA226, INA226_BATTERY, SOLAR_MPPT_PWM_PIN, BQ25798_PIN_CE ...):
// INA226_BATTERY and the PWM then apply only on the board that has them.
#ifndef SOLAR_BOARD_AUTO
#define SOLAR_BOARD_AUTO 0
#endif
#define SOLAR_BOARD_NONE     0
#define SOLAR_BOARD_BQ25798  1
#define SOLAR_BOARD_CN3791   2
#define SOLAR_BOARD_MSPM0    3
const char* solarBoardName(uint8_t b);   // "none" | "bq25798" | "cn3791" | "mspm0" | "?"

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t (*init)(TwoWire* wire, uint8_t address);   // again when it comes back (a power glitch resets its setup)
    uint8_t   sub_channel;
    uint8_t   address;
    const char* name;
    uint8_t   fails;    // reads in a row that made I2C errors
    bool      off;      // turned off after 3 of them; tried again later (retry_at)
    uint8_t   retries;  // tries since it was turned off: 10 min, 1 h, then every 6 h (back to 10 min after a good day)
    uint32_t  retry_at; // millis; while on again after a retry: when it came back
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;
  #if ENV_INCLUDE_BQ25798
  bool bqActive() const;
  void bqLoop();
  #endif

  void noteRead(void (*query)(uint8_t, uint8_t, CayenneLPP&), uint32_t errors_before);
  void retryOffSensors();
  void sampleBatteryGauge();
  bool readIna226Battery(uint16_t* mv, float* ma);
  void mpptLoop();
  uint32_t gaugeDay() const;

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  bool isGPSDetected() const override { return gps_detected; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  uint16_t getBatteryMilliVolts() override;
  void loop() override;
  BatteryGauge* getBatteryGauge() override;
  #ifdef SOLAR_MPPT_PWM_PIN
  SolarMppt* getSolarMppt() override;
  #endif
  #if ENV_INCLUDE_BQ25798
  BQ25798* getCharger() override;
  #endif
  #if ENV_INCLUDE_BQ25798 || defined(SOLAR_MPPT_PWM_PIN)
  int externalPower() override;
  #endif
  uint8_t solarBoardDetected() const override;
  uint8_t solarBoardActive() const override;
  void solarBoardLock(uint8_t stored) override;
  bool batteryOnlyFromSensor() const override;
  // the battery INA226 (0x44) now: mV and mA, + = charging; false: not the battery sensor here, or no read
  bool inaBatteryNow(uint16_t* mv, int* ma);
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
