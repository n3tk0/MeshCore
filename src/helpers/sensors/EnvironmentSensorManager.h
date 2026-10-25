#pragma once

#include <Wire.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

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
  bool         _ina219_found = false;   // its bus is taken as the battery

  void noteRead(void (*query)(uint8_t, uint8_t, CayenneLPP&), uint32_t errors_before);
  void retryOffSensors();
  void sampleBatteryGauge();

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
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
