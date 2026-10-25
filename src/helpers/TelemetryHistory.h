#pragma once

#include <stdint.h>
#include <string.h>

// Telemetry history: the last 24 hours every 10 minutes and the last week every hour.
// The caller adds a sample every 10 minutes; six of them make one hourly sample (averages, sums
// for the counters). Kept in RAM (~6 KB) and in a file, saved once an hour.
// An app reads one value at a time (a "field") for a stretch of samples: see the request below.
// Pure logic: no clock, no radio, no file system.

#define TH_DAY_SLOTS     144     // 24 h x 6
#define TH_WEEK_SLOTS    168     // 7 d x 24
#define TH_DAY_MINS       10
#define TH_WEEK_MINS      60
#define TH_FILE_VER        1

// fields (each value int16 on the air, 0x8000 = none)
#define TH_F_BATT_MV       0
#define TH_F_BATT_PCT      1
#define TH_F_TEMP_DC       2     // 0.1 degC: a sensor's, else the MCU's
#define TH_F_HUMIDITY      3     // %
#define TH_F_PRESSURE      4     // 0.1 hPa above 900 hPa (int16 range)
#define TH_F_CURRENT_MA    5     // battery current, positive = out (INA219)
#define TH_F_NOISE_DBM     6
#define TH_F_RX            7     // packets received in the slot
#define TH_F_TX            8     // packets sent in the slot
#define TH_F_AIR_TX_S      9     // seconds of transmit in the slot
#define TH_F_COUNT        10

#define TH_NONE         ((int16_t)0x8000)
// a clock before this is not set: VolatileRTCClock starts in May 2024 after every restart, and its
// samples would be followed by a jump of years once the clock is set (wiping the rings with blanks)
#define TH_VALID_EPOCH  1735689600UL   // 2025-01-01

struct TelemetrySample {
  int16_t v[TH_F_COUNT];
};

struct TelemetryRing {
  uint32_t last_time;     // RTC seconds of the newest sample, 0 = unknown clock
  uint16_t head;          // where the next sample goes
  uint16_t count;
};

class TelemetryHistory {
  TelemetrySample _day[TH_DAY_SLOTS];
  TelemetrySample _week[TH_WEEK_SLOTS];
  TelemetryRing _rday, _rweek;
  int32_t _acc[TH_F_COUNT];   // the hour being made, from 10-minute samples
  uint8_t _acc_n[TH_F_COUNT];
  uint8_t _acc_slots;
  bool _dirty;

  static bool isCounter(int f) { return f == TH_F_RX || f == TH_F_TX || f == TH_F_AIR_TX_S; }

  static void push(TelemetrySample* ring, int slots, TelemetryRing& r, const TelemetrySample& s, uint32_t now) {
    ring[r.head] = s;
    r.head = (r.head + 1) % slots;
    if (r.count < slots) r.count++;
    // no clock (a restart without RTC): go on from the last known time, so the history keeps its time
    // axis (the time the node was off is not known: it shows when the clock is set again)
    if (now) r.last_time = now;
    else if (r.last_time) r.last_time += (slots == TH_DAY_SLOTS ? TH_DAY_MINS : TH_WEEK_MINS) * 60;
  }
  static void blank(TelemetrySample& s) { for (int f = 0; f < TH_F_COUNT; f++) s.v[f] = TH_NONE; }

  void addHourPart(const TelemetrySample& s, uint32_t now) {
    for (int f = 0; f < TH_F_COUNT; f++) {
      if (s.v[f] == TH_NONE) continue;
      _acc[f] += s.v[f];
      _acc_n[f]++;
    }
    if (++_acc_slots >= TH_WEEK_MINS / TH_DAY_MINS) {
      TelemetrySample h;
      for (int f = 0; f < TH_F_COUNT; f++) {
        if (_acc_n[f] == 0) h.v[f] = TH_NONE;
        else if (isCounter(f)) h.v[f] = (int16_t)(_acc[f] > 32767 ? 32767 : _acc[f]);
        else h.v[f] = (int16_t)(_acc[f] / _acc_n[f]);
      }
      push(_week, TH_WEEK_SLOTS, _rweek, h, now);
      memset(_acc, 0, sizeof(_acc));
      memset(_acc_n, 0, sizeof(_acc_n));
      _acc_slots = 0;
    }
  }

public:
  TelemetryHistory() { clear(); }

