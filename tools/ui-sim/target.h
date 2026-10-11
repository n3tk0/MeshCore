#pragma once

// Desktop screen simulator: the hardware of the Faketec companion, driven by the scenario.

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/ui/RotaryInput.h>
#include <helpers/SensorManager.h>

#define BUTTON_EVENT_NONE        0
#define BUTTON_EVENT_CLICK       1
#define BUTTON_EVENT_LONG_PRESS  2

class SimButton {
  int _queue[16];
  int _n = 0;
public:
  void begin() { }
  void setDebounce(int) { }
  void wakeOnChange() { }
  void end() { }
  void push(int ev) { if (_n < 16) _queue[_n++] = ev; }
  int check() {
    if (_n == 0) return BUTTON_EVENT_NONE;
    int ev = _queue[0];
    memmove(_queue, _queue + 1, sizeof(int) * --_n);
    return ev;
  }
};

class SimRotary : public RotaryInput {
  int _pending = 0;   // + next, - prev
public:
  bool begin() override { return true; }
  bool isReady() const override { return true; }
  void turn(int n) { _pending += n; }
  RotaryInputEvent poll() override {
    if (_pending > 0) { _pending--; return RotaryInputEvent::Next; }
    if (_pending < 0) { _pending++; return RotaryInputEvent::Prev; }
    return RotaryInputEvent::None;
  }
};

class SimBoard : public mesh::MainBoard {
public:
  uint16_t mv = 3920;
  bool external = false;
  uint16_t getBattMilliVolts() override { return mv; }
  const char* getManufacturerName() const override { return "Faketec sim"; }
  void reboot() override { }
  void powerOff() override { }
  uint8_t getStartupReason() const override { return 0; }
  bool isExternalPowered() override { return external; }
};

class SimRadio {
public:
  uint32_t recv = 0;
  float snr = 6.5f;
  uint32_t getPacketsRecv() const { return recv; }
  float getLastSNR() const { return snr; }
  int getNoiseFloor() const { return -112; }
};

class SimClock : public mesh::RTCClock {
public:
  uint32_t base = 0;   // epoch at sim_millis == 0, 0 = not set
  uint32_t getCurrentTime() override { return base ? base + (uint32_t)(millis() / 1000) : 1715770351UL + millis() / 1000; }
  void setCurrentTime(uint32_t t) override { base = t - millis() / 1000; }
};

extern SimBoard board;
extern SimRadio radio_driver;
extern SimClock rtc_clock;
extern SensorManager sensors;
extern SimButton user_btn;
extern SimButton encoder_btn;
extern SimRotary sim_rotary;
extern RotaryInput& rotary_input;
