#include <gtest/gtest.h>

// Header-only drop statistics for the repeater packet filter.
#include "../../src/helpers/filter/FilterStats.h"
#include "../../src/helpers/filter/PathBlock.h"
#include "../../src/helpers/filter/SenderRules.h"

// ---- Buf: bounded, always-terminated appender -------------------------------

TEST(FilterStatsBuf, AppendsPlainText) {
  char out[16];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_TRUE(b.add("ab%d", 12));

  EXPECT_STREQ("ab12", out);
  EXPECT_EQ(4u, b.len());
  EXPECT_FALSE(b.truncated());
}

TEST(FilterStatsBuf, EmptyUntilFirstAppend) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_STREQ("", out);
  EXPECT_EQ(0u, b.len());
}

TEST(FilterStatsBuf, FillsExactlyToCapacity) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_TRUE(b.add("0123456"));

  EXPECT_STREQ("0123456", out);
  EXPECT_EQ(7u, b.len());
  EXPECT_FALSE(b.truncated());
}

TEST(FilterStatsBuf, RejectsOversizedAppendWholesale) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_FALSE(b.add("0123456789"));

  EXPECT_STREQ("", out) << "an append that does not fit must not leave a partial fragment";
  EXPECT_EQ(0u, b.len());
  EXPECT_TRUE(b.truncated());
}

TEST(FilterStatsBuf, RejectsAppendThatWouldNotFitWithoutPartialWrite) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abcde"));

  EXPECT_FALSE(b.add("XYZ"));

  EXPECT_STREQ("abcde", out) << "a rejected append must not leave a partial fragment";
  EXPECT_EQ(5u, b.len());
  EXPECT_TRUE(b.truncated());
}

TEST(FilterStatsBuf, IgnoresFurtherAppendsOnceTruncated) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abcde"));
  ASSERT_FALSE(b.add("XYZ"));

  EXPECT_FALSE(b.add("!")) << "a short append must not sneak in after truncation";

  EXPECT_STREQ("abcde", out);
}

TEST(FilterStatsBuf, HandlesZeroCapacityWithoutWriting) {
  char canary[4] = {'\x7f', '\x7f', '\x7f', '\x7f'};
  FilterStat::Buf b(canary, 0);

  EXPECT_FALSE(b.add("x"));

  EXPECT_EQ('\x7f', canary[0]);
  EXPECT_EQ(0u, b.len());
}

static const size_t REPLY_CAP = 160;

// Saturates every counter, the worst case any formatter has to survive.
static void saturate(Counters& c) {
  for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) c.hash_type[i] = 0xFFFFFFFFu;
  c.hash = c.channel = c.malformed = c.advert = c.path = c.air_ms = c.age = 0xFFFFFFFFu;
  c.block = c.block_local = c.ingress = c.newkey_advert = c.newkey_anon = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_HASH_SIZE_COUNT; i++) c.hash_size[i] = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_CHANNEL_COUNT; i++) c.channel_slot[i] = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_PATH_COUNT; i++) c.path_slot[i] = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_BLOCK_COUNT; i++) c.block_slot[i] = 0xFFFFFFFFu;
  for (int i = 0; i < MALFORMED_REASON_COUNT; i++) c.malformed_reason[i] = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_SRC_COUNT; i++) c.src[i] = 0xFFFF;
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    c.sender_slot[i] = c.text_slot[i] = c.sender_pass[i] = c.text_pass[i] = 0xFFFFFFFFu;
  }
}

// ---- Counters ---------------------------------------------------------------

