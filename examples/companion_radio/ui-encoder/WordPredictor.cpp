#include "WordPredictor.h"
#include <string.h>
#include "Dictionary.h"

// One byte per letter: Latin a..z as ASCII, Cyrillic а..я as 0xE0..0xFF. Upper case is folded.
// Returns the length, or -1 if the word has anything else in it (digits, punctuation, ...).
static int encodeWord(const char* s, uint8_t* out, int max_len, bool* all_upper) {
  int n = 0;
  bool upper = true;
  const uint8_t* p = (const uint8_t*)s;
  while (*p) {
    uint16_t cp;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      cp = ((uint16_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else {
      return -1;
    }
    uint8_t b;
    if (cp >= 'a' && cp <= 'z') { b = cp; upper = false; }
    else if (cp >= 'A' && cp <= 'Z') b = cp + 32;
    else if (cp >= 0x0430 && cp <= 0x044F) { b = 0xE0 + (cp - 0x0430); upper = false; }
    else if (cp >= 0x0410 && cp <= 0x042F) b = 0xE0 + (cp - 0x0410);
    else return -1;
    if (n >= max_len) return -1;
    out[n++] = b;
  }
  if (all_upper) *all_upper = upper;
  return n;
}

static void decodeLetters(const uint8_t* src, int len, bool upper, char* dest, size_t dest_size) {
  size_t j = 0;
  for (int i = 0; i < len; i++) {
    uint8_t b = src[i];
    if (b < 0x80) {
      if (j + 1 >= dest_size) break;
      dest[j++] = upper ? b - 32 : b;
    } else {
      if (j + 2 >= dest_size) break;
      uint16_t cp = (upper ? 0x0410 : 0x0430) + (b - 0xE0);
      dest[j++] = 0xC0 | (cp >> 6);
      dest[j++] = 0x80 | (cp & 0x3F);
    }
  }
  dest[j] = 0;
}

WordPredictor::WordPredictor() {
  memset(_recent, 0, sizeof(_recent));
}

bool WordPredictor::suggest(const char* prefix, char* rest, size_t rest_size) const {
  uint8_t key[PREDICT_MAX_WORD];
  bool upper = false;
  int n = encodeWord(prefix, key, sizeof(key), &upper);
  if (n <= 0) return false;
  if (n < 2) upper = false;   // one capital letter is just the start of a sentence

  for (int i = 0; i < PREDICT_RECENT_WORDS; i++) {
    const uint8_t* w = _recent[i];
    int len = strlen((const char*)w);
    if (len > n && memcmp(w, key, n) == 0) {
      decodeLetters(w + n, len - n, upper, rest, rest_size);
      return true;
    }
  }

  // the dictionary follows the script of the first letter
  const uint8_t* w = (const uint8_t*)(key[0] >= 0xE0 ? DICT_BG : DICT_EN);
  while (*w) {
    int len = strlen((const char*)w);
    if (len > n && memcmp(w, key, n) == 0) {
      decodeLetters(w + n, len - n, upper, rest, rest_size);
      return true;
    }
    w += len + 1;
  }
  return false;
}

bool WordPredictor::isKnownPrefix(const char* word) const {
  uint8_t key[PREDICT_MAX_WORD];
  int n = encodeWord(word, key, sizeof(key), NULL);
  if (n <= 0) return false;
  for (int i = 0; i < PREDICT_RECENT_WORDS; i++) {
    const uint8_t* w = _recent[i];
    if ((int)strlen((const char*)w) >= n && memcmp(w, key, n) == 0) return true;
  }
  const uint8_t* w = (const uint8_t*)(key[0] >= 0xE0 ? DICT_BG : DICT_EN);
  while (*w) {
    int len = strlen((const char*)w);
    if (len >= n && memcmp(w, key, n) == 0) return true;
    w += len + 1;
  }
  return false;
}

void WordPredictor::learn(const char* word) {
  uint8_t key[PREDICT_MAX_WORD + 1];
  int n = encodeWord(word, key, PREDICT_MAX_WORD, NULL);
  if (n < 3) return;   // short words are already easy to type
  key[n] = 0;
  int found = PREDICT_RECENT_WORDS - 1;   // drop the oldest unless the word is already known
  for (int i = 0; i < PREDICT_RECENT_WORDS; i++) {
    if (strcmp((const char*)_recent[i], (const char*)key) == 0) { found = i; break; }
  }
  memmove(_recent[1], _recent[0], found * sizeof(_recent[0]));
  memcpy(_recent[0], key, n + 1);
}
