#pragma once

#include <stdint.h>
#include <string.h>
#include <math.h>

// Calibration of the BQ25798 IBAT ADC (offset and gain error, different when charging and when
// discharging) as a piecewise-linear curve per direction. Sign: + = charging the battery, - =
// discharging, for the raw and the true value. The direction a value is corrected in is the sign of
// the raw value (0 counts as charging).
//
// Points: fixed nominal |I|, charge 5, 10, 20, 50, 100, 200, 500, 1000, 2000 mA; discharge 3, 5, 8, 12, 20, 35, 60, 120 mA. Each one keeps the mean raw
// BQ value and the mean true value of the samples whose TRUE |I| falls in its bucket (edges = geometric
// midpoints: charge 7.1, 14.1, 31.6, 70.7, 141, 316, 707, 1414 mA; discharge 3.9, 6.3, 9.8, 15.5, 26.5, 45.8, 84.9 mA; the first point takes everything under, the last everything over).
// Sources of a point: auto (INA219 reference, stable pairs, running mean over IBAT_N_CAP samples, used
// from IBAT_AUTO_MIN_N samples, or IBAT_AUTO_MIN_N_HIGH from point index 6 up: charge 500 mA+, discharge 60 / 120 mA: rare), manual (by hand
// against a multimeter, n = 1, overrides auto), build (BQ25798_IBAT_{CHG,DIS}_CURVE in platformio.ini,
// a fallback for a bucket without a stored usable point).
//
// Correction of a raw reading: linear between the two neighbouring usable points (by raw magnitude).
// Outside the covered range: the line through the nearest end point with the gain of the two-point fit
// (low point under 60 mA + high point from 150 mA; one point only = gain 1, offset only); with no usable
// point at all the build offset + gain (BQ25798_IBAT_*_OFFSET_X10 / _GAIN_X1000). Anchoring the line at
// the end point keeps the result continuous. A point is ignored when it is nonsense (|true - raw| over
// max(50 mA, 15 % of true), or true/raw outside 0.5..1.5 from 50 mA) or when it breaks the order (raw magnitude not
// growing with the true one); in a conflict a manual point wins over an auto one, else the lower one
// stays. Pure logic, no hardware: runs in the native unit tests.
//
// The same code calibrates the IBUS ADC (the solar input, only positive): an IbatCurve whose CHG half
// is the curve (points 5 .. 2000 mA as the IBAT charge side, same limits and sample counts), the DIS
// half stays empty; the *Bus variants below. Its reference is an INA219 moved to the solar input
// (`charger ina sun`), its file is /ibuscal (magic "IBU", else the same layout as /ibatcal).

#define IBAT_NP                9
#define IBAT_AUTO_MIN_N        12       // auto samples in a point before it is used (5 .. 200 mA)
#define IBAT_AUTO_MIN_N_HIGH   6        // ... from point index IBAT_HIGH_FROM (500 mA up: rare)
#define IBAT_HIGH_FROM         6
#define IBAT_N_CAP             24       // running mean over this many samples (older ones fade out)
#define IBAT_GAIN_MIN          0.5f
#define IBAT_GAIN_MAX          1.5f
#define IBAT_OFFSET_MAX_MA     50       // |true - raw| over this: a nonsense point / fit
#define IBAT_RATIO_FROM_MA     50       // the ratio check applies from this true |I|
#define IBAT_LOW_MAX_MA        60       // two-point fit (extrapolation): low point = true |I| under this
#define IBAT_HIGH_MIN_MA       150      // ... high point = true |I| from this
#define IBAT_MIN_SPAN_MA       60       // raw low and high points this far apart for a gain
#define IBAT_CAL_MAX_MA        3000     // values beyond this are no calibration points
#define IBAT_STABLE_ABS_MA     3        // two readings in a row within max(3 mA, 4 %) = holding still
#define IBAT_STABLE_PCT        4
#define IBAT_SAVE_FIRST_MS     (10 * 60000UL)   // the first save of an auto point: not before this after boot
#define IBAT_SAVE_PERIOD_MS    (60 * 60000UL)   // then at most this often (flash wear)
#define IBAT_SAVE_DELTA_X10    40       // a point whose (true - raw) moved this much (0.1 mA, or 3 % of it, if more) since the last save is saved again

