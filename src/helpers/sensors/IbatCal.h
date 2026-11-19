#pragma once

#include <stdint.h>
#include <string.h>
#include <math.h>

// Calibration of the BQ25798 IBAT ADC (it has an offset and a gain error, which can differ between
// charging and discharging): corrected = raw * gain + offset, per direction, from up to two points.
//   low  point: |I| <  IBAT_LOW_MAX_MA   (the idle drain of the node: fixes the offset)
//   high point: |I| >= IBAT_HIGH_MIN_MA  (a charge or a TX burst: fixes the gain)
// With one point the fit is an offset only (gain 1). Points come from an INA219 in the battery line
// (auto: pairs of readings that both hold still, averaged) or by hand against a multimeter (manual:
// they override the auto point of their bin). Sign: + = charging the battery, - = discharging, for
// both the raw and the true value. The direction a value is corrected in is the sign of the raw value
// (0 counts as charging). Pure logic, no hardware: runs in the native unit tests.

#define IBAT_LOW_MAX_MA        60       // a point with |true| under this is the low point
#define IBAT_HIGH_MIN_MA       150      // ... at or over this the high point; in between: no point
#define IBAT_AUTO_MIN_N        12       // auto samples in a bin before it is used
#define IBAT_N_CAP             24       // running mean over this many samples (older ones fade out)
#define IBAT_GAIN_MIN          0.5f
#define IBAT_GAIN_MAX          1.5f
#define IBAT_OFFSET_MAX_MA     50       // |offset| over this: a nonsense fit
#define IBAT_MIN_SPAN_MA       60       // raw low and high points this far apart for a gain
#define IBAT_CAL_MAX_MA        3000     // values beyond this are no calibration points
#define IBAT_STABLE_ABS_MA     3        // two readings in a row within max(3 mA, 4 %) = holding still
#define IBAT_STABLE_PCT        4
#define IBAT_SAVE_FIRST_MS     (10 * 60000UL)   // the first save of an auto point: not before this after boot
#define IBAT_SAVE_PERIOD_MS    (60 * 60000UL)   // then at most this often (flash wear)
#define IBAT_SAVE_DELTA_X10    40       // a bin whose (true - raw) moved this much (0.1 mA) since the last save is saved again

// Build-time defaults (platformio.ini), used for a direction that has no stored point yet:
// offset in 0.1 mA, gain in 0.001. 0 / 1000 = no change.
#ifndef BQ25798_IBAT_CHG_OFFSET_X10
#define BQ25798_IBAT_CHG_OFFSET_X10   0
#endif
#ifndef BQ25798_IBAT_CHG_GAIN_X1000
#define BQ25798_IBAT_CHG_GAIN_X1000   1000
#endif
#ifndef BQ25798_IBAT_DIS_OFFSET_X10
#define BQ25798_IBAT_DIS_OFFSET_X10   0
#endif
#ifndef BQ25798_IBAT_DIS_GAIN_X1000
#define BQ25798_IBAT_DIS_GAIN_X1000   1000
#endif

#define IBAT_CHG               0        // direction index: raw >= 0
#define IBAT_DIS               1        // raw < 0
#define IBAT_LOW               0        // bin index
#define IBAT_HIGH              1

// One point: the mean of raw and true readings (0.1 mA), how many went into it. In the /charger file.
struct IbatBin {
  int16_t raw_x10;
  int16_t ref_x10;
  uint8_t n;        // samples (auto: up to IBAT_N_CAP), 0 = empty
  uint8_t manual;   // 1 = entered by hand (n = 1): auto readings leave it alone
};

struct IbatCalData {
  IbatBin bin[2][2];   // [IBAT_CHG|IBAT_DIS][IBAT_LOW|IBAT_HIGH]
};

struct IbatFit {
  float   gain;        // 1 = none
  float   offset_ma;
  uint8_t points;      // 0 (the build-time default), 1 (offset only) or 2
  uint8_t source;      // 0 default, 1 auto, 2 manual (a manual point among the ones used)
  bool    rejected;    // a fit was left out as nonsense (gain / offset out of range)
};