TEST(FilterStatsCounters, RecordHashSplitsBySizeAndType) {
  Counters c;

  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_ADVERT);
  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_TXT_MSG);
  FilterStat::recordHash(c, 3, PAYLOAD_TYPE_ADVERT);

  EXPECT_EQ(3u, c.hash);
  EXPECT_EQ(2u, c.hash_size[0]) << "hash size 1 lives at index 0";
  EXPECT_EQ(1u, c.hash_size[2]);
  EXPECT_EQ(2u, c.hash_type[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(1u, c.hash_type[PAYLOAD_TYPE_TXT_MSG]);
}

TEST(FilterStatsCounters, RecordHashKeepsTotalWhenBreakdownIsOutOfRange) {
  Counters c;

  FilterStat::recordHash(c, 0, 0xFF);

  EXPECT_EQ(1u, c.hash) << "a drop must never be lost from the total";
  EXPECT_EQ(0u, c.hash_size[0]);
}

TEST(FilterStatsCounters, RecordChannelSplitsBySlot) {
  Counters c;

  FilterStat::recordChannel(c, 5);
  FilterStat::recordChannel(c, 5);

  EXPECT_EQ(2u, c.channel);
  EXPECT_EQ(2u, c.channel_slot[5]);
  EXPECT_EQ(0u, c.channel_slot[4]);
}

TEST(FilterStatsCounters, RecordChannelKeepsTotalWhenSlotIsOutOfRange) {
  Counters c;

  FilterStat::recordChannel(c, FILTER_CHANNEL_COUNT);

  EXPECT_EQ(1u, c.channel);
}

TEST(FilterStatsCounters, RecordMalformedSplitsByReason) {
  Counters c;

  FilterStat::recordMalformed(c, MALFORMED_UTF8);

  EXPECT_EQ(1u, c.malformed);
  EXPECT_EQ(1u, c.malformed_reason[MALFORMED_UTF8]);
  EXPECT_EQ(0u, c.malformed_reason[MALFORMED_TIMESTAMP]);
}

TEST(FilterStatsCounters, RecordSrcCountsPerHashByte) {
  Counters c;

  FilterStat::recordSrc(c, 0xa3);
  FilterStat::recordSrc(c, 0xa3);
  FilterStat::recordSrc(c, 0x00);

  EXPECT_EQ(2u, c.src[0xa3]);
  EXPECT_EQ(1u, c.src[0x00]);
  EXPECT_EQ(0u, c.src[0xa4]);
}

TEST(FilterStatsCounters, SaturatesInsteadOfWrappingTo32BitZero) {
  Counters c;
  c.hash = 0xFFFFFFFFu;
  c.hash_size[0] = 0xFFFFFFFFu;

  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_ADVERT);

  EXPECT_EQ(0xFFFFFFFFu, c.hash);
  EXPECT_EQ(0xFFFFFFFFu, c.hash_size[0]);
}

TEST(FilterStatsCounters, SaturatesInsteadOfWrappingTo16BitZero) {
  Counters c;
  c.src[0xa3] = 0xFFFF;

  FilterStat::recordSrc(c, 0xa3);

  EXPECT_EQ(0xFFFFu, c.src[0xa3]);
}

TEST(FilterStatsBuf, MarkTruncatedDoesNothingWhenEverythingFitted) {
  char out[16];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abc"));

  b.markTruncated("..");

  EXPECT_STREQ("abc", out);
}

TEST(FilterStatsBuf, MarkTruncatedAppendsMarkerWhenRoomIsLeft) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("ab"));
  ASSERT_FALSE(b.add("cdefghij"));

  b.markTruncated("..");

  EXPECT_STREQ("ab..", out) << "a clipped list must not read as a complete one";
}

TEST(FilterStatsBuf, MarkTruncatedOverwritesTailWhenBufferIsFull) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("0123456"));
  ASSERT_FALSE(b.add("x"));

  b.markTruncated("..");

  EXPECT_STREQ("01234..", out);
  EXPECT_EQ(7u, strlen(out));
}

// ---- topSrc -----------------------------------------------------------------

TEST(FilterStatsTopSrc, ReportsNothingWhenNoSourceWasRecorded) {
  Counters c;
  FilterStat::TopEntry top[8];

  EXPECT_EQ(0, FilterStat::topSrc(c, top, 8));
}

TEST(FilterStatsTopSrc, OrdersByCountDescending) {
  Counters c;
  c.src[0x11] = 190;
  c.src[0xa3] = 412;
  c.src[0x5c] = 288;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(3, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0xa3, top[0].hash);
  EXPECT_EQ(412, top[0].count);
  EXPECT_EQ(0x5c, top[1].hash);
  EXPECT_EQ(0x11, top[2].hash);
}

