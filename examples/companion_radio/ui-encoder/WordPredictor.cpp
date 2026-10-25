#include "WordPredictor.h"
#include <string.h>
#include <ctype.h>
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
  memset(_user, 0, sizeof(_user));
  _num_user = 0;
  _user_dirty = false;
  memset(_pairs, 0, sizeof(_pairs));
  _num_pairs = 0;
  _pairs_dirty = false;
}

static uint16_t wordHash(const uint8_t* w, int n) {
  uint16_t h = 0x9E37;
  for (int i = 0; i < n; i++) h = (uint16_t)((h ^ w[i]) * 0x3B1);
  return h ? h : 1;
}

// the first word of the list that starts with key[0..n) and is longer, or NULL
static const uint8_t* findInList(const uint8_t* w, const uint8_t* key, int n) {
  while (*w) {
    int len = strlen((const char*)w);
    if (len > n && memcmp(w, key, n) == 0) return w;
    w += len + 1;
  }
  return NULL;
}

static bool listHas(const uint8_t* w, const uint8_t* key, int n) {
  while (*w) {
    int len = strlen((const char*)w);
    if (len == n && memcmp(w, key, n) == 0) return true;
    w += len + 1;
  }
  return false;
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

  for (int i = 0; i < _num_user; i++) {
    const uint8_t* w = _user[i];
    int len = strlen((const char*)w);
    if (len > n && memcmp(w, key, n) == 0) {
      decodeLetters(w + n, len - n, upper, rest, rest_size);
      return true;
    }
  }

  // the dictionary follows the script of the first letter
  const uint8_t* w = findInList((const uint8_t*)(key[0] >= 0xE0 ? DICT_BG : DICT_EN), key, n);
  if (w == NULL) return false;
  int len = strlen((const char*)w);
  decodeLetters(w + n, len - n, upper, rest, rest_size);
  return true;
}

bool WordPredictor::inDictionary(const char* word) const {
  uint8_t key[PREDICT_MAX_WORD];
  int n = encodeWord(word, key, sizeof(key), NULL);
  if (n <= 0) return false;
  return listHas((const uint8_t*)(key[0] >= 0xE0 ? DICT_BG : DICT_EN), key, n);
}

void WordPredictor::learnText(const char* text) {
  char word[PREDICT_MAX_WORD * 2 + 1];
  const char* p = text;
  uint8_t prev[PREDICT_MAX_WORD + 1];
  int prev_len = 0;   // the word before, 0 = none (start, or after punctuation)
  while (*p) {
    // a word: letters only (ASCII or 2-byte UTF-8); anything else ends it
    int j = 0;
    bool ok = true;
    while (*p && (((unsigned char)*p >= 0x80) || isalpha((unsigned char)*p))) {
      if (j < (int)sizeof(word) - 1) word[j++] = *p; else ok = false;
      p++;
    }
    word[j] = 0;
    char sep = *p;
    if (*p) p++;
    uint8_t key[PREDICT_MAX_WORD + 1];
    int n = ok && j > 0 ? encodeWord(word, key, PREDICT_MAX_WORD, NULL) : -1;
    if (n > 0 && prev_len > 0) learnPair(prev, prev_len, key, n);
    if (n > 0 && (sep == ' ' || sep == 0)) {   // "word word": a pair; punctuation breaks the chain
      memcpy(prev, key, n);
      prev_len = n;
    } else {
      prev_len = 0;
    }
    if (n < 3) continue;   // short words are already easy to type
    if (listHas((const uint8_t*)(key[0] >= 0xE0 ? DICT_BG : DICT_EN), key, n)) continue;
    key[n] = 0;
    int found = -1;
    for (int i = 0; i < _num_user; i++) {
      if (strcmp((const char*)_user[i], (const char*)key) == 0) { found = i; break; }
    }
    if (found == 0) continue;   // already the most recent
    if (found < 0) {
      if (_num_user < PREDICT_USER_WORDS) _num_user++;
      found = _num_user - 1;   // a new word: the least recently used one drops out when full
      _user_dirty = true;      // a known word only moves to the front: saved with the next new one
    }
    memmove(_user[1], _user[0], found * sizeof(_user[0]));
    memcpy(_user[0], key, n + 1);
  }
}

void WordPredictor::clearUser() {
  memset(_user, 0, sizeof(_user));
  _num_user = 0;
  _user_dirty = true;
  memset(_pairs, 0, sizeof(_pairs));
  _num_pairs = 0;
  _pairs_dirty = true;
}