  void clear() {
    memset(&_rday, 0, sizeof(_rday));
    memset(&_rweek, 0, sizeof(_rweek));
    memset(_acc, 0, sizeof(_acc));
    memset(_acc_n, 0, sizeof(_acc_n));
    _acc_slots = 0;
    _dirty = false;
  }

  static void emptySample(TelemetrySample& s) { blank(s); }

  // A 10-minute sample. 'now' = RTC seconds (a clock before TH_VALID_EPOCH counts as unknown). Missed slots
  // (the node was off) are filled with empty samples, so the graph keeps its time axis.
  void add(const TelemetrySample& s, uint32_t now) {
    if (now < TH_VALID_EPOCH) now = 0;
    if (now && _rday.last_time && now > _rday.last_time) {
      uint32_t missed = (now - _rday.last_time) / (TH_DAY_MINS * 60);
      if (missed > 1) {
        missed--;
        if (missed > TH_WEEK_SLOTS * (TH_WEEK_MINS / TH_DAY_MINS)) missed = TH_WEEK_SLOTS * (TH_WEEK_MINS / TH_DAY_MINS);
        TelemetrySample e;
        blank(e);
        for (uint32_t i = 0; i < missed; i++) {
          uint32_t t = _rday.last_time + TH_DAY_MINS * 60;
          if (i < TH_DAY_SLOTS || missed - i <= TH_DAY_SLOTS) push(_day, TH_DAY_SLOTS, _rday, e, t);
          else _rday.last_time = t;
          addHourPart(e, t);
        }
      }
    }
    push(_day, TH_DAY_SLOTS, _rday, s, now);
    addHourPart(s, now);
    _dirty = true;
  }

  // ring 0 = day (10 min), 1 = week (1 h)
  int count(int ring) const { return ring ? _rweek.count : _rday.count; }
  uint32_t lastTime(int ring) const { return ring ? _rweek.last_time : _rday.last_time; }
  // i = 0 is the oldest sample kept
  const TelemetrySample* at(int ring, int i) const {
    const TelemetryRing& r = ring ? _rweek : _rday;
    int slots = ring ? TH_WEEK_SLOTS : TH_DAY_SLOTS;
    if (i < 0 || i >= r.count) return NULL;
    int idx = (r.head + slots - r.count + i) % slots;
    return ring ? &_week[idx] : &_day[idx];
  }
  bool takeDirty() { bool d = _dirty; _dirty = false; return d; }

  // Hours until the battery is empty, from how fast its percentage fell over the last few hours
  // (a least-squares line through the 10-minute samples). -1: not known (under 2 h of samples,
  // charging, or hardly falling). A rise of more than 2 % between two samples (a charger) starts over.
  int hoursLeft(int window = 36) const {
    int n = count(0), first = n - window < 0 ? 0 : n - window;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int k = 0, last = -1, last_pct = -1;
    for (int i = first; i < n; i++) {
      int16_t p = at(0, i)->v[TH_F_BATT_PCT];
      if (p == TH_NONE) continue;
      if (last_pct >= 0 && p > last_pct + 2) { sx = sy = sxx = sxy = 0; k = 0; }   // charged: only after it
      sx += i; sy += p; sxx += (double)i * i; sxy += (double)i * p;
      k++;
      last = i;
      last_pct = p;
    }
    if (k < 12 || last < 0) return -1;
    double den = k * sxx - sx * sx;
    if (den <= 0) return -1;
    double slope = (k * sxy - sx * sy) / den;           // % per sample (10 min)
    double per_hour = -slope * (60 / TH_DAY_MINS);
    if (per_hour < 0.2) return -1;                     // hardly falling: no honest estimate
    double h = last_pct / per_hour;
    return h > 999 ? 999 : (int)h;
  }

