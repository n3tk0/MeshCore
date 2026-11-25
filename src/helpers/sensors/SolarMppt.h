#pragma once

#include <stdint.h>
#include <math.h>

// Panel voltage set-point control for a CN3791 buck MPPT charger whose MPPT pin is fed, besides the
// 330k/100k divider, from a PWM pin (10k + 1uF RC filter, 200k injection resistor):
//
//   Vset = Vref * (1 + Rtop/Rbot) + (Vref - Vp) * Rtop/Rinj,   Vp = duty * Vpwm
//
// The pin as input without pull (hi-Z) leaves the divider alone: Vset = Vref * (1 + Rtop/Rbot) (5.18 V
// with the defaults). Duty 0 gives the highest set-point (~7.17 V), a higher duty a lower one.
//
// This class is the logic only (no pins, no I2C): the caller measures the panel (voltage, current) and
// drives the pin from driven() / duty(). It is a perturb & observe tracker on the panel power.
#ifndef SOLAR_MPPT_R_TOP
#define SOLAR_MPPT_R_TOP   330000
#endif
#ifndef SOLAR_MPPT_R_BOT
#define SOLAR_MPPT_R_BOT   100000
#endif
#ifndef SOLAR_MPPT_R_INJ
#define SOLAR_MPPT_R_INJ   200000
#endif
#ifndef SOLAR_MPPT_VREF
#define SOLAR_MPPT_VREF    1.205f   // the CN3791's MPPT pin regulation voltage
#endif
#ifndef SOLAR_MPPT_VPWM
#define SOLAR_MPPT_VPWM    3.3f     // the PWM pin's high level
#endif
#ifndef SOLAR_MPPT_V_MIN
#define SOLAR_MPPT_V_MIN   4.6f     // the set-point stays in this range
#endif
#ifndef SOLAR_MPPT_V_MAX
#define SOLAR_MPPT_V_MAX   6.9f
#endif

#define SOLAR_MPPT_MODE_AUTO   0
#define SOLAR_MPPT_MODE_OFF    1
#define SOLAR_MPPT_MODE_FIXED  2

#define SOLAR_MPPT_STEP_MS        10000UL   // between two steps of the tracker
#define SOLAR_MPPT_RETRY_MS       60000UL   // between two looks at the panel while the pin is released
#define SOLAR_MPPT_STEP_DUTY      0.02f     // about 0.11 V of the set-point
#define SOLAR_MPPT_MIN_W          0.05f     // under this the panel gives nothing: no sun
#define SOLAR_MPPT_START_W        0.10f     // released: tracking starts again above this ...
#define SOLAR_MPPT_START_A        0.020f    // ... and this
#define SOLAR_MPPT_DONE_A         0.010f    // under this the charger is done (or there is no sun)
#define SOLAR_MPPT_BATT_MARGIN_V  0.10f     // panel under battery + this: no sun

class SolarMppt {
public:
  enum State : uint8_t { ST_OFF, ST_FIXED, ST_TRACKING, ST_RELEASED, ST_NO_SENSOR };

  // ---- the set-point <-> duty maths ----
  static float defaultSetpoint() {
    return SOLAR_MPPT_VREF * (1.0f + (float)SOLAR_MPPT_R_TOP / (float)SOLAR_MPPT_R_BOT);
  }
  static float setpointForDuty(float duty) {
    return defaultSetpoint() + (SOLAR_MPPT_VREF - duty * SOLAR_MPPT_VPWM) * (float)SOLAR_MPPT_R_TOP / (float)SOLAR_MPPT_R_INJ;
  }
  static float clampSetpoint(float v) {
    if (!(v >= SOLAR_MPPT_V_MIN)) return SOLAR_MPPT_V_MIN;   // also NaN
    return v > SOLAR_MPPT_V_MAX ? SOLAR_MPPT_V_MAX : v;
  }
  static float dutyForSetpoint(float v) {
    v = clampSetpoint(v);
    float d = (SOLAR_MPPT_VREF - (v - defaultSetpoint()) * (float)SOLAR_MPPT_R_INJ / (float)SOLAR_MPPT_R_TOP) / SOLAR_MPPT_VPWM;
    return clampDuty(d);
  }
  // the duties that keep the set-point in its range: the highest set-point is the lowest duty
  static float minDuty() { return rawDuty(SOLAR_MPPT_V_MAX); }
  static float maxDuty() { return rawDuty(SOLAR_MPPT_V_MIN); }
  static float clampDuty(float d) {
    if (!(d >= minDuty())) return minDuty();   // also NaN
    return d > maxDuty() ? maxDuty() : d;
  }
  // the duty that equals the pin released
  static float defaultDuty() { return clampDuty(SOLAR_MPPT_VREF / SOLAR_MPPT_VPWM); }

