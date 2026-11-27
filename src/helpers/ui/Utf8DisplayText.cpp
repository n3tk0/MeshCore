#include "Utf8DisplayText.h"
#include "CyrillicFont5x8.h"
#include "EmojiIcons5x8.h"
#include <string.h>

// 5x8 column-major bitmaps (bit 0 = top row, bit 7 = bottom row), matching CyrillicFont5x8.h.
const uint8_t emoji_icons_5x8[EMOJI_ICON_COUNT][5] = {
  {0x10, 0x26, 0x20, 0x26, 0x10},  //  0: EMOJI_SMILE
  {0x18, 0x2A, 0x28, 0x2A, 0x18},  //  1: EMOJI_LAUGH
  {0x10, 0x26, 0x20, 0x24, 0x14},  //  2: EMOJI_WINK
  {0x20, 0x16, 0x10, 0x16, 0x20},  //  3: EMOJI_SAD
  {0x40, 0x22, 0x20, 0x2A, 0x40},  //  4: EMOJI_CRY
  {0x22, 0x14, 0x10, 0x14, 0x22},  //  5: EMOJI_ANGRY
  {0x00, 0x12, 0x28, 0x12, 0x00},  //  6: EMOJI_SURPRISED
  {0x16, 0x26, 0x22, 0x26, 0x16},  //  7: EMOJI_COOL
  {0x0C, 0x1E, 0x3C, 0x1E, 0x0C},  //  8: EMOJI_HEART
  {0x0C, 0x16, 0x38, 0x0E, 0x0C},  //  9: EMOJI_BROKEN_HEART
  {0x1E, 0x38, 0x3F, 0x38, 0x1E},  // 10: EMOJI_WAVE
  {0x38, 0x04, 0x3F, 0x3C, 0x3C},  // 11: EMOJI_THUMBS_UP
  {0x0E, 0x10, 0x7E, 0x1E, 0x1E},  // 12: EMOJI_THUMBS_DOWN
  {0x38, 0x7C, 0x6F, 0x7E, 0x38},  // 13: EMOJI_FIRE
  {0x18, 0x30, 0x18, 0x0C, 0x06},  // 14: EMOJI_CHECK
  {0x22, 0x14, 0x08, 0x14, 0x22},  // 15: EMOJI_CROSS
  {0x24, 0x1C, 0x0F, 0x1C, 0x24},  // 16: EMOJI_STAR
  {0x22, 0x1C, 0x5D, 0x1C, 0x22},  // 17: EMOJI_SUN
  {0x18, 0x3C, 0x3E, 0x3C, 0x38},  // 18: EMOJI_CLOUD
  {0x14, 0x26, 0x17, 0x26, 0x14},  // 19: EMOJI_RAIN
  {0x2A, 0x1C, 0x7F, 0x1C, 0x2A},  // 20: EMOJI_SNOW
  {0x08, 0x6C, 0x3E, 0x1B, 0x08},  // 21: EMOJI_ZAP
  {0x30, 0x3C, 0x3A, 0x3C, 0x30},  // 22: EMOJI_MOUNTAIN
  {0x01, 0x22, 0x3C, 0x22, 0x01},  // 23: EMOJI_ANTENNA
  {0x3E, 0x3A, 0x3B, 0x3A, 0x3E},  // 24: EMOJI_BATTERY
  {0x06, 0x0F, 0x3D, 0x0F, 0x06},  // 25: EMOJI_PIN
  {0x7C, 0x7E, 0x53, 0x7E, 0x7C},  // 26: EMOJI_WARNING
  {0x0E, 0x3B, 0x1F, 0x3B, 0x0E},  // 27: EMOJI_SKULL
  {0x3E, 0x1B, 0x3F, 0x1B, 0x3E},  // 28: EMOJI_GHOST
  {0x7C, 0x7D, 0x7E, 0x28, 0x38},  // 29: EMOJI_COFFEE
  {0x3E, 0x3F, 0x3F, 0x13, 0x1E},  // 30: EMOJI_BEER
  {0x30, 0x30, 0x3F, 0x02, 0x04},  // 31: EMOJI_MUSIC
  {0x10, 0x1E, 0x3F, 0x1E, 0x10},  // 32: EMOJI_BELL
  {0x18, 0x0E, 0x3F, 0x0E, 0x18},  // 33: EMOJI_ROCKET
  {0x04, 0x06, 0x3F, 0x06, 0x04},  // 34: EMOJI_ARROW_UP
  {0x08, 0x18, 0x3F, 0x18, 0x08},  // 35: EMOJI_ARROW_DOWN
  {0x08, 0x1C, 0x3E, 0x08, 0x08},  // 36: EMOJI_ARROW_LEFT
  {0x08, 0x08, 0x3E, 0x1C, 0x08},  // 37: EMOJI_ARROW_RIGHT
  {0x08, 0x14, 0x22, 0x14, 0x08},  // 38: EMOJI_GENERIC (hollow diamond)
};

