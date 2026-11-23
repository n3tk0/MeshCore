#pragma once

#include <stddef.h>
#include <stdint.h>

// Lookup ASCII replacement for common emojis & smart punctuation, or NULL if unknown.
const char* emojiToAscii(uint32_t cp);

// Translate UTF-8 text for OLED/LCD display:
// - Keeps printable ASCII (32..126)
// - Keeps 2-byte Cyrillic UTF-8 when allow_cyrillic is true
// - Maps common emojis & smart punctuation to ASCII
// - Collapses consecutive unknown non-ASCII characters into a single CP437 full block (0xDB)
void translateUTF8ForDisplay(char* dest, const char* src, size_t dest_size, bool allow_cyrillic);

static inline uint16_t utf8CharCount(const char* str) {
  uint16_t n = 0;
  for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
    if ((*p & 0xC0) != 0x80) n++;
  }
  return n;
}