  // ---- mode (persisted by the caller) ----
  uint8_t mode() const { return _mode; }
  float fixedVolts() const { return _fixed_v; }
  void setAuto() { _mode = SOLAR_MPPT_MODE_AUTO; resetTracker(); }
  void setOff() { _mode = SOLAR_MPPT_MODE_OFF; _driven = false; }
  void setFixed(float volts) {
    _mode = SOLAR_MPPT_MODE_FIXED;
    _fixed_v = clampSetpoint(volts);
    _duty = dutyForSetpoint(_fixed_v);
    _driven = true;
  }
  // from the saved file: [mode][volts in mV, 2 bytes little endian]
  static const int FILE_LEN = 3;
  void toFile(uint8_t* out) const {
    uint16_t mv = (uint16_t)lroundf(_fixed_v * 1000.0f);
    out[0] = _mode;
    out[1] = (uint8_t)(mv & 0xFF);
    out[2] = (uint8_t)(mv >> 8);
  }
  bool fromFile(const uint8_t* in, int len) {
    if (len < FILE_LEN || in[0] > SOLAR_MPPT_MODE_FIXED) return false;
    float v = ((uint16_t)(in[1] | (in[2] << 8))) / 1000.0f;
    if (in[0] == SOLAR_MPPT_MODE_FIXED && (v < SOLAR_MPPT_V_MIN - 0.001f || v > SOLAR_MPPT_V_MAX + 0.001f)) return false;
    if (v >= SOLAR_MPPT_V_MIN) _fixed_v = clampSetpoint(v);
    if (in[0] == SOLAR_MPPT_MODE_OFF) setOff();
    else if (in[0] == SOLAR_MPPT_MODE_FIXED) setFixed(_fixed_v);
    else setAuto();
    return true;
  }

  // ---- what the pin does now ----
  bool driven() const { return _driven; }   // false: the pin is an input without pull (hi-Z)
  float duty() const { return _duty; }      // 0..1, when driven
  uint8_t pwm8() const { return (uint8_t)lroundf(_duty * 255.0f); }
  float setpoint() const { return _driven ? setpointForDuty(_duty) : defaultSetpoint(); }
  State state() const {
    if (_mode == SOLAR_MPPT_MODE_OFF) return ST_OFF;
    if (_mode == SOLAR_MPPT_MODE_FIXED) return ST_FIXED;
    if (_no_sensor) return ST_NO_SENSOR;
    return _driven ? ST_TRACKING : ST_RELEASED;
  }
  static const char* stateName(State s) {
    switch (s) {
      case ST_OFF: return "off";
      case ST_FIXED: return "fixed";
      case ST_TRACKING: return "tracking";
      case ST_NO_SENSOR: return "released (no panel sensor)";
      default: return "released";
    }
  }
  const char* releaseReason() const { return _reason; }

  // the last panel reading (see update())
  bool hasPanel() const { return _have_panel; }
  float panelV() const { return _pv; }
  float panelA() const { return _pa; }
  float panelW() const { return _pv * _pa; }

  // ---- timing ----
  // true when the caller should measure the panel and call update() (millis wrap safe)
  bool due(uint32_t now) const { return _due_now || (int32_t)(now - _next_ms) >= 0; }

