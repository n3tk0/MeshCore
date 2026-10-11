#pragma once
// Simulator: no sensors, no GPS.
class SensorManager {
public:
  int getNumSettings() const { return 0; }
  const char* getSettingName(int) const { return ""; }
  const char* getSettingValue(int) const { return ""; }
  bool setSettingValue(const char*, const char*) { return false; }
};