TEST(FilterStatsTopSrc, SkipsZeroCounts) {
  Counters c;
  c.src[0x00] = 0;
  c.src[0xff] = 1;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(1, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0xff, top[0].hash);
}

TEST(FilterStatsTopSrc, BreaksTiesByLowestHashByte) {
  Counters c;
  c.src[0x9f] = 7;
  c.src[0x02] = 7;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(2, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0x02, top[0].hash) << "ordering must be deterministic across calls";
  EXPECT_EQ(0x9f, top[1].hash);
}

TEST(FilterStatsTopSrc, StopsAtRequestedMaximum) {
  Counters c;
  for (int i = 0; i < 20; i++) c.src[i] = (uint16_t)(100 + i);
  FilterStat::TopEntry top[8];

  EXPECT_EQ(3, FilterStat::topSrc(c, top, 3));

  EXPECT_EQ(19, top[0].hash) << "highest count first";
}

TEST(FilterStatsHash, SplitsTheTotalOverThePathHashSizes) {
  Counters c;
  c.hash = 8097;
  c.hash_size[0] = 8097;
  char out[160];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_STREQ("> Blocked 8097 [1B:8097 2B:0 3B:0]", out);
}

TEST(FilterStatsHash, ListsTopBlockedTypes) {
  Counters c;
  c.hash = 3;
  c.hash_type[PAYLOAD_TYPE_ADVERT] = 2;
  c.hash_type[PAYLOAD_TYPE_TXT_MSG] = 1;
  char out[160];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_NE(nullptr, strstr(out, "\n  Top types: 04:2 02:1")) << "got: " << out;
}

TEST(FilterStatsHash, ShowsTheFourByteBucketOnlyWhenItWasUsed) {
  Counters c;
  c.hash = 1;
  c.hash_size[3] = 1;
  char out[160];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_NE(nullptr, strstr(out, "4B:1")) << "got: " << out;
}

TEST(FilterStatsHash, SaysSoWhenNothingWasDropped) {
  Counters c;
  char out[160];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no path hash drops recorded", out);
}