  // Request (after the request type byte): ring(1) field(1) start(2 LE, from the oldest) max(1).
  // Reply: ring(1) field(1) total(2) start(2) n(1) last_time(4) interval_mins(1) values(n x int16 LE)
  //        fields(2 LE): bit f set = field f has a value somewhere in this ring (the app offers only those).
  // Returns the reply length (0 = bad request).
  int formatReply(const uint8_t* req, int req_len, uint8_t* out, int out_max) const {
    if (req_len < 5) return 0;
    int ring = req[0], field = req[1], start = req[2] | (req[3] << 8), max = req[4];
    if (ring > 1 || field >= TH_F_COUNT || out_max < 14) return 0;
    int total = count(ring);
    if (start > total) start = total;
    int room = (out_max - 14) / 2;
    uint16_t fields = fieldsWithValues(ring);
    int n = total - start;
    if (n > max) n = max;
    if (n > room) n = room;
    if (!(fields & (1 << field))) n = 0;   // nothing but empty slots: only the header
    uint32_t t = lastTime(ring);
    out[0] = ring; out[1] = field;
    out[2] = total & 0xFF; out[3] = total >> 8;
    out[4] = start & 0xFF; out[5] = start >> 8;
    out[6] = n;
    memcpy(&out[7], &t, 4);
    out[11] = ring ? TH_WEEK_MINS : TH_DAY_MINS;
    for (int i = 0; i < n; i++) {
      int16_t v = at(ring, start + i)->v[field];
      out[12 + i * 2] = v & 0xFF;
      out[13 + i * 2] = (v >> 8) & 0xFF;
    }
    out[12 + n * 2] = fields & 0xFF;
    out[13 + n * 2] = fields >> 8;
    return 14 + n * 2;
  }

  uint16_t fieldsWithValues(int ring) const {
    uint16_t m = 0;
    for (int i = count(ring) - 1; i >= 0; i--) {
      const TelemetrySample* s = at(ring, i);
      for (int f = 0; f < TH_F_COUNT; f++) if (s->v[f] != TH_NONE) m |= 1 << f;
    }
    return m;
  }

  // the file: ver(1) + both rings (header + all slots) + the hour being made
  template<class W> void save(W& w) const {
    uint8_t ver = TH_FILE_VER;
    w.write(&ver, 1);
    w.write((const uint8_t*)&_rday, sizeof(_rday));
    w.write((const uint8_t*)_day, sizeof(_day));
    w.write((const uint8_t*)&_rweek, sizeof(_rweek));
    w.write((const uint8_t*)_week, sizeof(_week));
    w.write((const uint8_t*)_acc, sizeof(_acc));
    w.write((const uint8_t*)_acc_n, sizeof(_acc_n));
    w.write(&_acc_slots, 1);
  }
  template<class R> bool load(R& r) {
    clear();
    uint8_t ver = 0;
    bool ok = r.read(&ver, 1) == 1 && ver == TH_FILE_VER
      && r.read((uint8_t*)&_rday, sizeof(_rday)) == (int)sizeof(_rday)
      && r.read((uint8_t*)_day, sizeof(_day)) == (int)sizeof(_day)
      && r.read((uint8_t*)&_rweek, sizeof(_rweek)) == (int)sizeof(_rweek)
      && r.read((uint8_t*)_week, sizeof(_week)) == (int)sizeof(_week)
      && r.read((uint8_t*)_acc, sizeof(_acc)) == (int)sizeof(_acc)
      && r.read((uint8_t*)_acc_n, sizeof(_acc_n)) == (int)sizeof(_acc_n)
      && r.read(&_acc_slots, 1) == 1
      && _rday.head < TH_DAY_SLOTS && _rday.count <= TH_DAY_SLOTS
      && _rweek.head < TH_WEEK_SLOTS && _rweek.count <= TH_WEEK_SLOTS && _acc_slots < 6;
    if (!ok) clear();
    return ok;
  }
};
