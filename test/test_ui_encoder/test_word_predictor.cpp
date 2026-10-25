#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-encoder/WordPredictor.cpp"

// ---- learned words (names, slang: what the dictionary does not have)

TEST(WordPredictor, LearnsOnlyWordsTheDictionaryLacks) {
  WordPredictor p;
  ASSERT_TRUE(p.inDictionary("здравей"));
  ASSERT_FALSE(p.inDictionary("лозенец"));
  p.learnText("Здравей, Лозенец! ok xyzzyq 42");
  EXPECT_EQ(2, p.userCount());   // "лозенец" and "xyzzyq"; short words and numbers are skipped
  EXPECT_TRUE(p.userDirty());

  char rest[48];
  ASSERT_TRUE(p.suggest("лозе", rest, sizeof(rest)));
  EXPECT_STREQ("нец", rest);
  ASSERT_TRUE(p.suggest("xyz", rest, sizeof(rest)));
  EXPECT_STREQ("zyq", rest);
}

TEST(WordPredictor, LearnedWordsKeepTheMostRecentFirstWithoutDuplicates) {
  WordPredictor p;
  p.learnText("qwertyab qwertycd");
  p.learnText("qwertyab");
  EXPECT_EQ(2, p.userCount());
  char rest[48];
  ASSERT_TRUE(p.suggest("qwerty", rest, sizeof(rest)));
  EXPECT_STREQ("ab", rest);
}

TEST(WordPredictor, LeastRecentlyUsedWordDropsOutWhenFull) {
  WordPredictor p;
  char text[32];
  for (int i = 0; i < PREDICT_USER_WORDS + 1; i++) {
    snprintf(text, sizeof(text), "zzq%c%cw", 'a' + i / 26, 'a' + i % 26);
    p.learnText(text);
  }
  EXPECT_EQ(PREDICT_USER_WORDS, p.userCount());
  char rest[48];
  EXPECT_FALSE(p.suggest("zzqaa", rest, sizeof(rest)));   // the first one learned is gone
  EXPECT_TRUE(p.suggest("zzqab", rest, sizeof(rest)));
}

TEST(WordPredictor, ExportImportRoundTripAndRejectsGarbage) {
  WordPredictor a;
  a.learnText("Лозенец xyzzyq");
  uint8_t buf[PREDICT_USER_WORDS * (PREDICT_MAX_WORD + 1)];
  int len = a.exportUser(buf, sizeof(buf));
  EXPECT_TRUE(a.userDirty());   // until the file is written
  a.markUserSaved();
  EXPECT_FALSE(a.userDirty());

  WordPredictor b;
  b.importUser(buf, len);
  EXPECT_EQ(2, b.userCount());
  char rest[48];
  ASSERT_TRUE(b.suggest("Лоз", rest, sizeof(rest)));
  EXPECT_STREQ("енец", rest);

  // a damaged file: bytes that are no letters, too short, no terminator at the end
  const uint8_t bad[] = { 'a', 'b', 0, '1', '2', '3', '4', 0, 'q', 'q', 'z', 'z' };
  WordPredictor c;
  c.importUser(bad, sizeof(bad));
  EXPECT_EQ(1, c.userCount());   // only "qqzz"
}

TEST(WordPredictor, ClearForgetsLearnedWords) {
  WordPredictor p;
  p.learnText("xyzzyq");
  p.clearUser();
  EXPECT_EQ(0, p.userCount());
  char rest[48];
  EXPECT_FALSE(p.suggest("xyzz", rest, sizeof(rest)));
}

TEST(WordPredictor, OnlyANewWordNeedsAWrite) {
  WordPredictor p;
  p.learnText("xyzzyq qqzzab");
  uint8_t buf[64];
  p.exportUser(buf, sizeof(buf));
  p.markUserSaved();                // written: clean
  p.learnText("xyzzyq");            // known, only moves to the front
  EXPECT_FALSE(p.userDirty());
  p.learnText("zzqwerty");
  EXPECT_TRUE(p.userDirty());
}

TEST(WordPredictor, SuggestsTheWordThatFollowedLast) {
  WordPredictor p;
  char w[48];
  EXPECT_FALSE(p.suggestNext("добро", w, sizeof(w)));
  p.learnText("Добро утро");
  ASSERT_TRUE(p.suggestNext("добро", w, sizeof(w)));
  EXPECT_STREQ("утро", w);
  ASSERT_TRUE(p.suggestNext("Добро", w, sizeof(w)));   // either case
  p.learnText("добро момче");
  ASSERT_TRUE(p.suggestNext("добро", w, sizeof(w)));
  EXPECT_STREQ("момче", w);                              // the most recent first
}

TEST(WordPredictor, PunctuationBreaksPairsAndOneLetterIsNoSuggestion) {
  WordPredictor p;
  char w[48];
  p.learnText("ok, see you a b");
  EXPECT_FALSE(p.suggestNext("ok", w, sizeof(w)));
  ASSERT_TRUE(p.suggestNext("see", w, sizeof(w)));
  EXPECT_STREQ("you", w);
  EXPECT_FALSE(p.suggestNext("you", w, sizeof(w)));   // "a": one letter
}

TEST(WordPredictor, PairsSurviveExportImport) {
  WordPredictor p, q;
  p.learnText("call me later");
  EXPECT_TRUE(p.pairsDirty());
  uint8_t buf[256];
  int n = p.exportPairs(buf, sizeof(buf));
  q.importPairs(buf, n);
  EXPECT_EQ(p.pairCount(), q.pairCount());
  char w[48];
  ASSERT_TRUE(q.suggestNext("me", w, sizeof(w)));
  EXPECT_STREQ("later", w);
  EXPECT_FALSE(q.pairsDirty());
  q.clearUser();
  EXPECT_FALSE(q.suggestNext("me", w, sizeof(w)));
}