  // The panel could not be read (no sensor, off, I2C errors): in auto the pin is released.
  void panelUnavailable(uint32_t now, bool no_sensor) {
    _have_panel = false;
    _no_sensor = no_sensor;
    _due_now = false;
    _next_ms = now + (_mode == SOLAR_MPPT_MODE_AUTO && _driven ? SOLAR_MPPT_STEP_MS : SOLAR_MPPT_RETRY_MS);
    if (_mode == SOLAR_MPPT_MODE_AUTO) release("panel not readable");
  }

  // One measurement of the panel (volts and amps at the charger input) and the battery (0: not known).
  void update(uint32_t now, float panel_v, float panel_a, float batt_v) {
    _no_sensor = false;
    _due_now = false;
    if (!(panel_v >= 0.0f) || !(panel_a > -1000.0f)) {   // NaN
      panelUnavailable(now, false);
      return;
    }
    _pv = panel_v;
    _pa = panel_a < 0 ? 0 : panel_a;
    _have_panel = true;
    float p = _pv * _pa;
    if (_mode != SOLAR_MPPT_MODE_AUTO) {
      _next_ms = now + SOLAR_MPPT_RETRY_MS;   // only for the display
      return;
    }

    bool no_sun = p < SOLAR_MPPT_MIN_W || (batt_v > 0 && _pv < batt_v + SOLAR_MPPT_BATT_MARGIN_V);
    bool done = _pa < SOLAR_MPPT_DONE_A;
    if (_driven) {
      if (no_sun || done) {
        release(no_sun ? "no sun" : "charge done");
        _next_ms = now + SOLAR_MPPT_RETRY_MS;
        return;
      }
      if (_have_last) {
        if (p < _last_p * 0.995f) _dir = -_dir;   // it got worse: the other way
      }
      _last_p = p;
      _have_last = true;
      float d = _duty + _dir * SOLAR_MPPT_STEP_DUTY;
      float c = clampDuty(d);
      if (c != d) _dir = -_dir;   // at the end of the range: back
      _duty = c;
      _next_ms = now + SOLAR_MPPT_STEP_MS;
      return;
    }

    // released: sun and a charge going on -> take over at the point the divider gave
    if (!no_sun && p >= SOLAR_MPPT_START_W && _pa >= SOLAR_MPPT_START_A) {
      _driven = true;
      _duty = defaultDuty();
      _last_p = p;      // the power at the default point is the first reference
      _have_last = true;
      _reason = "";
      _next_ms = now + SOLAR_MPPT_STEP_MS;
      // the first step goes on in the last direction; the next measurement turns it round when it is worse
      float c = clampDuty(_duty + _dir * SOLAR_MPPT_STEP_DUTY);
      _duty = c;
    } else {
      _reason = no_sun ? "no sun" : "charge done";
      _next_ms = now + SOLAR_MPPT_RETRY_MS;
    }
  }

private:
  static float rawDuty(float v) {
    return (SOLAR_MPPT_VREF - (v - defaultSetpoint()) * (float)SOLAR_MPPT_R_INJ / (float)SOLAR_MPPT_R_TOP) / SOLAR_MPPT_VPWM;
  }
  void release(const char* why) {
    _driven = false;
    _have_last = false;
    _reason = why;
  }
  void resetTracker() {
    _driven = false;
    _have_last = false;
    _dir = 1;
    _due_now = true;   // look at the panel at once
    _reason = "";
  }

  uint8_t _mode = SOLAR_MPPT_MODE_AUTO;
  float _fixed_v = 5.5f;
  bool _driven = false;
  float _duty = 0;
  int8_t _dir = 1;
  bool _have_last = false;
  float _last_p = 0;
  uint32_t _next_ms = 0;
  bool _due_now = true;   // first look at once (and after a mode change: no millis() here)
  bool _have_panel = false;
  bool _no_sensor = false;
  float _pv = 0, _pa = 0;
  const char* _reason = "";
};
