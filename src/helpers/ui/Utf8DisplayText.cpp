#include "Utf8DisplayText.h"
#include "CyrillicFont5x8.h"
#include <string.h>

namespace {

struct EmojiAsciiEntry {
  uint32_t cp;
  const char* ascii;
};

// Sorted by Unicode code point for binary search.
const EmojiAsciiEntry kEmojiAsciiTable[] = {
  // Common punctuation / typographic characters from mobile keyboards
  { 0x00A0, " " },         // NBSP
  { 0x00AB, "\"" },        // «
  { 0x00BB, "\"" },        // »
  { 0x2013, "-" },         // – en-dash
  { 0x2014, "-" },         // — em-dash
  { 0x2018, "'" },         // ‘
  { 0x2019, "'" },         // ’
  { 0x201C, "\"" },        // “
  { 0x201D, "\"" },        // ”
  { 0x201E, "\"" },        // „
  { 0x2026, "..." },       // …
  { 0x203C, "!!" },        // ‼️
  { 0x2049, "!?" },        // ⁉️
  { 0x2190, "<-" },        // ←
  { 0x2192, "->" },        // →
  { 0x2600, "[sun]" },     // ☀️
  { 0x2601, "[cloud]" },   // ☁️
  { 0x2615, "[coffee]" },  // ☕
  { 0x2639, ":(" },        // ☹️
  { 0x263A, ":)" },        // ☺️
  { 0x26A0, "[!]" },       // ⚠️
  { 0x26A1, "[zap]" },     // ⚡
  { 0x26F0, "[mtn]" },     // ⛰️
  { 0x2705, "[OK]" },      // ✅
  { 0x270C, "V" },         // ✌️
  { 0x2714, "[OK]" },      // ✔️
  { 0x2716, "[X]" },       // ✖️
  { 0x2728, "*" },         // ✨
  { 0x2744, "[snow]" },    // ❄️
  { 0x274C, "[X]" },       // ❌
  { 0x2753, "?" },         // ❓
  { 0x2757, "!" },         // ❗
  { 0x2764, "<3" },        // ❤️
  { 0x27A1, "->" },        // ➡️
  { 0x2B05, "<-" },        // ⬅️
  { 0x2B06, "^" },         // ⬆️
  { 0x2B07, "v" },         // ⬇️
  { 0x2B50, "*" },         // ⭐

  // 4-byte Unicode emojis (sorted by code point)
  { 0x1F31F, "*" },        // 🌟
  { 0x1F327, "[rain]" },   // 🌧️
  { 0x1F37A, "[beer]" },   // 🍺
  { 0x1F37B, "[cheers]" }, // 🍻
  { 0x1F389, "[!]" },      // 🎉
  { 0x1F3D4, "[mtn]" },    // 🏔️
  { 0x1F440, "o_o" },      // 👀
  { 0x1F44B, "o/" },       // 👋
  { 0x1F44C, "OK" },       // 👌
  { 0x1F44D, "+1" },       // 👍
  { 0x1F44E, "-1" },       // 👎
  { 0x1F44F, "[clap]" },   // 👏
  { 0x1F47B, "[ghost]" },  // 👻
  { 0x1F480, "[skull]" },  // 💀
  { 0x1F494, "</3" },      // 💔
  { 0x1F495, "<3" },       // 💕
  { 0x1F496, "<3" },       // 💖
  { 0x1F499, "<3" },       // 💙
  { 0x1F49A, "<3" },       // 💚
  { 0x1F49B, "<3" },       // 💛
  { 0x1F49C, "<3" },       // 💜
  { 0x1F4A5, "[boom]" },   // 💥
  { 0x1F4A9, "[poop]" },   // 💩
  { 0x1F4AA, "[flex]" },   // 💪
  { 0x1F4AF, "100" },      // 💯
  { 0x1F4CD, "[pin]" },    // 📍
  { 0x1F4E1, "[ant]" },    // 📡
  { 0x1F4FB, "[radio]" },  // 📻
  { 0x1F50B, "[bat]" },    // 🔋
  { 0x1F525, "[fire]" },   // 🔥
  { 0x1F5A4, "<3" },       // 🖤
  { 0x1F600, ":D" },       // 😀
  { 0x1F601, ":D" },       // 😁
  { 0x1F602, ":D" },       // 😂
  { 0x1F603, ":D" },       // 😃
  { 0x1F604, ":D" },       // 😄
  { 0x1F605, ":')" },      // 😅
  { 0x1F606, "XD" },       // 😆
  { 0x1F607, "O:)" },      // 😇
  { 0x1F608, ">:)" },      // 😈
  { 0x1F609, ";)" },       // 😉
  { 0x1F60A, ":)" },       // 😊
  { 0x1F60B, ":P" },       // 😋
  { 0x1F60C, ":)" },       // 😌
  { 0x1F60D, "<3" },       // 😍
  { 0x1F60E, "B)" },       // 😎
  { 0x1F60F, ";)" },       // 😏
  { 0x1F610, ":|" },       // 😐
  { 0x1F611, "-_-" },      // 😑
  { 0x1F612, ":/" },       // 😒
  { 0x1F614, ":(" },       // 😔
  { 0x1F615, ":/" },       // 😕
  { 0x1F618, ":*" },       // 😘
  { 0x1F61B, ":P" },       // 😛
  { 0x1F61C, ";P" },       // 😜
  { 0x1F61D, "XP" },       // 😝
  { 0x1F61E, ":(" },       // 😞
  { 0x1F620, ">:(" },      // 😠
  { 0x1F621, ">:(" },      // 😡
  { 0x1F622, ":'(" },      // 😢
  { 0x1F624, ">:(" },      // 😤
  { 0x1F629, ":(" },       // 😩
  { 0x1F62C, ":E" },       // 😬
  { 0x1F62D, ":'(" },      // 😭
  { 0x1F62E, ":O" },       // 😮
  { 0x1F631, ":O" },       // 😱
  { 0x1F632, ":O" },       // 😲
  { 0x1F634, "Zzz" },      // 😴
  { 0x1F635, "X_X" },      // 😵
  { 0x1F641, ":(" },       // 🙁
  { 0x1F642, ":)" },       // 🙂
  { 0x1F644, ":/" },       // 🙄
  { 0x1F64F, "[pray]" },   // 🙏
  { 0x1F697, "[car]" },    // 🚗
  { 0x1F6B2, "[bike]" },   // 🚲
  { 0x1F90D, "<3" },       // 🤍
  { 0x1F912, "[sick]" },   // 🤒
  { 0x1F914, ":?" },       // 🤔
  { 0x1F918, "\\m/" },     // 🤘
  { 0x1F919, "[call]" },   // 🤙
  { 0x1F91D, "[shake]" },  // 🤝
  { 0x1F91E, "[luck]" },   // 🤞
  { 0x1F921, "[clown]" },  // 🤡
  { 0x1F923, ":D" },       // 🤣
  { 0x1F926, "[fp]" },     // 🤦
  { 0x1F929, "*:)" },      // 🤩
  { 0x1F92B, "shh" },      // 🤫
  { 0x1F92C, "#@!" },      // 🤬
  { 0x1F92D, ":x" },       // 🤭
  { 0x1F92F, ":O" },       // 🤯
  { 0x1F937, "\\_/" },     // 🤷
  { 0x1F970, ":)" },       // 🥰
  { 0x1F973, ":D" },       // 🥳
  { 0x1F97A, ":(" },       // 🥺
  { 0x1F9E1, "<3" },       // 🧡
  { 0x1FAE1, "o7" },       // 🫡
};

// Variation selectors, keycap combiner, and Fitzpatrick skin-tone modifiers.
bool isIgnoredEmojiModifier(uint32_t cp) {
  return cp == 0x20E3 || cp == 0xFE0E || cp == 0xFE0F ||
         (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

}  // namespace

const char* emojiToAscii(uint32_t cp) {
  size_t lo = 0;
  size_t hi = sizeof(kEmojiAsciiTable) / sizeof(kEmojiAsciiTable[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (kEmojiAsciiTable[mid].cp == cp) return kEmojiAsciiTable[mid].ascii;
    if (kEmojiAsciiTable[mid].cp < cp) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

void translateUTF8ForDisplay(char* dest, const char* src, size_t dest_size, bool allow_cyrillic) {
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

    const char* ascii = emojiToAscii(cp);
    if (ascii) {
      size_t len = strlen(ascii);
      if (j + len < dest_size) {
        memcpy(dest + j, ascii, len);
        j += len;
        last_was_block = false;
      } else {
        break;
      }
      i += seq_len;
      continue;
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
