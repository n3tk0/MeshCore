#pragma once

#include <stdint.h>
#include <math.h>
#include <stdio.h>

// Panel voltage set-point control for a CN3791 buck MPPT charger whose MPPT pin is fed, besides the
// 330k/100k divider, from a PWM pin (10k + 1uF RC filter, 200k injection resistor: 210k in series):
//
//   Vset = Vref * (1 + Rtop/Rbot) + (Vref - Vp) * Rtop/Rinj,   Vp = duty * Vpwm
//
// The pin as input without pull (hi-Z) leaves the divider alone: Vset = Vref * (1 + Rtop/Rbot) (5.18 V
// with the defaults). Duty 0 gives the highest set-point (~7.08 V), a higher duty a lower one.
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
#define SOLAR_MPPT_R_INJ   210000   // xiao-solar-ultralite v0.2: R6 200k + the RC filter's R7 10k in series
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

// charge current limit (`charger ichg`): the CN3791 sets its own maximum with the sense resistor (120 mV / RCS);
// a lower limit is kept by moving the panel point up (off the power peak) while the battery takes more
#ifndef SOLAR_MPPT_ICHG_HW_MA
#define SOLAR_MPPT_ICHG_HW_MA     600       // xiao-solar-lite v0.6: 120 mV / (2 x 100 mOhm in series)
#endif
#define SOLAR_MPPT_ICHG_MIN_MA    100
#define SOLAR_MPPT_LIMIT_HYST_MA  30        // limited: P&O again only under limit - this
#define SOLAR_MPPT_LIMIT_FAR_MA   50        // over limit + this: big steps
#define SOLAR_MPPT_LIMIT_STEP     0.01f     // ~0.055 V of the set-point, near the limit

