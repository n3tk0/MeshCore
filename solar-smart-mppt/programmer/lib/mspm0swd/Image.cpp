#include "Image.h"
#include <string.h>

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int byteAt(const char* p, const char* end) {
  if (p + 2 > end) return -1;
  int h = hexval(p[0]), l = hexval(p[1]);
  return (h < 0 || l < 0) ? -1 : (h << 4) | l;
}

int hexToBin(const char* text, size_t text_len, uint8_t* out, uint32_t max) {
  memset(out, 0xFF, max);
  const char* p = text;
  const char* end = text + text_len;
  uint32_t base = 0, len = 0;
  bool any = false;
  while (p < end) {
    if (*p != ':') { p++; continue; }   // line ends, spaces
    p++;
    uint8_t rec[260];
    int n = byteAt(p, end);
    if (n < 0) return -1;
    for (int i = 0; i < n + 5; i++) {
      int b = byteAt(p + 2 * i, end);
      if (b < 0) return -1;
      rec[i] = (uint8_t)b;
    }
    p += 2 * (n + 5);
    uint8_t sum = 0;
    for (int i = 0; i < n + 5; i++) sum += rec[i];
    if (sum) return -2;
    uint32_t off = ((uint32_t)rec[1] << 8) | rec[2];
    switch (rec[3]) {
      case 0x00: {
        uint32_t a = base + off;
        if (a > max || (uint32_t)n > max - a) return -3;
        memcpy(out + a, rec + 4, n);
        if (a + n > len) len = a + n;
        any = any || n > 0;
        break;
      }
      case 0x01: return any ? (int)len : -4;
      case 0x02:
        if (n != 2) return -1;
        base = (((uint32_t)rec[4] << 8) | rec[5]) << 4;
        break;
      case 0x04:
        if (n != 2) return -1;
        base = (((uint32_t)rec[4] << 8) | rec[5]) << 16;
        break;
      case 0x03: case 0x05: break;     // start address: not needed
      default: return -1;
    }
  }
  return any ? (int)len : -4;
}

bool imageLooksValid(const uint8_t* img, uint32_t len) {
  if (len < 8 || len > IMAGE_MAX) return false;
  uint32_t sp = img[0] | (img[1] << 8) | (img[2] << 16) | ((uint32_t)img[3] << 24);
  uint32_t rv = img[4] | (img[5] << 8) | (img[6] << 16) | ((uint32_t)img[7] << 24);
  return sp >= 0x20000000u && sp <= 0x20008000u && (rv & 1) && (rv & ~1u) >= 8 && (rv & ~1u) < len;
}
