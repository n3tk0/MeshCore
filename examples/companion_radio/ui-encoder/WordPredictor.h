#pragma once

// Word completion for the encoder keyboard: the most frequent dictionary word (BG or EN, picked by
// the script of the typed prefix) that starts with what has been typed, with words typed recently
// on this device tried first.

#include <stddef.h>
#include <stdint.h>

#ifndef PREDICT_RECENT_WORDS
  #define PREDICT_RECENT_WORDS 16
#endif
#define PREDICT_MAX_WORD 24   // encoded bytes (one per letter)

class WordPredictor {
  uint8_t _recent[PREDICT_RECENT_WORDS][PREDICT_MAX_WORD + 1];   // most recent first, encoded

public:
  WordPredictor();

  // prefix: the UTF-8 word being typed. On success 'rest' gets the UTF-8 letters that complete it
  // (lowercase, or uppercase when the whole prefix is in capitals).
  bool suggest(const char* prefix, char* rest, size_t rest_size) const;

  // true if 'word' is a dictionary (or recently typed) word, or the start of one
  bool isKnownPrefix(const char* word) const;

  // remember a word the user typed or accepted (kept in RAM until reboot)
  void learn(const char* word);
};
