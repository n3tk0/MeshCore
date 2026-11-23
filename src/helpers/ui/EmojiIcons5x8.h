#pragma once

#include <stdint.h>

// 5x8 monochrome emoji icons encoded in Unicode Private Use Area (U+E000..U+E03F)
// so translated strings stay valid UTF-8 (1 character per icon for width/slicing/ellipsizing).
// Bitmaps use the same 5-column layout as CyrillicFont5x8.h (bit 0 = top row).

static const uint16_t EMOJI_PUA_BASE = 0xE000;

enum EmojiIconId : int8_t {
  EMOJI_ICON_NONE = -1,
  EMOJI_SMILE = 0,
  EMOJI_LAUGH,
  EMOJI_WINK,
  EMOJI_SAD,
  EMOJI_CRY,
  EMOJI_ANGRY,
  EMOJI_SURPRISED,
  EMOJI_COOL,
  EMOJI_HEART,
  EMOJI_BROKEN_HEART,
  EMOJI_WAVE,
  EMOJI_THUMBS_UP,
  EMOJI_THUMBS_DOWN,
  EMOJI_FIRE,
  EMOJI_CHECK,
  EMOJI_CROSS,
  EMOJI_STAR,
  EMOJI_SUN,
  EMOJI_CLOUD,
  EMOJI_RAIN,
  EMOJI_SNOW,
  EMOJI_ZAP,
  EMOJI_MOUNTAIN,
  EMOJI_ANTENNA,
  EMOJI_BATTERY,
  EMOJI_PIN,
  EMOJI_WARNING,
  EMOJI_SKULL,
  EMOJI_GHOST,
  EMOJI_COFFEE,
  EMOJI_BEER,
  EMOJI_MUSIC,
  EMOJI_BELL,
  EMOJI_ROCKET,
  EMOJI_ARROW_UP,
  EMOJI_ARROW_DOWN,
  EMOJI_ARROW_LEFT,
  EMOJI_ARROW_RIGHT,
  EMOJI_ICON_COUNT
};

static_assert(EMOJI_ICON_COUNT <= 64, "3-byte PUA UTF-8 encoding supports up to 64 icons");

extern const uint8_t emoji_icons_5x8[EMOJI_ICON_COUNT][5];

static inline int emojiIconIndex(uint16_t cp) {
  if (cp >= EMOJI_PUA_BASE && cp < EMOJI_PUA_BASE + EMOJI_ICON_COUNT) {
    return (int)(cp - EMOJI_PUA_BASE);
  }
  return -1;
}