int WordPredictor::exportUser(uint8_t* buf, int max_len) {
  int len = 0;
  for (int i = 0; i < _num_user; i++) {
    int l = strlen((const char*)_user[i]);
    if (len + l + 1 > max_len) break;
    memcpy(&buf[len], _user[i], l + 1);
    len += l + 1;
  }
  return len;   // still dirty until markUserSaved(): the write may fail
}

void WordPredictor::importUser(const uint8_t* buf, int len) {
  memset(_user, 0, sizeof(_user));
  _num_user = 0;
  int i = 0;
  while (i < len && _num_user < PREDICT_USER_WORDS) {
    int l = 0;
    while (i + l < len && buf[i + l]) l++;
    if (l >= 3 && l <= PREDICT_MAX_WORD) {
      bool valid = true;   // letters as the encoder writes them: a..z, 0xE0..0xFF
      for (int k = 0; k < l; k++) {
        uint8_t b = buf[i + k];
        if (!((b >= 'a' && b <= 'z') || b >= 0xE0)) valid = false;
      }
      if (valid) {
        memcpy(_user[_num_user], &buf[i], l);
        _user[_num_user][l] = 0;
        _num_user++;
      }
    }
    i += l + 1;
  }
  _user_dirty = false;
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

void WordPredictor::learnPair(const uint8_t* prev, int prev_len, const uint8_t* next, int next_len) {
  if (next_len < 2 || next_len > PREDICT_PAIR_WORD) return;   // one letter is no help
  uint16_t h = wordHash(prev, prev_len);
  int found = -1;
  for (int i = 0; i < _num_pairs; i++) {
    if (_pairs[i].prev == h && (int)strlen((const char*)_pairs[i].next) == next_len
        && memcmp(_pairs[i].next, next, next_len) == 0) { found = i; break; }
  }
  if (found == 0) return;
  if (found < 0) {
    if (_num_pairs < PREDICT_PAIRS) _num_pairs++;
    found = _num_pairs - 1;   // full: the least recently used pair drops out
  }
  memmove(&_pairs[1], &_pairs[0], found * sizeof(WordPair));
  _pairs[0].prev = h;
  memcpy(_pairs[0].next, next, next_len);
  _pairs[0].next[next_len] = 0;
  _pairs_dirty = true;
}

bool WordPredictor::suggestNext(const char* prev, char* word, size_t word_size) const {
  uint8_t key[PREDICT_MAX_WORD];
  int n = encodeWord(prev, key, sizeof(key), NULL);
  if (n <= 0) return false;
  uint16_t h = wordHash(key, n);
  for (int i = 0; i < _num_pairs; i++) {
    if (_pairs[i].prev == h) {
      decodeLetters(_pairs[i].next, strlen((const char*)_pairs[i].next), false, word, word_size);
      return true;
    }
  }
  return false;
}

int WordPredictor::exportPairs(uint8_t* buf, int max_len) {
  int len = 0;
  for (int i = 0; i < _num_pairs; i++) {
    int l = strlen((const char*)_pairs[i].next);
    if (len + 3 + l > max_len) break;
    buf[len++] = _pairs[i].prev & 0xFF;
    buf[len++] = _pairs[i].prev >> 8;
    buf[len++] = l;
    memcpy(&buf[len], _pairs[i].next, l);
    len += l;
  }
  return len;
}

void WordPredictor::importPairs(const uint8_t* buf, int len) {
  memset(_pairs, 0, sizeof(_pairs));
  _num_pairs = 0;
  int i = 0;
  while (i + 3 <= len && _num_pairs < PREDICT_PAIRS) {
    uint16_t h = buf[i] | (buf[i + 1] << 8);
    int l = buf[i + 2];
    if (l < 2 || l > PREDICT_PAIR_WORD || i + 3 + l > len) break;
    bool valid = true;
    for (int k = 0; k < l; k++) {
      uint8_t b = buf[i + 3 + k];
      if (!((b >= 'a' && b <= 'z') || b >= 0xE0)) valid = false;
    }
    if (valid) {
      _pairs[_num_pairs].prev = h;
      memcpy(_pairs[_num_pairs].next, &buf[i + 3], l);
      _pairs[_num_pairs].next[l] = 0;
      _num_pairs++;
    }
    i += 3 + l;
  }
  _pairs_dirty = false;
}
