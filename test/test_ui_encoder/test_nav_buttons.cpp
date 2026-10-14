#include <gtest/gtest.h>

#include <helpers/ui/NavButtonDecoder.h>

// state = (A << 1) | B, 1 = high (released)
static const uint8_t IDLE = 0x03, A_DOWN = 0x01, B_DOWN = 0x02, BOTH = 0x00;

// a pin change at 'now', then the loop polls; returns what the poll gave
static int8_t edge(NavButtonDecoder& d, uint8_t state, uint32_t now, bool buttons) {
  d.onEdge(state);
  return d.poll(state, now, buttons);
}

TEST(NavButtons, ButtonAnswersOnPressAndIgnoresBounce) {
  NavButtonDecoder d;
  EXPECT_EQ(-1, edge(d, A_DOWN, 1000, true));   // A = previous, at once
  EXPECT_EQ(0, edge(d, IDLE, 1002, true));      // bounce
  EXPECT_EQ(0, edge(d, A_DOWN, 1003, true));
  EXPECT_EQ(0, d.poll(A_DOWN, 1100, true));
  EXPECT_EQ(0, edge(d, IDLE, 1150, true));      // released
  EXPECT_EQ(0, d.poll(IDLE, 1200, true));       // settled: nothing more
  EXPECT_EQ(1, edge(d, B_DOWN, 1300, true));    // B = next
}

TEST(NavButtons, HeldButtonRepeatsThenFaster) {
  NavButtonDecoder d;
  EXPECT_EQ(1, edge(d, B_DOWN, 0, true));
  int n = 0;
  uint32_t first = 0, last = 0, gap_end = 0;
  for (uint32_t t = 1; t <= 3000; t++) {
    if (d.poll(B_DOWN, t, true)) {
      if (!first) first = t;
      if (last) gap_end = t - last;
      last = t;
      n++;
    }
  }
  EXPECT_EQ((uint32_t)NavButtonDecoder::REPEAT_DELAY_MS, first);
  EXPECT_EQ((uint32_t)NavButtonDecoder::REPEAT_FAST_MS, gap_end);
  EXPECT_GT(n, 20);
}

TEST(NavButtons, TapShorterThanALoopStillCounts) {
  NavButtonDecoder d;
  d.onEdge(A_DOWN);   // pressed and released between two polls
  d.onEdge(IDLE);
  EXPECT_EQ(-1, d.poll(IDLE, 500, true));   // counted at once, once
  EXPECT_EQ(0, d.poll(IDLE, 530, true));
  EXPECT_EQ(0, d.poll(IDLE, 600, true));
}

TEST(NavButtons, DetectCountsAButtonOnlyOnRelease) {
  NavButtonDecoder d;
  EXPECT_EQ(0, edge(d, B_DOWN, 100, false));
  EXPECT_EQ(0, d.poll(B_DOWN, 900, false));   // held: no repeats while finding out
  EXPECT_EQ(0, edge(d, IDLE, 1000, false));
  EXPECT_EQ(1, d.poll(IDLE, 1030, false));
}

TEST(NavButtons, DetectNeverTakesAnEncoderTurnForAButton) {
  NavButtonDecoder d;
  uint32_t t = 100;
  const uint8_t turn[] = { A_DOWN, BOTH, B_DOWN, IDLE, B_DOWN, BOTH, A_DOWN, IDLE };   // slow detents, both ways
  for (uint8_t s : turn) {
    EXPECT_EQ(0, edge(d, s, t, false)) << (int)s;
    for (int k = 0; k < 60; k++) EXPECT_EQ(0, d.poll(s, ++t, false));
  }
}

TEST(NavButtons, BothButtonsTogetherDoNothing) {
  NavButtonDecoder d;
  d.onEdge(A_DOWN);
  d.onEdge(BOTH);
  EXPECT_EQ(0, d.poll(BOTH, 100, true));
  EXPECT_EQ(0, edge(d, IDLE, 200, true));
  EXPECT_EQ(0, d.poll(IDLE, 300, true));
}
