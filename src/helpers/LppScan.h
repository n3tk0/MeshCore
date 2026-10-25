#pragma once

#include <stdint.h>

// Finds the first value of a Cayenne LPP type in a telemetry buffer ([channel][type][data]...).
// Returns false when it is not there, or the buffer holds a type this does not know the size of.
#define LPP_SCAN_TEMPERATURE   103   // 2 bytes, 0.1 degC signed
#define LPP_SCAN_HUMIDITY      104   // 1 byte, 0.5 %
#define LPP_SCAN_BAROMETER     115   // 2 bytes, 0.1 hPa

static inline int lppScanSize(uint8_t type) {
  switch (type) {
    case 0: case 1: case 102: case 104: case 120: case 142: return 1;
    case 2: case 3: case 101: case 103: case 115: case 116: case 117: case 121: case 125: case 128: case 132: return 2;
    case 135: return 3;
    case 100: case 118: case 130: case 131: case 133: return 4;
    case 113: case 134: return 6;
    case 136: return 9;
    default: return -1;
  }
}

// raw: the value as stored (big endian, signed for 2 bytes)
static inline bool lppScanFirst(const uint8_t* buf, int len, uint8_t type, int32_t* raw) {
  int i = 0;
  while (i + 2 <= len) {
    uint8_t t = buf[i + 1];
    int n = lppScanSize(t);
    if (n < 0 || i + 2 + n > len) return false;
    if (t == type) {
      const uint8_t* d = &buf[i + 2];
      if (n == 1) *raw = d[0];
      else if (n == 2) *raw = (int16_t)((d[0] << 8) | d[1]);
      else return false;
      return true;
    }
    i += 2 + n;
  }
  return false;
}