namespace {

struct EmojiEntry {
  uint32_t cp;
  int8_t icon;
  const char* ascii;
};

#ifndef NO_EMOJI_TABLE   // small-flash builds (STM32): every emoji is the generic icon / '?'
// Sorted by Unicode code point for binary search.
const EmojiEntry kEmojiTable[] = {
  // Common punctuation / typographic characters from mobile keyboards
  { 0x00A0, EMOJI_ICON_NONE,    " " },        // NBSP
  { 0x00AB, EMOJI_ICON_NONE,    "\"" },       // «
  { 0x00BB, EMOJI_ICON_NONE,    "\"" },       // »
  { 0x2013, EMOJI_ICON_NONE,    "-" },        // – en-dash
  { 0x2014, EMOJI_ICON_NONE,    "-" },        // — em-dash
  { 0x2018, EMOJI_ICON_NONE,    "'" },        // ‘
  { 0x2019, EMOJI_ICON_NONE,    "'" },        // ’
  { 0x201C, EMOJI_ICON_NONE,    "\"" },       // “
  { 0x201D, EMOJI_ICON_NONE,    "\"" },       // ”
  { 0x201E, EMOJI_ICON_NONE,    "\"" },       // „
  { 0x2026, EMOJI_ICON_NONE,    "..." },      // …
  { 0x203C, EMOJI_ICON_NONE,    "!!" },       // ‼️
  { 0x2049, EMOJI_ICON_NONE,    "!?" },       // ⁉️
  { 0x2190, EMOJI_ARROW_LEFT,   "<-" },       // ←
  { 0x2192, EMOJI_ARROW_RIGHT,  "->" },       // →
  { 0x2600, EMOJI_SUN,          "[sun]" },    // ☀️
  { 0x2601, EMOJI_CLOUD,        "[cloud]" },  // ☁️
  { 0x2615, EMOJI_COFFEE,       "[coffee]" }, // ☕
  { 0x2639, EMOJI_SAD,          ":(" },       // ☹️
  { 0x263A, EMOJI_SMILE,        ":)" },       // ☺️
  { 0x26A0, EMOJI_WARNING,      "[!]" },      // ⚠️
  { 0x26A1, EMOJI_ZAP,          "[zap]" },    // ⚡
  { 0x26F0, EMOJI_MOUNTAIN,     "[mtn]" },    // ⛰️
  { 0x2705, EMOJI_CHECK,        "[OK]" },     // ✅
  { 0x270C, EMOJI_ICON_NONE,    "V" },        // ✌️
  { 0x2714, EMOJI_CHECK,        "[OK]" },     // ✔️
  { 0x2716, EMOJI_CROSS,        "[X]" },      // ✖️
  { 0x2728, EMOJI_STAR,         "*" },        // ✨
  { 0x2744, EMOJI_SNOW,         "[snow]" },   // ❄️
  { 0x274C, EMOJI_CROSS,        "[X]" },      // ❌
  { 0x2753, EMOJI_ICON_NONE,    "?" },        // ❓
  { 0x2757, EMOJI_ICON_NONE,    "!" },        // ❗
  { 0x2764, EMOJI_HEART,        "<3" },       // ❤️
  { 0x27A1, EMOJI_ARROW_RIGHT,  "->" },       // ➡️
  { 0x2B05, EMOJI_ARROW_LEFT,   "<-" },       // ⬅️
  { 0x2B06, EMOJI_ARROW_UP,     "^" },        // ⬆️
  { 0x2B07, EMOJI_ARROW_DOWN,   "v" },        // ⬇️
  { 0x2B50, EMOJI_STAR,         "*" },        // ⭐

  // 4-byte Unicode emojis (sorted by code point)
  { 0x1F31F, EMOJI_STAR,        "*" },        // 🌟
  { 0x1F327, EMOJI_RAIN,        "[rain]" },   // 🌧️
  { 0x1F37A, EMOJI_BEER,        "[beer]" },   // 🍺
  { 0x1F37B, EMOJI_BEER,        "[cheers]" }, // 🍻
  { 0x1F389, EMOJI_ICON_NONE,   "[!]" },      // 🎉
  { 0x1F3B5, EMOJI_MUSIC,       "[music]" },  // 🎵
  { 0x1F3B6, EMOJI_MUSIC,       "[music]" },  // 🎶
  { 0x1F3D4, EMOJI_MOUNTAIN,    "[mtn]" },    // 🏔️
  { 0x1F440, EMOJI_ICON_NONE,   "o_o" },      // 👀
  { 0x1F44B, EMOJI_WAVE,        "o/" },       // 👋
  { 0x1F44C, EMOJI_ICON_NONE,   "OK" },       // 👌
  { 0x1F44D, EMOJI_THUMBS_UP,   "+1" },       // 👍
  { 0x1F44E, EMOJI_THUMBS_DOWN, "-1" },       // 👎
  { 0x1F44F, EMOJI_ICON_NONE,   "[clap]" },   // 👏
  { 0x1F47B, EMOJI_GHOST,       "[ghost]" },  // 👻
  { 0x1F480, EMOJI_SKULL,       "[skull]" },  // 💀
  { 0x1F494, EMOJI_BROKEN_HEART,"</3" },      // 💔
  { 0x1F495, EMOJI_HEART,       "<3" },       // 💕
  { 0x1F496, EMOJI_HEART,       "<3" },       // 💖
  { 0x1F499, EMOJI_HEART,       "<3" },       // 💙
  { 0x1F49A, EMOJI_HEART,       "<3" },       // 💚
  { 0x1F49B, EMOJI_HEART,       "<3" },       // 💛
  { 0x1F49C, EMOJI_HEART,       "<3" },       // 💜
  { 0x1F4A5, EMOJI_ICON_NONE,   "[boom]" },   // 💥
  { 0x1F4A9, EMOJI_ICON_NONE,   "[poop]" },   // 💩
  { 0x1F4AA, EMOJI_ICON_NONE,   "[flex]" },   // 💪
  { 0x1F4AF, EMOJI_ICON_NONE,   "100" },      // 💯
  { 0x1F4CD, EMOJI_PIN,         "[pin]" },    // 📍
  { 0x1F4E1, EMOJI_ANTENNA,     "[ant]" },    // 📡
  { 0x1F4FB, EMOJI_ANTENNA,     "[radio]" },  // 📻
  { 0x1F50B, EMOJI_BATTERY,     "[bat]" },    // 🔋
  { 0x1F514, EMOJI_BELL,        "[bell]" },   // 🔔
  { 0x1F525, EMOJI_FIRE,        "[fire]" },   // 🔥
  { 0x1F5A4, EMOJI_HEART,       "<3" },       // 🖤
  { 0x1F600, EMOJI_LAUGH,       ":D" },       // 😀
  { 0x1F601, EMOJI_LAUGH,       ":D" },       // 😁
  { 0x1F602, EMOJI_LAUGH,       ":D" },       // 😂
  { 0x1F603, EMOJI_LAUGH,       ":D" },       // 😃
  { 0x1F604, EMOJI_LAUGH,       ":D" },       // 😄
  { 0x1F605, EMOJI_ICON_NONE,   ":')" },      // 😅
  { 0x1F606, EMOJI_LAUGH,       "XD" },       // 😆
  { 0x1F607, EMOJI_ICON_NONE,   "O:)" },      // 😇
  { 0x1F608, EMOJI_ICON_NONE,   ">:)" },      // 😈
  { 0x1F609, EMOJI_WINK,        ";)" },       // 😉
  { 0x1F60A, EMOJI_SMILE,       ":)" },       // 😊
  { 0x1F60B, EMOJI_ICON_NONE,   ":P" },       // 😋
  { 0x1F60C, EMOJI_SMILE,       ":)" },       // 😌
  { 0x1F60D, EMOJI_HEART,       "<3" },       // 😍
  { 0x1F60E, EMOJI_COOL,        "B)" },       // 😎
  { 0x1F60F, EMOJI_WINK,        ";)" },       // 😏
  { 0x1F610, EMOJI_ICON_NONE,   ":|" },       // 😐
  { 0x1F611, EMOJI_ICON_NONE,   "-_-" },      // 😑
  { 0x1F612, EMOJI_ICON_NONE,   ":/" },       // 😒
  { 0x1F614, EMOJI_SAD,         ":(" },       // 😔
  { 0x1F615, EMOJI_ICON_NONE,   ":/" },       // 😕
  { 0x1F618, EMOJI_ICON_NONE,   ":*" },       // 😘
  { 0x1F61B, EMOJI_ICON_NONE,   ":P" },       // 😛
  { 0x1F61C, EMOJI_ICON_NONE,   ";P" },       // 😜
  { 0x1F61D, EMOJI_ICON_NONE,   "XP" },       // 😝
  { 0x1F61E, EMOJI_SAD,         ":(" },       // 😞
  { 0x1F620, EMOJI_ANGRY,       ">:(" },      // 😠
  { 0x1F621, EMOJI_ANGRY,       ">:(" },      // 😡
  { 0x1F622, EMOJI_CRY,         ":'(" },      // 😢
  { 0x1F624, EMOJI_ANGRY,       ">:(" },      // 😤
  { 0x1F629, EMOJI_SAD,         ":(" },       // 😩
  { 0x1F62C, EMOJI_ICON_NONE,   ":E" },       // 😬
  { 0x1F62D, EMOJI_CRY,         ":'(" },      // 😭
  { 0x1F62E, EMOJI_SURPRISED,   ":O" },       // 😮
  { 0x1F631, EMOJI_SURPRISED,   ":O" },       // 😱
  { 0x1F632, EMOJI_SURPRISED,   ":O" },       // 😲
  { 0x1F634, EMOJI_ICON_NONE,   "Zzz" },      // 😴
  { 0x1F635, EMOJI_ICON_NONE,   "X_X" },      // 😵
  { 0x1F641, EMOJI_SAD,         ":(" },       // 🙁
  { 0x1F642, EMOJI_SMILE,       ":)" },       // 🙂
  { 0x1F644, EMOJI_ICON_NONE,   ":/" },       // 🙄
  { 0x1F64F, EMOJI_ICON_NONE,   "[pray]" },   // 🙏
  { 0x1F680, EMOJI_ROCKET,      "[rocket]" }, // 🚀
  { 0x1F697, EMOJI_ICON_NONE,   "[car]" },    // 🚗
  { 0x1F6B2, EMOJI_ICON_NONE,   "[bike]" },   // 🚲
  { 0x1F90D, EMOJI_HEART,       "<3" },       // 🤍
  { 0x1F912, EMOJI_ICON_NONE,   "[sick]" },   // 🤒
  { 0x1F914, EMOJI_ICON_NONE,   ":?" },       // 🤔
  { 0x1F918, EMOJI_ICON_NONE,   "\\m/" },     // 🤘
  { 0x1F919, EMOJI_ICON_NONE,   "[call]" },   // 🤙
  { 0x1F91D, EMOJI_ICON_NONE,   "[shake]" },  // 🤝
  { 0x1F91E, EMOJI_ICON_NONE,   "[luck]" },   // 🤞
  { 0x1F921, EMOJI_ICON_NONE,   "[clown]" },  // 🤡
  { 0x1F923, EMOJI_LAUGH,       ":D" },       // 🤣
  { 0x1F926, EMOJI_ICON_NONE,   "[fp]" },     // 🤦
  { 0x1F929, EMOJI_STAR,        "*:)" },      // 🤩
  { 0x1F92B, EMOJI_ICON_NONE,   "shh" },      // 🤫
  { 0x1F92C, EMOJI_ANGRY,       "#@!" },      // 🤬
  { 0x1F92D, EMOJI_ICON_NONE,   ":x" },       // 🤭
  { 0x1F92F, EMOJI_SURPRISED,   ":O" },       // 🤯
  { 0x1F937, EMOJI_ICON_NONE,   "\\_/" },     // 🤷
  { 0x1F970, EMOJI_SMILE,       ":)" },       // 🥰
  { 0x1F973, EMOJI_LAUGH,       ":D" },       // 🥳
  { 0x1F97A, EMOJI_SAD,         ":(" },       // 🥺
  { 0x1F9E1, EMOJI_HEART,       "<3" },       // 🧡
  { 0x1FAE1, EMOJI_ICON_NONE,   "o7" },       // 🫡
};

#endif

const EmojiEntry* findEmojiEntry(uint32_t cp) {
#ifdef NO_EMOJI_TABLE
  (void)cp;
  return NULL;
#else
  size_t lo = 0;
  size_t hi = sizeof(kEmojiTable) / sizeof(kEmojiTable[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (kEmojiTable[mid].cp == cp) return &kEmojiTable[mid];
    if (kEmojiTable[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
#endif
}

// Variation selectors, keycap combiner, and Fitzpatrick skin-tone modifiers.
bool isIgnoredEmojiModifier(uint32_t cp) {
  return cp == 0x20E3 || cp == 0xFE0E || cp == 0xFE0F ||
         (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

// Emoji blocks: misc symbols & dingbats, misc symbols & arrows, and the supplementary emoji planes.
bool isEmojiCodepoint(uint32_t cp) {
  return (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
         (cp >= 0x1F000 && cp <= 0x1FAFF);
}

}  // namespace

const char* emojiToAscii(uint32_t cp) {
  const EmojiEntry* e = findEmojiEntry(cp);
  return e ? e->ascii : NULL;
}

void translateUTF8ForDisplay(char* dest, const char* src, size_t dest_size,
                             bool allow_cyrillic, bool allow_icons) {
  if (!dest || dest_size == 0) return;
  if (!src) { dest[0] = 0; return; }
  size_t j = 0;
  bool last_was_block = false;
  bool after_zwj = false;

  for (size_t i = 0; src[i] != 0 && j < dest_size - 1; ) {
    unsigned char c = (unsigned char)src[i];
    if (c < 0x80) {
      after_zwj = false;
      if (c >= 32 && c <= 126) {
        dest[j++] = (char)c;
        last_was_block = false;
      }
      i++;
      continue;
    }

    // Decode multi-byte UTF-8 sequence (short-circuiting prevents reading past NUL)
    uint32_t cp = 0xFFFD;
    size_t seq_len = 1;
    if ((c & 0xE0) == 0xC0 &&
        ((unsigned char)src[i + 1] & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x1F) << 6) |
           ((unsigned char)src[i + 1] & 0x3F);
      seq_len = 2;
    } else if ((c & 0xF0) == 0xE0 &&
               ((unsigned char)src[i + 1] & 0xC0) == 0x80 &&
               ((unsigned char)src[i + 2] & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x0F) << 12) |
           ((uint32_t)((unsigned char)src[i + 1] & 0x3F) << 6) |
           ((unsigned char)src[i + 2] & 0x3F);
      seq_len = 3;
    } else if ((c & 0xF8) == 0xF0 &&
               ((unsigned char)src[i + 1] & 0xC0) == 0x80 &&
               ((unsigned char)src[i + 2] & 0xC0) == 0x80 &&
               ((unsigned char)src[i + 3] & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x07) << 18) |
           ((uint32_t)((unsigned char)src[i + 1] & 0x3F) << 12) |
           ((uint32_t)((unsigned char)src[i + 2] & 0x3F) << 6) |
           ((unsigned char)src[i + 3] & 0x3F);
      seq_len = 4;
    } else {
      // Malformed/stray high byte: consume any trailing continuation bytes
      while (src[i + seq_len] && ((unsigned char)src[i + seq_len] & 0xC0) == 0x80) {
        seq_len++;
      }
    }

    if (cp == 0x200D) {
      after_zwj = true;
      i += seq_len;
      continue;
    }

    if (isIgnoredEmojiModifier(cp) || (after_zwj && (cp == 0x2640 || cp == 0x2642))) {
      after_zwj = false;
      i += seq_len;
      continue;
    }
    after_zwj = false;

    if (allow_cyrillic && seq_len == 2 && cp <= 0xFFFF && cyrillicGlyphIndex((uint16_t)cp) >= 0) {
      if (j + 2 < dest_size) {
        dest[j++] = src[i];
        dest[j++] = src[i + 1];
        last_was_block = false;
      } else {
        break;
      }
      i += seq_len;
      continue;
    }

    const EmojiEntry* entry = findEmojiEntry(cp);

    // Every emoji becomes exactly ONE displayed cell (fixed-width lines on small OLEDs):
    // its own icon, else the generic icon; without icons a single ASCII character or '?'.
    if (isEmojiCodepoint(cp)) {
      if (allow_icons) {
        int icon = (entry && entry->icon >= 0 && entry->icon < 64) ? entry->icon : EMOJI_GENERIC;
        // Encode U+E000 + icon as 3-byte UTF-8: 0xEE 0x80 (0x80 | icon)
        if (j + 3 < dest_size) {
          dest[j++] = (char)0xEE;
          dest[j++] = (char)0x80;
          dest[j++] = (char)(0x80 | (uint8_t)icon);
        } else {
          break;
        }
      } else {
        char ch = (entry && entry->ascii && entry->ascii[0] && entry->ascii[1] == 0) ? entry->ascii[0] : '?';
        if (j + 1 < dest_size) dest[j++] = ch;
        else break;
      }
      last_was_block = false;
      i += seq_len;
      continue;
    }

    // Smart punctuation and arrows from the table
    if (entry) {
      if (allow_icons && entry->icon >= 0 && entry->icon < 64) {
        if (j + 3 < dest_size) {
          dest[j++] = (char)0xEE;
          dest[j++] = (char)0x80;
          dest[j++] = (char)(0x80 | (uint8_t)entry->icon);
          last_was_block = false;
        } else {
          break;
        }
        i += seq_len;
        continue;
      }
      if (entry->ascii) {
        size_t len = strlen(entry->ascii);
        if (j + len < dest_size) {
          memcpy(dest + j, entry->ascii, len);
          j += len;
          last_was_block = false;
        } else {
          break;
        }
        i += seq_len;
        continue;
      }
    }

    // Unknown non-ASCII character: collapse consecutive unknowns into one solid block
    if (!last_was_block) {
      dest[j++] = '\xDB';
      last_was_block = true;
    }
    i += seq_len;
  }

  dest[j] = 0;
}