namespace IbatCal {

inline int absi(int v) { return v < 0 ? -v : v; }

inline int dirOf(int raw_ma) { return raw_ma >= 0 ? IBAT_CHG : IBAT_DIS; }

// the bin for a true current: 0 low, 1 high, -1 neither (60..149 mA), zero or beyond range
inline int binOf(int ref_ma) {
  int a = absi(ref_ma);
  if (a == 0 || a > IBAT_CAL_MAX_MA) return -1;
  return a < IBAT_LOW_MAX_MA ? IBAT_LOW : a >= IBAT_HIGH_MIN_MA ? IBAT_HIGH : -1;
}

inline bool usable(const IbatBin& b) { return b.n >= (b.manual ? 1 : IBAT_AUTO_MIN_N); }

inline bool empty(const IbatCalData& c) {
  for (int d = 0; d < 2; d++) for (int k = 0; k < 2; k++) if (c.bin[d][k].n) return false;
  return true;
}

inline bool fitOk(float gain, float off) {
  return gain >= IBAT_GAIN_MIN && gain <= IBAT_GAIN_MAX && off >= -IBAT_OFFSET_MAX_MA && off <= IBAT_OFFSET_MAX_MA;
}

// the fit for one direction from its usable points
inline IbatFit fit(const IbatCalData& c, int dir) {
  IbatFit f = { 1.0f, 0.0f, 0, 0, false };
  const IbatBin& lo = c.bin[dir][IBAT_LOW];
  const IbatBin& hi = c.bin[dir][IBAT_HIGH];
  bool l = usable(lo), h = usable(hi);
  if (dir == IBAT_CHG) { f.gain = BQ25798_IBAT_CHG_GAIN_X1000 / 1000.0f; f.offset_ma = BQ25798_IBAT_CHG_OFFSET_X10 / 10.0f; }
  else { f.gain = BQ25798_IBAT_DIS_GAIN_X1000 / 1000.0f; f.offset_ma = BQ25798_IBAT_DIS_OFFSET_X10 / 10.0f; }
  if (l && h) {
    float rl = lo.raw_x10 / 10.0f, fl = lo.ref_x10 / 10.0f;
    float rh = hi.raw_x10 / 10.0f, fh = hi.ref_x10 / 10.0f;
    float dr = rh - rl;
    if (dr >= IBAT_MIN_SPAN_MA || dr <= -IBAT_MIN_SPAN_MA) {   // never a division by (nearly) zero
      float g = (fh - fl) / dr;
      float o = fl - g * rl;
      if (fitOk(g, o)) { f.gain = g; f.offset_ma = o; f.points = 2; f.source = (lo.manual || hi.manual) ? 2 : 1; return f; }
    }
    f.rejected = true;   // the two points do not make a sane line: the low one alone (below)
    h = false;
  }
  if (l || h) {
    const IbatBin& b = l ? lo : hi;
    float o = (b.ref_x10 - b.raw_x10) / 10.0f;
    if (fitOk(1.0f, o)) { f.gain = 1.0f; f.offset_ma = o; f.points = 1; f.source = b.manual ? 2 : 1; }
    else f.rejected = true;
  }
  return f;
}

// raw -> corrected, mA
inline int16_t apply(const IbatCalData& c, int16_t raw_ma) {
  IbatFit f = fit(c, dirOf(raw_ma));
  float v = raw_ma * f.gain + f.offset_ma;
  v = v < 0 ? v - 0.5f : v + 0.5f;
  return v > 32000 ? 32000 : v < -32000 ? -32000 : (int16_t)v;
}

// one auto pair into its bin (the bin of the true value; the raw one has to have the same sign).
// false: not used (no direction, zero, middle zone, out of range, the bin is a manual one).
inline bool addAuto(IbatCalData& c, int raw_ma, int ref_ma) {
  if (raw_ma == 0 || ref_ma == 0 || (raw_ma > 0) != (ref_ma > 0)) return false;
  if (absi(raw_ma) > IBAT_CAL_MAX_MA) return false;
  int k = binOf(ref_ma);
  if (k < 0) return false;
  IbatBin& b = c.bin[dirOf(ref_ma)][k];
  if (b.manual) return false;
  int32_t rx = raw_ma * 10, fx = ref_ma * 10;
  if (b.n < IBAT_N_CAP) b.n++;
  int32_t n = b.n;
  if (n == 1) { b.raw_x10 = (int16_t)rx; b.ref_x10 = (int16_t)fx; return true; }
  // mean += (x - mean) / n, rounded to the nearest
  int32_t dr = rx - b.raw_x10, df = fx - b.ref_x10;
  b.raw_x10 = (int16_t)(b.raw_x10 + (dr >= 0 ? (dr + n / 2) / n : -((-dr + n / 2) / n)));
  b.ref_x10 = (int16_t)(b.ref_x10 + (df >= 0 ? (df + n / 2) / n : -((-df + n / 2) / n)));
  return true;
}

// codes of setManual()
#define IBAT_SET_OK         0
#define IBAT_SET_ZERO      -2   // the true or the raw value is 0 (no direction), or out of range
#define IBAT_SET_DIRECTION -3   // raw and true value of different sign
#define IBAT_SET_NONSENSE  -4   // the fit would be out of range: not taken
#define IBAT_SET_ZONE      -5   // |true| between 60 and 149 mA: neither low nor high

// a point by hand: the raw reading against the true current; replaces the bin (auto or manual)
inline int setManual(IbatCalData& c, int raw_ma, int ref_ma) {
  if (raw_ma == 0 || ref_ma == 0 || absi(ref_ma) > IBAT_CAL_MAX_MA || absi(raw_ma) > IBAT_CAL_MAX_MA) return IBAT_SET_ZERO;
  if ((raw_ma > 0) != (ref_ma > 0)) return IBAT_SET_DIRECTION;
  int k = binOf(ref_ma);
  if (k < 0) return IBAT_SET_ZONE;
  IbatCalData t = c;
  IbatBin& b = t.bin[dirOf(ref_ma)][k];
  b.raw_x10 = (int16_t)(raw_ma * 10);
  b.ref_x10 = (int16_t)(ref_ma * 10);
  b.n = 1;
  b.manual = 1;
  if (fit(t, dirOf(ref_ma)).rejected) return IBAT_SET_NONSENSE;
  c = t;
  return IBAT_SET_OK;
}

// a file's calibration: bins that cannot be right (direction, range, flags) are emptied
inline void sanitize(IbatCalData& c) {
  for (int d = 0; d < 2; d++) {
    for (int k = 0; k < 2; k++) {
      IbatBin& b = c.bin[d][k];
      bool ok = b.n > 0 && b.n <= IBAT_N_CAP && b.manual <= 1 && (!b.manual || b.n == 1);
      if (ok) {
        int raw = b.raw_x10, ref = b.ref_x10;
        ok = raw != 0 && ref != 0 && (raw > 0) == (d == IBAT_CHG) && (ref > 0) == (d == IBAT_CHG) &&
             absi(raw) <= IBAT_CAL_MAX_MA * 10 && absi(ref) <= IBAT_CAL_MAX_MA * 10 &&
             binOf(ref / 10) == k;
      }
      if (!ok) memset(&b, 0, sizeof(b));
    }
  }
}

}  // namespace IbatCal