// scan (`charger scan`): the set-point from the top down in steps, the power at each, then P&O from the best
#define SOLAR_MPPT_SCAN_MS        2000UL    // per point
#define SOLAR_MPPT_SCAN_STEP_V    0.2f
#define SOLAR_MPPT_SCAN_POINTS    12        // 6.9, 6.7 .. 4.7 V

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
  void setOff() { _mode = SOLAR_MPPT_MODE_OFF; _driven = false; endLimitAndScan(); }
  void setFixed(float volts) {
    endLimitAndScan();
    _mode = SOLAR_MPPT_MODE_FIXED;
    _fixed_v = clampSetpoint(volts);
    _duty = dutyForSetpoint(_fixed_v);
    _driven = true;
  }
  // charge current limit, mA: SOLAR_MPPT_ICHG_HW_MA = none (the CN3791's own); kept only in auto mode
  uint16_t ichg() const { return _ichg_ma; }
  void setIchg(int ma) {
    if (ma < SOLAR_MPPT_ICHG_MIN_MA) ma = SOLAR_MPPT_ICHG_MIN_MA;
    if (ma > SOLAR_MPPT_ICHG_HW_MA) ma = SOLAR_MPPT_ICHG_HW_MA;
    _ichg_ma = (uint16_t)(ma / 10 * 10);
    if (!limitSet()) { _limiting = false; _limit_stuck = false; }
  }
  bool limitSet() const { return _ichg_ma < SOLAR_MPPT_ICHG_HW_MA; }
  bool limiting() const { return _limiting; }        // the set-point is held up for the limit now
  bool limitStuck() const { return _limit_stuck; }   // at the highest set-point and still over the limit
  // the battery current is wanted with each update() (an extra read): a limit is set and could act
  bool wantsBatteryCurrent() const { return limitSet() && _mode == SOLAR_MPPT_MODE_AUTO && _driven && !_scanning; }

  // from the saved file: [mode][volts in mV, 2 bytes LE][ichg mA, 2 bytes LE]; the old 3-byte file has no limit
  static const int FILE_LEN = 5;
  static const int FILE_LEN_OLD = 3;
  void toFile(uint8_t* out) const {
    uint16_t mv = (uint16_t)lroundf(_fixed_v * 1000.0f);
    out[0] = _mode;
    out[1] = (uint8_t)(mv & 0xFF);
    out[2] = (uint8_t)(mv >> 8);
    out[3] = (uint8_t)(_ichg_ma & 0xFF);
    out[4] = (uint8_t)(_ichg_ma >> 8);
  }
  bool fromFile(const uint8_t* in, int len) {
    if (len < FILE_LEN_OLD || in[0] > SOLAR_MPPT_MODE_FIXED) return false;
    uint16_t ma = len >= FILE_LEN ? (uint16_t)(in[3] | (in[4] << 8)) : SOLAR_MPPT_ICHG_HW_MA;
    if (ma < SOLAR_MPPT_ICHG_MIN_MA || ma > SOLAR_MPPT_ICHG_HW_MA) return false;
    float v = ((uint16_t)(in[1] | (in[2] << 8))) / 1000.0f;
    if (in[0] == SOLAR_MPPT_MODE_FIXED && (v < SOLAR_MPPT_V_MIN - 0.001f || v > SOLAR_MPPT_V_MAX + 0.001f)) return false;
    setIchg(ma);
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
    endLimitAndScan();
    _no_sensor = no_sensor;
    _due_now = false;
    _next_ms = now + (_mode == SOLAR_MPPT_MODE_AUTO && _driven ? SOLAR_MPPT_STEP_MS : SOLAR_MPPT_RETRY_MS);
    if (_mode == SOLAR_MPPT_MODE_AUTO) release("panel not readable");
  }

  // ---- scan ----
  // false: not tracking now (auto, pin driven, a panel reading) or a scan is already running
  bool scanStart(uint32_t now) {
    if (_mode != SOLAR_MPPT_MODE_AUTO || !_driven || !_have_panel || _scanning) return false;
    _scanning = true;
    _scan_i = 0;
    _scan_have = false;
    _limiting = false;
    _limit_stuck = false;
    _duty = dutyForSetpoint(scanVolts(0));
    _due_now = false;
    _next_ms = now + SOLAR_MPPT_SCAN_MS;
    return true;
  }
  bool scanning() const { return _scanning; }
  // "scan 5/12 ..." / "scan: best 5.70V 1.23W; 6.9:0.40 6.7:0.81 ..." / "scan: none yet"
  int formatScan(char* out, int max) const {
    int n;
    if (_scanning) n = snprintf(out, max, "scan %d/%d, ~%ds left", _scan_i + 1, SOLAR_MPPT_SCAN_POINTS,
                                (int)((SOLAR_MPPT_SCAN_POINTS - _scan_i) * SOLAR_MPPT_SCAN_MS / 1000));
    else if (!_scan_have) return snprintf(out, max, "scan: none yet");
    else n = snprintf(out, max, "scan: best %d.%02dV %d.%02dW;", x100i(_scan_best_v), x100f(_scan_best_v),
                      _scan_mw[_scan_best_i] / 1000, _scan_mw[_scan_best_i] % 1000 / 10);
    int pts = _scanning ? _scan_i : SOLAR_MPPT_SCAN_POINTS;
    for (int i = 0; i < pts && n > 0 && n < max; i++) {
      int dv = (int)lroundf(scanVolts(i) * 10.0f);
      n += snprintf(out + n, max - n, " %d.%d:%d.%02d", dv / 10, dv % 10, _scan_mw[i] / 1000, _scan_mw[i] % 1000 / 10);
    }
    return n < max ? n : max - 1;
  }
  static float scanVolts(int i) { return SOLAR_MPPT_V_MAX - SOLAR_MPPT_SCAN_STEP_V * i; }

  // One measurement of the panel (volts and amps at the charger input) and the battery (volts, 0: not known;
  // amps, + = charging, NaN: not read).
  void update(uint32_t now, float panel_v, float panel_a, float batt_v, float batt_a = NAN) {
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

    if (_scanning) {   // no release while scanning: the top points may be above the panel's open voltage
      scanStep(now, p);
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
      if (limitSet() && batt_a == batt_a && limitStep(now, batt_a)) return;   // batt_a NaN: not read, P&O
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
    endLimitAndScan();
  }
  void resetTracker() {
    _driven = false;
    _have_last = false;
    _dir = 1;
    _due_now = true;   // look at the panel at once
    _reason = "";
    endLimitAndScan();
  }
  void endLimitAndScan() {
    _scanning = false;
    _limiting = false;
    _limit_stuck = false;
  }
  // true: the limit decided this step (the set-point went up, or is held); false: P&O as usual
  bool limitStep(uint32_t now, float batt_a) {
    float lim = _ichg_ma / 1000.0f;
    if (batt_a > lim) {
      float step = batt_a > lim + SOLAR_MPPT_LIMIT_FAR_MA / 1000.0f ? SOLAR_MPPT_STEP_DUTY : SOLAR_MPPT_LIMIT_STEP;
      float c = clampDuty(_duty - step);   // a lower duty is a higher set-point: off the peak, less current
      _limit_stuck = c == _duty;
      _duty = c;
    } else if (!(_limiting && batt_a > lim - SOLAR_MPPT_LIMIT_HYST_MA / 1000.0f)) {
      if (_limiting) { _dir = 1; _have_last = false; }   // back to P&O, first towards the peak (lower volts)
      _limiting = false;
      _limit_stuck = false;
      return false;
    }
    _limiting = true;
    _have_last = false;
    _next_ms = now + SOLAR_MPPT_STEP_MS;
    return true;
  }
  void scanStep(uint32_t now, float p) {
    float mw = p * 1000.0f;
    _scan_mw[_scan_i] = (uint16_t)(mw > 65535.0f ? 65535.0f : mw);
    if (++_scan_i < SOLAR_MPPT_SCAN_POINTS) {
      _duty = dutyForSetpoint(scanVolts(_scan_i));
      _next_ms = now + SOLAR_MPPT_SCAN_MS;
      return;
    }
    _scan_best_i = 0;
    for (int i = 1; i < SOLAR_MPPT_SCAN_POINTS; i++) if (_scan_mw[i] > _scan_mw[_scan_best_i]) _scan_best_i = i;
    _scan_best_v = scanVolts(_scan_best_i);
    _scanning = false;
    _scan_have = true;
    _duty = dutyForSetpoint(_scan_best_v);   // P&O from the best point
    _have_last = false;
    _dir = 1;
    _next_ms = now + SOLAR_MPPT_STEP_MS;
  }
  static int x100i(float v) { return (int)lroundf(v * 100.0f) / 100; }
  static int x100f(float v) { return (int)lroundf(v * 100.0f) % 100; }

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
  uint16_t _ichg_ma = SOLAR_MPPT_ICHG_HW_MA;
  bool _limiting = false, _limit_stuck = false;
  bool _scanning = false, _scan_have = false;
  uint8_t _scan_i = 0, _scan_best_i = 0;
  float _scan_best_v = 0;
  uint16_t _scan_mw[SOLAR_MPPT_SCAN_POINTS] = {0};
};