// Build-time defaults (platformio.ini), used for a direction that has no usable point:
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
// A default curve per direction: "raw:true,raw:true,..." in mA (one decimal allowed, sign = direction),
// e.g. -D BQ25798_IBAT_DIS_CURVE='"-24:-12,-60:-48,-230:-200"'
#ifndef BQ25798_IBAT_CHG_CURVE
#define BQ25798_IBAT_CHG_CURVE   ""
#endif
#ifndef BQ25798_IBAT_DIS_CURVE
#define BQ25798_IBAT_DIS_CURVE   ""
#endif
// ... and a second part of it (`charger ibat export <dir> 2`: the points from index 5 up), same format
#ifndef BQ25798_IBAT_CHG_CURVE2
#define BQ25798_IBAT_CHG_CURVE2  ""
#endif
#ifndef BQ25798_IBAT_DIS_CURVE2
#define BQ25798_IBAT_DIS_CURVE2  ""
#endif

// The IBUS curve defaults (`charger ibus export [2]`): "raw:true,..." in mA, positive, same format as the IBAT charge one
#ifndef BQ25798_IBUS_CURVE
#define BQ25798_IBUS_CURVE       ""
#endif
#ifndef BQ25798_IBUS_CURVE2
#define BQ25798_IBUS_CURVE2      ""
#endif

#define IBAT_CHG               0        // direction index: raw >= 0
#define IBAT_DIS               1        // raw < 0

#define IBAT_SRC_AUTO          0
#define IBAT_SRC_MANUAL        1
#define IBAT_SRC_BUILD         2        // only in the build default curve, never in the file

// One point: the mean of raw and true readings (0.1 mA, signed), how many went into it.
struct IbatPoint {
  int16_t raw_x10;
  int16_t ref_x10;
  uint8_t n;        // samples (auto: up to IBAT_N_CAP; manual / build: 1), 0 = empty
  uint8_t src;      // IBAT_SRC_*
};

struct IbatCurve {
  IbatPoint p[2][IBAT_NP];   // [IBAT_CHG|IBAT_DIS][point]
};

// The calibration of the build 934d2da (two bins per direction), in the /charger file. Only read now:
// imported into the curve when there is no /ibatcal yet.
struct IbatLegacyBin {
  int16_t raw_x10;
  int16_t ref_x10;
  uint8_t n;
  uint8_t manual;
};
struct IbatLegacyCal {
  IbatLegacyBin bin[2][2];   // [dir][0 low (true under 60 mA) | 1 high (from 150 mA)]
};

struct IbatFit {
  float   gain;        // 1 = none
  float   offset_ma;   // signed: corrected = raw * gain + offset (the BQ25798_IBAT_*_OFFSET_X10 / 10)
  uint8_t points;      // 0 (the build-time default), 1 (offset only) or 2
  uint8_t source;      // 0 default, 1 auto, 2 manual, 3 build curve (the "strongest" source among the points)
  bool    rejected;    // a two-point fit was left out as nonsense (gain / offset out of range)
};

// The points of one direction that count, in order of growing |I| (magnitudes, 0.1 mA).
struct IbatEff {
  uint8_t m;                    // accepted points
  int32_t raw[IBAT_NP];         // |raw|
  int32_t ref[IBAT_NP];         // |true|
  uint8_t src[IBAT_NP];
  uint8_t idx[IBAT_NP];         // nominal point index
  uint16_t present;             // bit i: point i is usable
  uint16_t ignored;             // bit i: usable but left out (nonsense or out of order)
};

