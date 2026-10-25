#pragma once

// Pure helpers of the encoder UI: no Arduino, display or mesh dependencies, so they are unit-tested
// natively (test/test_ui_encoder).

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ui {

// ---- UTF-8

inline int utf8Len(const char* s) {
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) if ((*p & 0xC0) != 0x80) n++;
  return n;
}

inline const char* utf8Skip(const char* s, int chars) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p && chars > 0) {
    p++;
    while ((*p & 0xC0) == 0x80) p++;
    chars--;
  }
  return (const char*)p;
}

// copy up to max_chars characters starting at character 'from'
inline void utf8Slice(char* dest, size_t dest_size, const char* src, int from, int max_chars) {
  const char* a = utf8Skip(src, from);
  const char* b = utf8Skip(a, max_chars);
  size_t n = b - a;
  if (n >= dest_size) n = dest_size - 1;
  memcpy(dest, a, n);
  dest[n] = 0;
}

// Word wrap into lines of at most line_chars characters, breaking at the last space that fits.
// Fills start offsets (bytes into text) and lengths (characters); returns the number of lines.
inline int wrapLines(const char* text, int line_chars, uint8_t* starts, uint8_t* lens, int max_lines) {
  int n = 0;
  const char* p = text;
  while (*p && n < max_lines) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* start = p;
    const char* last_space = NULL;
    int chars = 0, chars_at_space = 0;
    const char* q = p;
    while (*q && chars < line_chars) {
      if (*q == ' ') { last_space = q; chars_at_space = chars; }
      q = utf8Skip(q, 1);
      chars++;
    }
    starts[n] = (uint8_t)(start - text);
    if (*q && *q != ' ' && last_space) {   // break at the last space
      lens[n++] = (uint8_t)chars_at_space;
      p = last_space + 1;
    } else {
      lens[n++] = (uint8_t)chars;
      p = q;
    }
  }
  return n;
}

// ---- battery

// LiPo state of charge from the resting voltage (mV), 0..100. The device powers off at 3.3 V,
// so that is 0%; the curve between follows a typical 1-cell LiPo discharge under light load.
inline int lipoPercent(uint16_t mv) {
  static const uint16_t MV[]  = { 3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200 };
  static const uint8_t  PCT[] = {    0,    5,   10,   25,   35,   45,   55,   65,   80,   92,  100 };
  const int N = sizeof(MV) / sizeof(MV[0]);
  if (mv <= MV[0]) return 0;
  if (mv >= MV[N - 1]) return 100;
  for (int i = 1; i < N; i++) {
    if (mv <= MV[i]) {
      return PCT[i - 1] + (int)(mv - MV[i - 1]) * (PCT[i] - PCT[i - 1]) / (MV[i] - MV[i - 1]);
    }
  }
  return 100;
}

// ---- time

// days since 1970-01-01 of a civil date (Howard Hinnant's algorithm)
inline int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline void civilFromDays(int32_t z, int32_t& y, uint32_t& m, uint32_t& d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

// unix time of 01:00 UTC on the last Sunday of a month (EU summer time switches at that moment)
inline uint32_t lastSunday0100(int32_t year, uint32_t month) {
  int32_t next = month == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, month + 1, 1);
  int32_t last = next - 1;
  int32_t weekday = (last + 4) % 7;   // 1970-01-01 was a Thursday; 0 = Sunday
  if (weekday < 0) weekday += 7;
  return (uint32_t)(last - weekday) * 86400UL + 3600UL;
}

// EU rule: summer time from the last Sunday of March to the last Sunday of October, 01:00 UTC
inline bool euSummerTime(uint32_t utc) {
  int32_t y; uint32_t m, d;
  civilFromDays((int32_t)(utc / 86400UL), y, m, d);
  if (m < 3 || m > 10) return false;
  if (m > 3 && m < 10) return true;
  uint32_t edge = lastSunday0100(y, m);
  return m == 3 ? utc >= edge : utc < edge;
}

// local time: UTC plus the zone offset (hours) plus an hour of EU summer time when enabled
inline uint32_t localTime(uint32_t utc, int tz_hours, bool eu_dst) {
  int32_t t = (int32_t)utc + tz_hours * 3600;
  if (eu_dst && euSummerTime(utc)) t += 3600;
  return (uint32_t)t;
}

// ---- encoder acceleration

// Steps to apply for one detent, from the time since the previous detent in the same direction:
// a fast spin moves further, a slow turn stays one step at a time.
inline int accelSteps(unsigned long since_prev_ms, bool same_direction) {
  if (!same_direction) return 1;
  if (since_prev_ms < 30) return 5;
  if (since_prev_ms < 60) return 3;
  if (since_prev_ms < 100) return 2;
  return 1;
}

// quiet hours (Settings > Messages > Quiet): 0 = off, else one of these spans of local time
#define QUIET_PRESETS 6
static const uint8_t QUIET_FROM[QUIET_PRESETS] = { 0, 22, 23, 0, 22, 23 };
static const uint8_t QUIET_TO[QUIET_PRESETS]   = { 0,  7,  7, 7,  6,  6 };

// local: local time in seconds (localTime()); the span may wrap past midnight
inline bool quietNow(uint32_t local, uint8_t preset) {
  if (preset == 0 || preset >= QUIET_PRESETS) return false;
  int h = (int)((local % 86400) / 3600);
  int from = QUIET_FROM[preset], to = QUIET_TO[preset];
  return from <= to ? (h >= from && h < to) : (h >= from || h < to);
}

}  // namespace ui
