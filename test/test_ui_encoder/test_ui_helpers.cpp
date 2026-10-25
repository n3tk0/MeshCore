#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-encoder/UIHelpers.h"

// ---- UTF-8

TEST(UIHelpers, Utf8LenCountsCharactersNotBytes) {
  EXPECT_EQ(0, ui::utf8Len(""));
  EXPECT_EQ(5, ui::utf8Len("hello"));
  EXPECT_EQ(4, ui::utf8Len("Здр!"));
}

TEST(UIHelpers, Utf8SkipAndSliceStayOnCharacterBoundaries) {
  const char* s = "Аз съм";
  EXPECT_STREQ("съм", ui::utf8Skip(s, 3));
  EXPECT_STREQ("", ui::utf8Skip(s, 100));

  char buf[16];
  ui::utf8Slice(buf, sizeof(buf), s, 3, 2);
  EXPECT_STREQ("съ", buf);
  // a tiny buffer truncates instead of overflowing
  ui::utf8Slice(buf, 3, s, 0, 6);
  EXPECT_EQ(2u, strlen(buf));
}

TEST(UIHelpers, WrapBreaksAtTheLastSpaceThatFits) {
  const char* text = "Waiting at the entrance for 10 minutes.";
  uint8_t starts[8], lens[8];
  int n = ui::wrapLines(text, 21, starts, lens, 8);
  ASSERT_EQ(3, n);
  EXPECT_EQ(0, starts[0]);
  EXPECT_EQ(14, lens[0]);                   // "Waiting at the"
  EXPECT_EQ(std::string("entrance for 10"), std::string(text + starts[1], lens[1]));
  EXPECT_EQ(std::string("minutes."), std::string(text + starts[2]));
}

TEST(UIHelpers, WrapSplitsAWordLongerThanALine) {
  const char* text = "абвгдежзийклмнопрстуфхцч";   // 24 Cyrillic letters, no space
  uint8_t starts[4], lens[4];
  int n = ui::wrapLines(text, 21, starts, lens, 4);
  ASSERT_EQ(2, n);
  EXPECT_EQ(21, lens[0]);
  EXPECT_EQ(3, lens[1]);
  EXPECT_EQ(42, starts[1]);                 // 21 two-byte characters
}

TEST(UIHelpers, WrapStopsAtMaxLines) {
  uint8_t starts[2], lens[2];
  EXPECT_EQ(2, ui::wrapLines("a b c d e f g h", 3, starts, lens, 2));
  EXPECT_EQ(0, ui::wrapLines("   ", 21, starts, lens, 2));
}

// ---- battery

TEST(UIHelpers, LipoPercentFollowsTheCurve) {
  EXPECT_EQ(0, ui::lipoPercent(3000));
  EXPECT_EQ(0, ui::lipoPercent(3300));      // shutdown voltage is empty
  EXPECT_EQ(25, ui::lipoPercent(3700));
  EXPECT_EQ(80, ui::lipoPercent(4000));
  EXPECT_EQ(100, ui::lipoPercent(4200));
  EXPECT_EQ(100, ui::lipoPercent(4350));
  EXPECT_EQ(68, ui::lipoPercent(3920));     // interpolated between 3900 and 4000
}

TEST(UIHelpers, LipoPercentNeverDecreasesWithVoltage) {
  int prev = 0;
  for (int mv = 3000; mv <= 4300; mv += 5) {
    int p = ui::lipoPercent((uint16_t)mv);
    EXPECT_GE(p, prev) << mv;
    prev = p;
  }
}

// ---- time

TEST(UIHelpers, CivilDateRoundTrip) {
  EXPECT_EQ(0, ui::daysFromCivil(1970, 1, 1));
  EXPECT_EQ(11016, ui::daysFromCivil(2000, 2, 29));
  for (int32_t z = 0; z < 30000; z += 37) {
    int32_t y; uint32_t m, d;
    ui::civilFromDays(z, y, m, d);
    EXPECT_EQ(z, ui::daysFromCivil(y, m, d));
  }
}

TEST(UIHelpers, EuSummerTimeEdges2026) {
  const uint32_t start = 1774746000UL;   // 2026-03-29 01:00 UTC
  const uint32_t end = 1792890000UL;     // 2026-10-25 01:00 UTC
  EXPECT_EQ(start, ui::lastSunday0100(2026, 3));
  EXPECT_EQ(end, ui::lastSunday0100(2026, 10));
  EXPECT_FALSE(ui::euSummerTime(start - 1));
  EXPECT_TRUE(ui::euSummerTime(start));
  EXPECT_TRUE(ui::euSummerTime(end - 1));
  EXPECT_FALSE(ui::euSummerTime(end));
  EXPECT_TRUE(ui::euSummerTime(1782907200UL));    // 2026-07-01
  EXPECT_FALSE(ui::euSummerTime(1768478400UL));   // 2026-01-15
}

TEST(UIHelpers, EuSummerTimeEdges2024) {
  EXPECT_EQ(1711846800UL, ui::lastSunday0100(2024, 3));    // 2024-03-31
  EXPECT_EQ(1729990800UL, ui::lastSunday0100(2024, 10));   // 2024-10-27
}

TEST(UIHelpers, LocalTimeAddsZoneAndSummerHour) {
  const uint32_t july = 1782907200UL, jan = 1768478400UL;
  EXPECT_EQ(july + 3 * 3600, ui::localTime(july, 2, true));
  EXPECT_EQ(july + 2 * 3600, ui::localTime(july, 2, false));
  EXPECT_EQ(jan + 2 * 3600, ui::localTime(jan, 2, true));
  EXPECT_EQ(jan - 5 * 3600, ui::localTime(jan, -5, true));
}

// ---- encoder acceleration

TEST(UIHelpers, AccelerationOnlyForFastTurnsInOneDirection) {
  EXPECT_EQ(1, ui::accelSteps(10, false));
  EXPECT_EQ(5, ui::accelSteps(10, true));
  EXPECT_EQ(3, ui::accelSteps(45, true));
  EXPECT_EQ(2, ui::accelSteps(80, true));
  EXPECT_EQ(1, ui::accelSteps(100, true));
  EXPECT_EQ(1, ui::accelSteps(5000, true));
}

TEST(UIHelpers, QuietHoursWrapPastMidnight) {
  const uint32_t day = 20000 * 86400UL;
  EXPECT_FALSE(ui::quietNow(day + 23 * 3600, 0));        // off
  EXPECT_TRUE(ui::quietNow(day + 23 * 3600, 1));         // 22-07
  EXPECT_TRUE(ui::quietNow(day + 3 * 3600, 1));
  EXPECT_FALSE(ui::quietNow(day + 7 * 3600, 1));         // ends at 7:00
  EXPECT_FALSE(ui::quietNow(day + 21 * 3600 + 59 * 60, 1));
  EXPECT_FALSE(ui::quietNow(day + 23 * 3600, 3));        // 00-07
  EXPECT_TRUE(ui::quietNow(day + 30 * 60, 3));
  EXPECT_FALSE(ui::quietNow(day + 2 * 3600, QUIET_PRESETS));   // a bad value is off
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