TEST(FilterStatsHash, StaysWithinTheReplyBufferAtSaturatedCounters) {
  Counters c;
  saturate(c);
  char out[160];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

TEST(FilterStatsMalformed, SplitsTheTotalByRejectionReason) {
  Counters c;
  c.malformed = 12;
  c.malformed_reason[MALFORMED_SHORT] = 1;
  c.malformed_reason[MALFORMED_TIMESTAMP] = 8;
  c.malformed_reason[MALFORMED_UTF8] = 3;
  char out[160];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_STREQ("> Blocked 12 [ short:1 time:8 empty:0 utf8:3 ]", out);
}

TEST(FilterStatsMalformed, SaysSoWhenNothingWasDropped) {
  Counters c;
  char out[160];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no malformed drops recorded", out);
}

TEST(FilterStatsMalformed, StaysWithinTheReplyBufferAtSaturatedCounters) {
  Counters c;
  saturate(c);
  char out[160];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

TEST(FilterStatsTop, ListsTheWorstSourceHashes) {
  Counters c;
  c.src[0xa3] = 412;
  c.src[0x5c] = 288;
  char out[160];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_STREQ("> Top drops: a3:412 5c:288", out);
}

TEST(FilterStatsTop, SaysSoWhenNothingWasRecorded) {
  Counters c;
  char out[160];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no source drops recorded", out);
}

TEST(FilterStatsTop, StaysWithinTheReplyBufferWhenEverySourceDrops) {
  Counters c;
  saturate(c);
  char out[160];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

// ---- Counters: fork-derived additions (advert origin, path block, airtime) --

TEST(FilterStatsCounters, RecordAdvertCountsOriginDrops) {
  Counters c;

  FilterStat::recordAdvert(c);
  FilterStat::recordAdvert(c);

  EXPECT_EQ(2u, c.advert);
}

TEST(FilterStatsCounters, RecordPathSplitsBySlot) {
  Counters c;

  FilterStat::recordPath(c, 0);
  FilterStat::recordPath(c, 3);
  FilterStat::recordPath(c, 3);

  EXPECT_EQ(3u, c.path);
  EXPECT_EQ(1u, c.path_slot[0]);
  EXPECT_EQ(2u, c.path_slot[3]);
}

TEST(FilterStatsCounters, RecordPathKeepsTotalWhenSlotIsOutOfRange) {
  Counters c;

  FilterStat::recordPath(c, FILTER_PATH_COUNT);
  FilterStat::recordPath(c, -1);

  EXPECT_EQ(2u, c.path);
  for (int i = 0; i < FILTER_PATH_COUNT; i++) EXPECT_EQ(0u, c.path_slot[i]);
}

TEST(FilterStatsCounters, RecordAirAccumulatesSaturating) {
  Counters c;

  FilterStat::recordAir(c, 1500);
  FilterStat::recordAir(c, 250);
  EXPECT_EQ(1750u, c.air_ms);

  FilterStat::recordAir(c, 0xFFFFFFFFu);
  EXPECT_EQ(0xFFFFFFFFu, c.air_ms);
}

TEST(FilterStatsCounters, ResetClearsTheNewBucketsToo) {
  Counters c;
  FilterStat::recordAdvert(c);
  FilterStat::recordPath(c, 1);
  FilterStat::recordAir(c, 9);

  c.reset();

  EXPECT_EQ(0u, c.advert);
  EXPECT_EQ(0u, c.path);
  EXPECT_EQ(0u, c.path_slot[1]);
  EXPECT_EQ(0u, c.air_ms);
}

// ---- `filter` summary: dry-run marker ----------------------------------------

TEST(FilterStatsAdvert, ReportsWindowDropsAndCacheFill) {
  Counters c;
  c.advert = 42;
  char out[160];

  FilterStat::formatStatsAdvert(out, sizeof(out), c, 48, 17, 256);

  EXPECT_STREQ("> Advert origins: window 48h, dropped 42, cache 17/256", out);
}

TEST(FilterStatsAdvert, SaysSoWhenTheLimiterIsOff) {
  Counters c;
  char out[160];

  FilterStat::formatStatsAdvert(out, sizeof(out), c, 0, 0, 256);

  EXPECT_STREQ("> Filter: advert origin limit off", out);
}

// ---- `filter stats path` and `filter path list` -------------------------------

TEST(FilterStatsPath, ListsEveryPrefixWithItsDrops) {
  Counters c;
  c.path_slot[0] = 5;
  c.path_slot[2] = 0;
  PathPrefix list[FILTER_PATH_COUNT] = {};
  ASSERT_TRUE(FilterPath::parse("A1", &list[0]));
  ASSERT_TRUE(FilterPath::parse("B2C3", &list[2]));
  char out[160];

  FilterStat::formatPathList(out, sizeof(out), list, &c);

  EXPECT_STREQ("A1: 5,B2C3: 0", out);
}

TEST(FilterStatsPath, ListsPrefixesWithoutCountsForTheSettingView) {
  Counters c;
  PathPrefix list[FILTER_PATH_COUNT] = {};
  ASSERT_TRUE(FilterPath::parse("A1", &list[0]));
  ASSERT_TRUE(FilterPath::parse("B2C3", &list[2]));
  char out[160];

  FilterStat::formatPathList(out, sizeof(out), list, nullptr);

  EXPECT_STREQ("A1,B2C3", out);
}

TEST(FilterStatsPath, SaysNoneWhenNothingIsBlocked) {
  PathPrefix list[FILTER_PATH_COUNT] = {};
  char out[160];

  FilterStat::formatPathList(out, sizeof(out), list, nullptr);

  EXPECT_STREQ("None", out);
}

// ---- `filter stats air` --------------------------------------------------------

TEST(FilterStatsAir, ReportsSavedAirtimeInMillisecondsAndHumanUnits) {
  Counters c;
  c.air_ms = 214500;
  char out[160];

  FilterStat::formatStatsAir(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: saved airtime 214500 ms (3m 34s)", out);
}

TEST(FilterStatsAir, SaysSoWhenNothingWasSaved) {
  Counters c;
  char out[160];

  FilterStat::formatStatsAir(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no airtime saved yet", out);
}

// ---- prefs migration helper ----------------------------------------------------

TEST(FilterStatsCounters, RecordSenderAndTextSplitBySlot) {
  Counters c;

  FilterStat::recordSender(c, 2);
  FilterStat::recordSender(c, 2);
  FilterStat::recordText(c, 0);
  FilterStat::recordText(c, FILTER_RULE_COUNT);

  EXPECT_EQ(2u, c.sender);
  EXPECT_EQ(2u, c.sender_slot[2]);
  EXPECT_EQ(2u, c.text);
  EXPECT_EQ(1u, c.text_slot[0]);
}

static void mkRule(SenderRule& r, const char* name, uint16_t secs, uint8_t prob) {
  memset(&r, 0, sizeof(r));
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.secs = secs;
  r.prob = prob;
}

TEST(FilterStatsRules, ListsEveryRuleWithItsMode) {
  SenderRule rules[FILTER_RULE_COUNT] = {};
  mkRule(rules[0], "Bot*", 60, 100);
  mkRule(rules[2], "Spam", 0, 30);
  mkRule(rules[3], "Alice", 0, 100);
  char out[160];

  FilterStat::formatRuleList(out, sizeof(out), rules, FILTER_RULE_COUNT, nullptr, nullptr);

  EXPECT_STREQ("Bot* throttle 60s,Spam block 30%,Alice block", out);
}

TEST(FilterStatsRules, ListsDropsAndPassesForTheStatsView) {
  SenderRule rules[FILTER_RULE_COUNT] = {};
  mkRule(rules[0], "Bot*", 60, 100);
  mkRule(rules[1], "Spam", 0, 100);
  uint32_t drops[FILTER_RULE_COUNT] = { 12, 5 };
  uint32_t passes[FILTER_RULE_COUNT] = { 3, 0 };
  char out[160];

  FilterStat::formatRuleList(out, sizeof(out), rules, FILTER_RULE_COUNT, drops, passes);

  EXPECT_STREQ("Bot*: 12 (pass 3),Spam: 5", out);
}

TEST(FilterStatsRules, SaysNoneWhenNoRuleIsSet) {
  SenderRule rules[FILTER_RULE_COUNT] = {};
  char out[160];

  FilterStat::formatRuleList(out, sizeof(out), rules, FILTER_RULE_COUNT, nullptr, nullptr);

  EXPECT_STREQ("None", out);
}

TEST(FilterStatsRules, WorksForTextRulesToo) {
  TextRule rules[FILTER_RULE_COUNT] = {};
  memset(&rules[0], 0, sizeof(rules[0]));
  strncpy(rules[0].text, "^BEACON", sizeof(rules[0].text) - 1);
  rules[0].secs = 0;
  rules[0].prob = 100;
  char out[160];

  FilterStat::formatRuleList(out, sizeof(out), rules, FILTER_RULE_COUNT, nullptr, nullptr);

  EXPECT_STREQ("^BEACON block", out);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---- message age -------------------------------------------------------------

TEST(FilterStatsAge, RecordAgeCountsDrops) {
  Counters c;
  FilterStat::recordAge(c);
  FilterStat::recordAge(c);
  EXPECT_EQ(2u, c.age);
}

TEST(FilterStatsAge, SaysOffWhenNoLimitIsSet) {
  Counters c;
  char out[160];
  FilterStat::formatStatsAge(out, sizeof(out), c, 0, true);
  EXPECT_STREQ("> Filter: message age limit off", out);
}

TEST(FilterStatsAge, ShowsLimitAndDrops) {
  Counters c;
  c.age = 7;
  char out[160];
  FilterStat::formatStatsAge(out, sizeof(out), c, 30, true);
  EXPECT_STREQ("> Message age: max 30m, dropped 7", out);
}

TEST(FilterStatsAge, FlagsAnUnsetClock) {
  Counters c;
  char out[160];
  FilterStat::formatStatsAge(out, sizeof(out), c, 30, false);
  EXPECT_STREQ("> Message age: max 30m, dropped 0 (clock not set, inactive)", out);
}

TEST(FilterStatsAge, ResetClearsTheCounter) {
  Counters c;
  c.age = 3;
  c.reset();
  EXPECT_EQ(0u, c.age);
}