namespace IbatCal {

// nominal |I| per direction: the node's discharge is a few tens of mA (idle 10-15 mA; TX bursts are too
// short to pass the stability filter), the charge goes up to the amps
static const int16_t PT_MA[2][IBAT_NP] = { { 5, 10, 20, 50, 100, 200, 500, 1000, 2000 },
                                           { 3, 5, 8, 12, 20, 35, 60, 120, 0 } };   // [IBAT_DIS][8] unused
static const int NPTS[2] = { 9, 8 };
// bucket edges (geometric midpoints), 0.1 mA: true |I| under edge[i] (and from edge[i-1]) belongs to point i
static const int32_t EDGE_X10[2][IBAT_NP - 1] = { { 71, 141, 316, 707, 1414, 3162, 7071, 14142 },
                                                  { 39, 63, 98, 155, 265, 458, 849, 0 } };

inline int absi(int v) { return v < 0 ? -v : v; }

inline int dirOf(int raw_ma) { return raw_ma >= 0 ? IBAT_CHG : IBAT_DIS; }

// the point for a true |I| in 0.1 mA: 0..8, or -1 for 0 and beyond IBAT_CAL_MAX_MA
inline int bucketX10(int32_t a, int dir) {
  if (a <= 0 || a > IBAT_CAL_MAX_MA * 10) return -1;
  int i = 0;
  while (i < NPTS[dir] - 1 && a >= EDGE_X10[dir][i]) i++;
  return i;
}
inline int bucketOf(int ref_ma) { return bucketX10((int32_t)absi(ref_ma) * 10, dirOf(ref_ma)); }

inline bool usable(const IbatPoint& b, int idx) {
  return b.n >= (b.src != IBAT_SRC_AUTO ? 1 : (idx >= IBAT_HIGH_FROM ? IBAT_AUTO_MIN_N_HIGH : IBAT_AUTO_MIN_N));
}

inline bool empty(const IbatCurve& c) {
  for (int d = 0; d < 2; d++) for (int i = 0; i < IBAT_NP; i++) if (c.p[d][i].n) return false;
  return true;
}

inline int count(const IbatCurve& c, int dir) {   // stored points of a direction (usable or not)
  int n = 0;
  for (int i = 0; i < IBAT_NP; i++) if (c.p[dir][i].n) n++;
  return n;
}

inline bool fitOk(float gain, float off) {
  return gain >= IBAT_GAIN_MIN && gain <= IBAT_GAIN_MAX && off >= -IBAT_OFFSET_MAX_MA && off <= IBAT_OFFSET_MAX_MA;
}

// a point on its own: raw and true |I| (0.1 mA) are sane
inline bool pointSane(int32_t raw, int32_t ref) {
  if (raw <= 0 || ref <= 0) return false;
  int32_t d = ref - raw;
  int32_t lim = ref * 15 / 100;   // the allowed difference grows with the current: max(50 mA, 15 % of true)
  if (lim < IBAT_OFFSET_MAX_MA * 10) lim = IBAT_OFFSET_MAX_MA * 10;
  if ((d < 0 ? -d : d) > lim) return false;
  if (ref >= IBAT_RATIO_FROM_MA * 10) {   // ratio true/raw in 0.5..1.5 (integers: no division)
    if (ref * 2 < raw || ref > raw * 3 / 2) return false;
  }
  return true;
}

// the point of a direction as it counts: a usable stored one, else the build default one
inline const IbatPoint* chosen(const IbatCurve& st, const IbatCurve& def, int dir, int i) {
  if (usable(st.p[dir][i], i)) return &st.p[dir][i];
  if (def.p[dir][i].n) return &def.p[dir][i];
  return 0;
}

inline IbatEff build(const IbatCurve& st, const IbatCurve& def, int dir) {
  IbatEff e;
  memset(&e, 0, sizeof(e));
  for (int i = 0; i < NPTS[dir]; i++) {
    const IbatPoint* p = chosen(st, def, dir, i);
    if (!p) continue;
    e.present |= (uint16_t)(1u << i);
    int32_t raw = p->raw_x10 < 0 ? -(int32_t)p->raw_x10 : p->raw_x10;
    int32_t ref = p->ref_x10 < 0 ? -(int32_t)p->ref_x10 : p->ref_x10;
    if (!pointSane(raw, ref)) { e.ignored |= (uint16_t)(1u << i); continue; }
    // order: the raw magnitude has to grow with the true one (the true ones grow with the index)
    bool dropped = false;
    while (e.m > 0 && raw <= e.raw[e.m - 1]) {
      if (p->src == IBAT_SRC_MANUAL && e.src[e.m - 1] != IBAT_SRC_MANUAL) {   // the manual one wins over a lower auto / build one
        e.ignored |= (uint16_t)(1u << e.idx[e.m - 1]);
        e.m--;
      } else { dropped = true; break; }
    }
    if (dropped) { e.ignored |= (uint16_t)(1u << i); continue; }
    e.raw[e.m] = raw; e.ref[e.m] = ref; e.src[e.m] = p->src; e.idx[e.m] = (uint8_t)i; e.m++;
  }
  return e;
}

// the line outside the covered range (and the whole line without points): from the accepted points of a
// direction, else the build default
inline IbatFit fit(const IbatEff& e, int dir, bool bus = false) {
  IbatFit f = { 1.0f, 0.0f, 0, 0, false };
  if (e.m == 0) {
    if (bus) return f;   // IBUS: no build-time offset / gain, only the curve
    if (dir == IBAT_CHG) { f.gain = BQ25798_IBAT_CHG_GAIN_X1000 / 1000.0f; f.offset_ma = BQ25798_IBAT_CHG_OFFSET_X10 / 10.0f; }
    else { f.gain = BQ25798_IBAT_DIS_GAIN_X1000 / 1000.0f; f.offset_ma = BQ25798_IBAT_DIS_OFFSET_X10 / 10.0f; }
    return f;
  }
  int lo = -1, hi = -1;
  for (int k = 0; k < e.m; k++) {
    if (e.ref[k] < IBAT_LOW_MAX_MA * 10 && lo < 0) lo = k;
    if (e.ref[k] >= IBAT_HIGH_MIN_MA * 10) hi = k;
  }
  int sgn = dir == IBAT_CHG ? 1 : -1;
  int single = lo >= 0 ? lo : hi >= 0 ? hi : 0;
  int best = 1;   // source: the strongest among the points: manual 2 > auto 1 > build 3 reported as 3 only when all are build
  bool anyManual = false, anyAuto = false;
  for (int k = 0; k < e.m; k++) { if (e.src[k] == IBAT_SRC_MANUAL) anyManual = true; else if (e.src[k] == IBAT_SRC_AUTO) anyAuto = true; }
  best = anyManual ? 2 : anyAuto ? 1 : 3;
  if (lo >= 0 && hi >= 0) {
    float dr = (e.raw[hi] - e.raw[lo]) / 10.0f;
    if (dr >= IBAT_MIN_SPAN_MA) {   // magnitudes grow, so dr > 0; never a division by (nearly) zero
      float g = ((e.ref[hi] - e.ref[lo]) / 10.0f) / dr;
      float o = e.ref[lo] / 10.0f - g * (e.raw[lo] / 10.0f);   // magnitude space
      if (fitOk(g, o)) { f.gain = g; f.offset_ma = sgn * o; f.points = 2; f.source = (uint8_t)best; return f; }
    }
    f.rejected = true;
  }
  float o = (e.ref[single] - e.raw[single]) / 10.0f;
  f.gain = 1.0f; f.offset_ma = sgn * o; f.points = 1; f.source = (uint8_t)best;
  return f;
}

// raw -> corrected, mA, in the direction `dir` (bus: the IBUS curve, no build offset / gain, never negative)
inline int16_t applyDir(const IbatCurve& st, const IbatCurve& def, int16_t raw_ma, int dir, bool bus) {
  IbatEff e = build(st, def, dir);
  IbatFit f = fit(e, dir, bus);
  float v;
  if (e.m == 0) {
    v = raw_ma * f.gain + f.offset_ma;
  } else {
    float a = (float)absi(raw_ma);   // |raw|, mA
    float t;                          // |corrected|
    float r0 = e.raw[0] / 10.0f, r1 = e.raw[e.m - 1] / 10.0f;
    if (a <= r0) t = e.ref[0] / 10.0f + (a - r0) * f.gain;
    else if (a >= r1) t = e.ref[e.m - 1] / 10.0f + (a - r1) * f.gain;
    else {
      int k = 0;
      while (k < e.m - 2 && a >= e.raw[k + 1] / 10.0f) k++;
      float ra = e.raw[k] / 10.0f, rb = e.raw[k + 1] / 10.0f;   // rb > ra: the order is enforced by build()
      float ta = e.ref[k] / 10.0f, tb = e.ref[k + 1] / 10.0f;
      t = ta + (tb - ta) * (a - ra) / (rb - ra);
    }
    v = dir == IBAT_CHG ? t : -t;
  }
  if (bus && v < 0) v = 0;
  v = v < 0 ? v - 0.5f : v + 0.5f;
  return v > 32000 ? 32000 : v < -32000 ? -32000 : (int16_t)v;
}

// raw -> corrected, mA
inline int16_t apply(const IbatCurve& st, const IbatCurve& def, int16_t raw_ma) {
  return applyDir(st, def, raw_ma, dirOf(raw_ma), false);
}

// IBUS raw -> corrected, mA: a reading of 0 or less (nothing flows, or the ADC's offset) is left as it is
inline int16_t applyBus(const IbatCurve& st, const IbatCurve& def, int16_t raw_ma) {
  return raw_ma <= 0 ? raw_ma : applyDir(st, def, raw_ma, IBAT_CHG, true);
}

// one auto pair into its point (the one of the true value; the raw one has to have the same sign).
// false: not used (no direction, zero, out of range, the point is a manual one).
inline bool addAuto(IbatCurve& c, int raw_ma, int ref_ma) {
  if (raw_ma == 0 || ref_ma == 0 || (raw_ma > 0) != (ref_ma > 0)) return false;
  if (absi(raw_ma) > IBAT_CAL_MAX_MA) return false;
  int k = bucketOf(ref_ma);
  if (k < 0) return false;
  IbatPoint& b = c.p[dirOf(ref_ma)][k];
  if (b.n && b.src != IBAT_SRC_AUTO) return false;
  int32_t rx = raw_ma * 10, fx = ref_ma * 10;
  if (b.n < IBAT_N_CAP) b.n++;
  b.src = IBAT_SRC_AUTO;
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
#define IBAT_SET_NONSENSE  -4   // the point is nonsense (offset over 50 mA, ratio outside 0.5..1.5) or breaks the order of the manual ones: not taken

// a point by hand: the raw reading against the true current; replaces the point of the bucket (auto or manual)
inline int setManual(IbatCurve& c, const IbatCurve& def, int raw_ma, int ref_ma) {
  if (raw_ma == 0 || ref_ma == 0 || absi(ref_ma) > IBAT_CAL_MAX_MA || absi(raw_ma) > IBAT_CAL_MAX_MA) return IBAT_SET_ZERO;
  if ((raw_ma > 0) != (ref_ma > 0)) return IBAT_SET_DIRECTION;
  int k = bucketOf(ref_ma), d = dirOf(ref_ma);
  if (k < 0) return IBAT_SET_ZERO;
  IbatCurve t = c;
  IbatPoint& b = t.p[d][k];
  b.raw_x10 = (int16_t)(raw_ma * 10);
  b.ref_x10 = (int16_t)(ref_ma * 10);
  b.n = 1;
  b.src = IBAT_SRC_MANUAL;
  IbatEff e = build(t, def, d);
  if (!(e.present & (1u << k)) || (e.ignored & (1u << k))) return IBAT_SET_NONSENSE;
  // the new point must not push out another manual point that counted before (a lower current with a raw
  // above that of a higher manual point): then it is out of order with it, and not taken
  IbatEff was = build(c, def, d);
  for (int i = 0; i < IBAT_NP; i++) {
    if (i == k || t.p[d][i].src != IBAT_SRC_MANUAL || t.p[d][i].n == 0) continue;
    if ((e.ignored & (1u << i)) && !(was.ignored & (1u << i))) return IBAT_SET_NONSENSE;
  }
  c = t;
  return IBAT_SET_OK;
}

// `charger ibat clear auto`: only the learnt points go
inline void clearAuto(IbatCurve& c) {
  for (int d = 0; d < 2; d++) for (int i = 0; i < IBAT_NP; i++)
    if (c.p[d][i].src == IBAT_SRC_AUTO) memset(&c.p[d][i], 0, sizeof(IbatPoint));
}

// a stored point that cannot be right (direction, range, bucket, flags) is emptied
inline void sanitize(IbatCurve& c) {
  for (int d = 0; d < 2; d++) {
    for (int i = 0; i < IBAT_NP; i++) {
      IbatPoint& b = c.p[d][i];
      bool ok = b.n > 0 && b.src <= IBAT_SRC_MANUAL && (b.src == IBAT_SRC_AUTO ? b.n <= IBAT_N_CAP : b.n == 1);
      if (ok) {
        int raw = b.raw_x10, ref = b.ref_x10;
        ok = raw != 0 && ref != 0 && (raw > 0) == (d == IBAT_CHG) && (ref > 0) == (d == IBAT_CHG) &&
             absi(raw) <= IBAT_CAL_MAX_MA * 10 && bucketX10(absi(ref), d) == i;
      }
      if (!ok) memset(&b, 0, sizeof(b));
    }
  }
}

// ---- the /ibatcal file: "IBC", version, 18 points of 6 bytes (little endian), CRC16 over all before it ----
#define IBAT_FILE_VERSION  1
#define IBAT_FILE_LEN      (4 + 2 * IBAT_NP * 6 + 2)

inline uint16_t crc16(const uint8_t* d, int n) {   // CCITT, init 0xFFFF
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

#define IBUS_FILE_MAGIC   "IBU"   // /ibuscal: the same layout, the CHG half only
inline int toFile(const IbatCurve& c, uint8_t* out, const char* magic = "IBC") {   // IBAT_FILE_LEN bytes
  int n = 0;
  out[n++] = (uint8_t)magic[0]; out[n++] = (uint8_t)magic[1]; out[n++] = (uint8_t)magic[2]; out[n++] = IBAT_FILE_VERSION;
  for (int d = 0; d < 2; d++) for (int i = 0; i < IBAT_NP; i++) {
    const IbatPoint& b = c.p[d][i];
    out[n++] = (uint8_t)((uint16_t)b.raw_x10 & 0xFF); out[n++] = (uint8_t)((uint16_t)b.raw_x10 >> 8);
    out[n++] = (uint8_t)((uint16_t)b.ref_x10 & 0xFF); out[n++] = (uint8_t)((uint16_t)b.ref_x10 >> 8);
    out[n++] = b.n; out[n++] = b.src;
  }
  uint16_t crc = crc16(out, n);
  out[n++] = (uint8_t)(crc & 0xFF); out[n++] = (uint8_t)(crc >> 8);
  return n;
}

// false: a missing, short, long, foreign, newer or damaged file (c is then left empty)
inline bool fromFile(const uint8_t* in, int len, IbatCurve& c, const char* magic = "IBC") {
  memset(&c, 0, sizeof(c));
  if (in == 0 || len != IBAT_FILE_LEN) return false;
  if (in[0] != (uint8_t)magic[0] || in[1] != (uint8_t)magic[1] || in[2] != (uint8_t)magic[2] || in[3] != IBAT_FILE_VERSION) return false;
  uint16_t crc = (uint16_t)(in[len - 2] | (in[len - 1] << 8));
  if (crc != crc16(in, len - 2)) return false;
  int n = 4;
  for (int d = 0; d < 2; d++) for (int i = 0; i < IBAT_NP; i++) {
    IbatPoint& b = c.p[d][i];
    b.raw_x10 = (int16_t)(in[n] | (in[n + 1] << 8));
    b.ref_x10 = (int16_t)(in[n + 2] | (in[n + 3] << 8));
    b.n = in[n + 4]; b.src = in[n + 5];
    n += 6;
  }
  sanitize(c);
  if (magic[2] == IBUS_FILE_MAGIC[2]) memset(c.p[IBAT_DIS], 0, sizeof(c.p[IBAT_DIS]));   // IBUS: the CHG half only
  return true;
}

// The 934d2da calibration in /charger -> points of the curve (only into empty ones)
inline void importLegacy(IbatCurve& c, const IbatLegacyCal& L) {
  for (int d = 0; d < 2; d++) for (int k = 0; k < 2; k++) {
    const IbatLegacyBin& b = L.bin[d][k];
    if (b.n == 0 || b.n > IBAT_N_CAP || b.manual > 1 || (b.manual && b.n != 1)) continue;
    int raw = b.raw_x10, ref = b.ref_x10;
    if (raw == 0 || ref == 0 || (raw > 0) != (d == IBAT_CHG) || (ref > 0) != (d == IBAT_CHG)) continue;
    if (absi(raw) > IBAT_CAL_MAX_MA * 10) continue;
    int i = bucketX10(absi(ref), d);
    if (i < 0 || c.p[d][i].n) continue;
    c.p[d][i].raw_x10 = b.raw_x10; c.p[d][i].ref_x10 = b.ref_x10; c.p[d][i].n = b.n;
    c.p[d][i].src = b.manual ? IBAT_SRC_MANUAL : IBAT_SRC_AUTO;
  }
}

// a number of mA with an optional one-digit fraction -> 0.1 mA; false: not a number
inline bool parseX10(const char*& s, int32_t& out) {
  bool neg = false;
  if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
  if (*s < '0' || *s > '9') return false;
  int32_t v = 0;
  for (; *s >= '0' && *s <= '9'; s++) { v = v * 10 + (*s - '0'); if (v > 100000) return false; }
  v *= 10;
  if (*s == '.') {
    s++;
    if (*s < '0' || *s > '9') return false;
    v += *s - '0';
    s++;
    while (*s >= '0' && *s <= '9') s++;   // finer digits are dropped
  }
  out = neg ? -v : v;
  return true;
}

// "raw:true,raw:true": the default curve of one direction (pairs of the other sign, nonsense or
// unparsable ones are skipped; a later pair of a bucket replaces an earlier one). Returns the points set.
inline int parseCurve(const char* s, int dir, IbatCurve& c) {
  int placed = 0;
  while (s && *s) {
    while (*s == ' ' || *s == ',') s++;
    if (!*s) break;
    int32_t raw = 0, ref = 0;
    bool ok = parseX10(s, raw);
    if (ok && *s == ':') { s++; ok = parseX10(s, ref); } else ok = false;
    while (*s == ' ') s++;
    if (ok && *s != ',' && *s != 0) ok = false;
    if (!ok) { while (*s && *s != ',') s++; continue; }
    if (raw == 0 || ref == 0 || (raw > 0) != (dir == IBAT_CHG) || (ref > 0) != (dir == IBAT_CHG)) continue;
    int i = bucketX10(ref < 0 ? -ref : ref, dir);
    if (i < 0 || absi(raw) > IBAT_CAL_MAX_MA * 10) continue;
    IbatPoint& b = c.p[dir][i];
    if (!b.n) placed++;
    b.raw_x10 = (int16_t)raw; b.ref_x10 = (int16_t)ref; b.n = 1; b.src = IBAT_SRC_BUILD;
  }
  return placed;
}

inline void buildDefault(IbatCurve& c) {
  memset(&c, 0, sizeof(c));
  parseCurve(BQ25798_IBAT_CHG_CURVE, IBAT_CHG, c);
  parseCurve(BQ25798_IBAT_DIS_CURVE, IBAT_DIS, c);
  parseCurve(BQ25798_IBAT_CHG_CURVE2, IBAT_CHG, c);
  parseCurve(BQ25798_IBAT_DIS_CURVE2, IBAT_DIS, c);
}

// the IBUS default curve: BQ25798_IBUS_CURVE and _CURVE2, as the CHG half
inline void buildDefaultBus(IbatCurve& c) {
  memset(&c, 0, sizeof(c));
  parseCurve(BQ25798_IBUS_CURVE, IBAT_CHG, c);
  parseCurve(BQ25798_IBUS_CURVE2, IBAT_CHG, c);
}

}  // namespace IbatCal

// The auto mode: pairs (raw, true) that both hold still go into the points; tells when the file should
// be written again. It does not write: the owner of the IbatCurve does that.
class IbatLearner {
  bool     _have;
  int      _prev_raw, _prev_ref;
  bool     _dirty;
  uint32_t _next_save;
  IbatCurve _saved;   // what the file holds
  bool     _bus;      // IBUS: only positive pairs

  static bool holdsStill(int a, int b) {
    int d = IbatCal::absi(a - b);
    int m = IbatCal::absi(a) > IbatCal::absi(b) ? IbatCal::absi(a) : IbatCal::absi(b);
    int tol = m * IBAT_STABLE_PCT / 100;
    return d <= (tol > IBAT_STABLE_ABS_MA ? tol : IBAT_STABLE_ABS_MA);
  }

public:
  explicit IbatLearner(bool bus = false) { _bus = bus; memset(&_saved, 0, sizeof(_saved)); _have = false; _prev_raw = _prev_ref = 0; _dirty = false; _next_save = IBAT_SAVE_FIRST_MS; }

  // a gap in the readings (the reference is gone): the next pair has no predecessor
  void gap() { _have = false; }

  // one pair; true when it went into a point
  bool feed(IbatCurve& c, int raw_ma, int ref_ma) {
    if (_bus && (raw_ma <= 0 || ref_ma <= 0)) { _have = false; return false; }   // IBUS is never negative
    bool stable = _have && holdsStill(raw_ma, _prev_raw) && holdsStill(ref_ma, _prev_ref);
    _prev_raw = raw_ma;
    _prev_ref = ref_ma;
    _have = true;
    if (!stable || !IbatCal::addAuto(c, raw_ma, ref_ma)) return false;
    // worth a write: a point that just became usable, or one that moved a lot since the last write
    int d = IbatCal::dirOf(ref_ma), k = IbatCal::bucketOf(ref_ma);
    const IbatPoint& now = c.p[d][k];
    const IbatPoint& was = _saved.p[d][k];
    if (IbatCal::usable(now, k)) {
      if (!IbatCal::usable(was, k)) _dirty = true;
      else {
        int dnow = now.ref_x10 - now.raw_x10, dwas = was.ref_x10 - was.raw_x10;
        int lim = IbatCal::absi(now.ref_x10) * 3 / 100;   // 3 % of the current at high current
        if (lim < IBAT_SAVE_DELTA_X10) lim = IBAT_SAVE_DELTA_X10;
        if (IbatCal::absi(dnow - dwas) > lim) _dirty = true;
      }
    }
    return true;
  }

  bool saveDue(uint32_t now) const { return _dirty && (int32_t)(now - _next_save) >= 0; }
  bool dirty() const { return _dirty; }
  void markDirty() { _dirty = true; }
  // a write failed: stays dirty, the next try after the period (flash wear)
  void saveFailed(uint32_t now) { _next_save = now + IBAT_SAVE_PERIOD_MS; }

  // the file now holds `c`: written by the owner (written = true: the next write waits a period),
  // or just loaded
  void synced(const IbatCurve& c, uint32_t now, bool written) {
    _saved = c;
    _dirty = false;
    if (written) _next_save = now + IBAT_SAVE_PERIOD_MS;
  }
};
