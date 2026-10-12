#pragma once

// Word completion for the encoder keyboard: the most frequent dictionary word (BG or EN, picked by
// the script of the typed prefix) that starts with what has been typed, with words typed recently
// on this device tried first.

#include <stddef.h>
#include <stdint.h>

#ifndef PREDICT_RECENT_WORDS
  #define PREDICT_RECENT_WORDS 16
#endif
#ifndef PREDICT_USER_WORDS
  #define PREDICT_USER_WORDS 128   // learned words kept on flash (names, slang: not in the dictionary)
#endif
#define PREDICT_MAX_WORD 24   // encoded bytes (one per letter)

class WordPredictor {
  uint8_t _recent[PREDICT_RECENT_WORDS][PREDICT_MAX_WORD + 1];   // most recent first, encoded
  uint8_t _user[PREDICT_USER_WORDS][PREDICT_MAX_WORD + 1];       // learned, most recently used first
  int _num_user;
  bool _user_dirty;

public:
  WordPredictor();

  // prefix: the UTF-8 word being typed. On success 'rest' gets the UTF-8 letters that complete it
  // (lowercase, or uppercase when the whole prefix is in capitals).
  bool suggest(const char* prefix, char* rest, size_t rest_size) const;

  // remember a word the user typed or accepted (kept in RAM until reboot)
  void learn(const char* word);

  // a sent message: its words that the dictionary lacks become learned words (kept on flash)
  void learnText(const char* text);
  int userCount() const { return _num_user; }
  void clearUser();
  bool userDirty() const { return _user_dirty; }
  void markUserSaved() { _user_dirty = false; }   // after the file was written
  // file image: the encoded words, NUL separated; returns the length
  int exportUser(uint8_t* buf, int max_len);
  void importUser(const uint8_t* buf, int len);
  bool inDictionary(const char* word) const;
};