// The auto mode: pairs (raw, true) that both hold still go into the bins; tells when the file should
// be written again. It does not write: the owner of the IbatCalData does that.
class IbatLearner {
  bool     _have;
  int      _prev_raw, _prev_ref;
  bool     _dirty;
  uint32_t _next_save;
  IbatCalData _saved;   // what the file holds

  static bool holdsStill(int a, int b) {
    int d = IbatCal::absi(a - b);
    int m = IbatCal::absi(a) > IbatCal::absi(b) ? IbatCal::absi(a) : IbatCal::absi(b);
    int tol = m * IBAT_STABLE_PCT / 100;
    return d <= (tol > IBAT_STABLE_ABS_MA ? tol : IBAT_STABLE_ABS_MA);
  }

public:
  IbatLearner() { memset(&_saved, 0, sizeof(_saved)); _have = false; _prev_raw = _prev_ref = 0; _dirty = false; _next_save = IBAT_SAVE_FIRST_MS; }

  // a gap in the readings (the reference is gone): the next pair has no predecessor
  void gap() { _have = false; }

  // one pair; true when it went into a bin
  bool feed(IbatCalData& c, int raw_ma, int ref_ma) {
    bool stable = _have && holdsStill(raw_ma, _prev_raw) && holdsStill(ref_ma, _prev_ref);
    _prev_raw = raw_ma;
    _prev_ref = ref_ma;
    _have = true;
    if (!stable || !IbatCal::addAuto(c, raw_ma, ref_ma)) return false;
    // worth a write: a bin that just became usable, or one that moved a lot since the last write
    int d = IbatCal::dirOf(ref_ma), k = IbatCal::binOf(ref_ma);
    const IbatBin& now = c.bin[d][k];
    const IbatBin& was = _saved.bin[d][k];
    if (IbatCal::usable(now)) {
      if (!IbatCal::usable(was)) _dirty = true;
      else if (IbatCal::absi((now.ref_x10 - now.raw_x10) - (was.ref_x10 - was.raw_x10)) > IBAT_SAVE_DELTA_X10) _dirty = true;
    }
    return true;
  }

  bool saveDue(uint32_t now) const { return _dirty && (int32_t)(now - _next_save) >= 0; }
  bool dirty() const { return _dirty; }

  // the file now holds `c`: written by the owner (written = true: the next write waits a period),
  // or just loaded
  void synced(const IbatCalData& c, uint32_t now, bool written) {
    _saved = c;
    _dirty = false;
    if (written) _next_save = now + IBAT_SAVE_PERIOD_MS;
  }
};
