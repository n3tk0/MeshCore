#pragma once

#include <stdint.h>

// A coulomb counter for a battery with a current sensor in its line (an INA219: battery on Vin+,
// load and charger on Vin-, so a positive current is the battery discharging).
//
// The voltage of a LiPo says little while it is loaded or on a solar charger; counting the charge
// that goes in and out does not. The gauge needs to know the capacity: it is set by hand
// (`battery capacity <mAh>`), or learnt from a full charge down to empty. It knows it is full when
// the cell sits at the top voltage with only a trickle going in. Before either, the percentage comes
// from the voltage as before. Pure logic: the caller feeds it readings and keeps the capacity.

#define GAUGE_FULL_MV          4150    // at or above, with only a trickle going in = full
#define GAUGE_EMPTY_MV         3300    // at or below while discharging = empty (learning ends here)
#define GAUGE_FULL_TRICKLE_MA    15    // charge current below this (or capacity / 20) at the top = full
#define GAUGE_FULL_SECS         120    // ... for this long
#define GAUGE_MAX_GAP_MS     600000    // a reading this long after the last one is not integrated
#define GAUGE_AVG_SECS        21600    // the discharge average behind 'hours left' (6 h)

// LiPo resting voltage -> percent (rough: under load the cell reads lower)
static inline int gaugeVoltagePercent(uint16_t mv) {
  static const uint16_t curve[11] = { 3300, 3600, 3690, 3740, 3770, 3790, 3820, 3870, 3920, 4000, 4150 };
  if (mv <= curve[0]) return 0;
  if (mv >= curve[10]) return 100;
  for (int i = 1; i < 11; i++) {
    if (mv < curve[i]) return (i - 1) * 10 + (int)(mv - curve[i - 1]) * 10 / (curve[i] - curve[i - 1]);
  }
  return 100;
}

class BatteryGauge {
  uint16_t _capacity;      // mAh, 0 = unknown
  bool     _learnt;        // _capacity came from a full charge down to empty
  float    _charge;        // mAh in the cell, < 0 = unknown
  bool     _since_full_valid;
  float    _since_full;    // net mAh out since the last full charge
  float    _in_today, _out_today, _in_yday, _out_yday;   // mAh
  uint32_t _day;           // day number the 'today' counters belong to
  float    _avg_ma;        // net discharge, averaged
  bool     _have_avg;
  uint32_t _last_ms;
  bool     _have_last;
  uint32_t _full_for_ms;   // how long the 'full' condition has held
  uint16_t _last_mv;
  float    _last_ma;
  bool     _capacity_changed;

public:
  BatteryGauge() { reset(); }

  void reset() {
    _capacity = 0; _learnt = false;
    _charge = -1; _since_full_valid = false; _since_full = 0;
    _in_today = _out_today = _in_yday = _out_yday = 0;
    _day = 0; _avg_ma = 0; _have_avg = false;
    _last_ms = 0; _have_last = false; _full_for_ms = 0; _last_mv = 0; _last_ma = 0;
    _capacity_changed = false;
  }

  // a capacity set by hand, or one kept from an earlier learning (learnt = true)
  void setCapacity(uint16_t mah, bool learnt = false) {
    _capacity = mah;
    _learnt = learnt && mah > 0;
    if (_capacity == 0) _charge = -1;
    else if (_charge > _capacity) _charge = _capacity;
  }
  uint16_t capacity() const { return _capacity; }
  bool capacityLearnt() const { return _learnt; }
  // true once after the gauge learnt a new capacity (the caller saves it)
  bool takeCapacityChanged() { bool c = _capacity_changed; _capacity_changed = false; return c; }

  // A reading: the battery voltage and its current (positive = out of the battery).
  // 'day' is a day number (e.g. RTC seconds / 86400); the daily counters roll over when it changes.
  void update(uint32_t now_ms, uint16_t mv, float discharge_ma, uint32_t day) {
    if (mv < 2000 || mv > 5000) return;   // not a battery reading
    if (day != _day) {
      if (_day != 0) { _in_yday = _in_today; _out_yday = _out_today; }
      _in_today = _out_today = 0;
      _day = day;
    }
    _last_mv = mv;
    _last_ma = discharge_ma;
    if (_charge < 0 && _capacity > 0) _charge = (float)_capacity * gaugeVoltagePercent(mv) / 100.0f;

    if (_have_last) {
      uint32_t dt = now_ms - _last_ms;
      if (dt > 0 && dt <= GAUGE_MAX_GAP_MS) {
        float mah = discharge_ma * (float)dt / 3600000.0f;
        if (mah > 0) _out_today += mah; else _in_today -= mah;
        if (_charge >= 0) {
          _charge -= mah;
          if (_charge < 0) _charge = 0;
          if (_capacity > 0 && _charge > _capacity) _charge = _capacity;
        }
        if (_since_full_valid) _since_full += mah;

        float k = (float)dt / 1000.0f / GAUGE_AVG_SECS;
        if (k > 1) k = 1;
        _avg_ma = _have_avg ? _avg_ma + (discharge_ma - _avg_ma) * k : discharge_ma;
        _have_avg = true;

        // full: top voltage and only a trickle going in, for a while
        float trickle = _capacity > 0 && _capacity / 20 > GAUGE_FULL_TRICKLE_MA ? _capacity / 20 : GAUGE_FULL_TRICKLE_MA;
        if (mv >= GAUGE_FULL_MV && discharge_ma <= 0 && -discharge_ma < trickle) {
          _full_for_ms += dt;
          if (_full_for_ms >= GAUGE_FULL_SECS * 1000UL) {
            if (_capacity > 0) _charge = _capacity;
            _since_full_valid = true;
            _since_full = 0;
          }
        } else {
          _full_for_ms = 0;
        }

        // empty after a full charge: what came out is the capacity
        if (mv <= GAUGE_EMPTY_MV && discharge_ma > 0 && _since_full_valid) {
          if (_since_full > 100 && _since_full < 60000) {
            uint16_t cap = (uint16_t)(_since_full + 0.5f);
            if (cap != _capacity || !_learnt) _capacity_changed = true;
            _capacity = cap;
            _learnt = true;
            _charge = 0;
          }
          _since_full_valid = false;
        }
      }
    }
    _last_ms = now_ms;
    _have_last = true;
  }

  // 0..100; from the counted charge when the capacity is known, else from the voltage; -1 before any reading
  int percent() const {
    if (_capacity > 0 && _charge >= 0) {
      int p = (int)(_charge * 100.0f / _capacity + 0.5f);
      return p < 0 ? 0 : p > 100 ? 100 : p;
    }
    return _last_mv ? gaugeVoltagePercent(_last_mv) : -1;
  }
  bool fromCount() const { return _capacity > 0 && _charge >= 0; }

  // hours until empty at the average discharge; -1 unknown, -2 charging on average
  int hoursLeft() const {
    if (!_have_avg) return -1;
    if (_avg_ma < -1.0f) return -2;
    if (_avg_ma < 0.5f || _capacity == 0 || _charge < 0) return -1;
    float h = _charge / _avg_ma;
    return h > 9999 ? 9999 : (int)h;
  }
  float averageMilliAmps() const { return _avg_ma; }
  float lastMilliAmps() const { return _last_ma; }
  bool hasReading() const { return _have_last; }
  int inToday() const { return (int)(_in_today + 0.5f); }
  int outToday() const { return (int)(_out_today + 0.5f); }
  int inYesterday() const { return (int)(_in_yday + 0.5f); }
  int outYesterday() const { return (int)(_out_yday + 0.5f); }
};
