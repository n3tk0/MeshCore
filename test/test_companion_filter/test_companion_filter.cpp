#include <gtest/gtest.h>

#include <string>
#include <vector>

// The companion receive filter: verdicts (drop / hide / pass), the block list, hidden channels,
// dry-run, the CLI and the saved file.
#include "../../src/helpers/filter/CompanionFilter.h"

namespace {

const uint32_t NOW_SECS = 1790000000;

struct MemSink {
  std::vector<uint8_t> v;
  void put(const void* d, size_t n) { v.insert(v.end(), (const uint8_t*)d, (const uint8_t*)d + n); }
};
struct MemSrc {
  const std::vector<uint8_t>& v;
  size_t at = 0;
  explicit MemSrc(const std::vector<uint8_t>& x) : v(x) {}
  bool get(void* d, size_t n) {
    if (at + n > v.size()) return false;
    memcpy(d, v.data() + at, n);
    at += n;
    return true;
  }
};

uint8_t KEY_A[32], KEY_B[32];
void initKeys() {
  for (int i = 0; i < 32; i++) { KEY_A[i] = (uint8_t)(0xA0 + i); KEY_B[i] = (uint8_t)(0x10 + i); }
}

mesh::Packet advertFrom(const uint8_t* key, uint8_t route = ROUTE_TYPE_FLOOD) {
  mesh::Packet p;
  p.header = route | (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  p.setPathHashSizeAndCount(1, 0);
  memcpy(p.payload, key, 32);
  p.payload_len = 100;
  return p;
}

// plaintext of a group text: timestamp, flags, "Sender: text"
std::vector<uint8_t> groupText(uint32_t ts, const char* body) {
  std::vector<uint8_t> d(5);
  memcpy(d.data(), &ts, 4);
  d[4] = 0;
  d.insert(d.end(), body, body + strlen(body));
  return d;
}

CFilterVerdict judge(CompanionFilter& f, const std::vector<uint8_t>& d, uint32_t chan = 0) {
  return f.checkGroupText(d.data(), (int)d.size(), NOW_SECS, 1000, 0, chan);
}

std::string cli(CompanionFilter& f, const char* cmd, bool* changed = nullptr) {
  char reply[CFILTER_REPLY_SIZE + 40];
  memset(reply, 'Z', sizeof(reply));
  reply[sizeof(reply) - 1] = '\0';
  bool c = f.handleCommand(cmd, reply, NOW_SECS);
  if (changed) *changed = c;
  EXPECT_LT(strlen(reply), (size_t)CFILTER_REPLY_SIZE) << cmd << " -> " << reply;
  return reply;
}

}  // namespace

class CompanionFilterTest : public ::testing::Test {
protected:
  void SetUp() override { initKeys(); }
};

TEST_F(CompanionFilterTest, OffPassesEverything) {
  CompanionFilter f;
  f.c.addBlockKey(KEY_A);
  mesh::Packet p = advertFrom(KEY_A);
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&p));
  EXPECT_FALSE(f.isBlocked(KEY_A));
  EXPECT_FALSE(f.isPrefixBlocked(KEY_A, 8));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS, "Bob: hi")));
}

TEST_F(CompanionFilterTest, BlockedAdvertIsDropped) {
  CompanionFilter f;
  EXPECT_TRUE(f.blockKey(KEY_A));   // also switches the filter on
  EXPECT_TRUE(f.enabled);
  mesh::Packet a = advertFrom(KEY_A), b = advertFrom(KEY_B);
  EXPECT_EQ(CFilterVerdict::DROP, f.checkFlood(&a));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&b));
  EXPECT_EQ(1u, f.c.cnt.block);
  EXPECT_TRUE(f.isBlocked(KEY_A));
  EXPECT_FALSE(f.isBlocked(KEY_B));
}

TEST_F(CompanionFilterTest, DirectPacketsAreNotJudgedByCheckFlood) {
  CompanionFilter f;
  f.blockKey(KEY_A);
  mesh::Packet p = advertFrom(KEY_A, ROUTE_TYPE_DIRECT);
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&p));   // the zero-hop advert is caught in onAdvertRecv
  EXPECT_TRUE(f.isBlocked(KEY_A));
}

TEST_F(CompanionFilterTest, ShortPrefixMatchesLooselyOnly) {
  CompanionFilter f;
  f.blockKey(KEY_A);
  EXPECT_TRUE(f.isPrefixBlocked(KEY_A, 8));    // a discovery answer carries 8 bytes
  EXPECT_FALSE(f.isPrefixBlocked(KEY_B, 8));
  EXPECT_FALSE(f.isPrefixBlocked(KEY_A, 3));   // too short to say
}

TEST_F(CompanionFilterTest, PathBlockDropsFloodAdvert) {
  CompanionFilter f;
  f.enabled = true;
  EXPECT_TRUE(f.c.path_block[0].len == 0);
  EXPECT_EQ(0, FilterBlock::count(f.c.block, FILTER_BLOCK_COUNT));
  bool changed = false;
  cli(f, "filter path add 77", &changed);
  EXPECT_TRUE(changed);
  mesh::Packet p = advertFrom(KEY_B);
  p.path[0] = 0x77;
  p.setPathHashSizeAndCount(1, 1);
  EXPECT_EQ(CFilterVerdict::DROP, f.checkFlood(&p));
  p.path[0] = 0x78;
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&p));
}

TEST_F(CompanionFilterTest, SenderRuleHidesButAgeDrops) {
  CompanionFilter f;
  f.enabled = true;
  ASSERT_TRUE(f.c.addSenderName("Spammer"));
  EXPECT_EQ(CFilterVerdict::HIDE, judge(f, groupText(NOW_SECS, "Spammer: buy now")));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS, "Bob: hello")));
  EXPECT_EQ(1u, f.c.cnt.sender);

  f.c.age_mins = 10;
  EXPECT_EQ(CFilterVerdict::DROP, judge(f, groupText(NOW_SECS - 3600, "Bob: old")));   // junk is dropped, not hidden
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS - 60, "Bob: fresh")));
}

TEST_F(CompanionFilterTest, MalformedIsDropped) {
  CompanionFilter f;
  f.enabled = true;
  f.c.malformed = true;
  EXPECT_EQ(CFilterVerdict::DROP, judge(f, groupText(NOW_SECS, "Bob: bad \xff\xfe bytes")));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS, "Bob: fine")));
}

TEST_F(CompanionFilterTest, HiddenChannelHidesOnlyThatChannel) {
  CompanionFilter f;
  EXPECT_FALSE(f.wantsGroupText());
  EXPECT_TRUE(f.hideChannel(0x11223344, "bg-mesh"));
  EXPECT_TRUE(f.enabled);
  EXPECT_TRUE(f.wantsGroupText());
  auto d = groupText(NOW_SECS, "Bob: hi");
  EXPECT_EQ(CFilterVerdict::HIDE, judge(f, d, 0x11223344));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, d, 0x55667788));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, d, 0));
  EXPECT_TRUE(f.hideChannel(0x11223344, "again"));   // twice is fine
  EXPECT_EQ(1, f.hiddenCount());
  EXPECT_TRUE(f.unhideSlot(0));
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, d, 0x11223344));
  EXPECT_FALSE(f.unhideSlot(0));
  EXPECT_FALSE(f.unhideSlot(99));
}

TEST_F(CompanionFilterTest, HiddenListFills) {
  CompanionFilter f;
  for (int i = 0; i < CFILTER_HIDDEN_COUNT; i++) EXPECT_TRUE(f.hideChannel(100 + i, "x"));
  EXPECT_FALSE(f.hideChannel(999, "full"));
  EXPECT_FALSE(f.hideChannel(0, "nokey"));
}

TEST_F(CompanionFilterTest, DryRunCountsButPasses) {
  CompanionFilter f;
  f.blockKey(KEY_A);
  f.c.addSenderName("Spammer");
  f.hideChannel(7, "c");
  f.dryrun = true;
  mesh::Packet a = advertFrom(KEY_A);
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&a));
  EXPECT_EQ(1u, f.c.cnt.block);
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS, "Spammer: x")));
  EXPECT_EQ(1u, f.c.cnt.sender);
  EXPECT_EQ(CFilterVerdict::PASS, judge(f, groupText(NOW_SECS, "Bob: x"), 7));
  EXPECT_FALSE(f.isBlocked(KEY_A));         // counted, not acted on
  EXPECT_FALSE(f.isPrefixBlocked(KEY_A, 8));
}

TEST_F(CompanionFilterTest, AlreadyJudgedRemembersRecentHashes) {
  CompanionFilter f;
  uint8_t h1[MAX_HASH_SIZE] = {1}, h2[MAX_HASH_SIZE] = {2};
  EXPECT_FALSE(f.alreadyJudged(h1));
  EXPECT_TRUE(f.alreadyJudged(h1));
  EXPECT_FALSE(f.alreadyJudged(h2));
  for (int i = 0; i < CFILTER_JUDGED_COUNT + 2; i++) {
    uint8_t h[MAX_HASH_SIZE] = {0};
    h[0] = (uint8_t)(10 + i);
    f.alreadyJudged(h);
  }
  EXPECT_FALSE(f.alreadyJudged(h1));   // pushed out of the small ring
}

TEST_F(CompanionFilterTest, CliBlockAddRemove) {
  CompanionFilter f;
  bool changed = false;
  EXPECT_TRUE(CompanionFilter::isCommand("filter"));
  EXPECT_TRUE(CompanionFilter::isCommand("filter on"));
  EXPECT_FALSE(CompanionFilter::isCommand("filters"));
  EXPECT_FALSE(CompanionFilter::isCommand("set name x"));

  cli(f, "filter on", &changed);
  EXPECT_TRUE(changed);
  EXPECT_TRUE(f.enabled);
  std::string hex = "A0A1A2A3A4A5A6A7";
  cli(f, ("filter block add " + hex).c_str(), &changed);
  EXPECT_TRUE(changed);
  mesh::Packet p = advertFrom(KEY_A);
  EXPECT_EQ(CFilterVerdict::DROP, f.checkFlood(&p));
  cli(f, ("filter block remove " + hex).c_str(), &changed);
  EXPECT_TRUE(changed);
  EXPECT_EQ(CFilterVerdict::PASS, f.checkFlood(&p));
  std::string bad = cli(f, "filter block add zz", &changed);
  EXPECT_FALSE(changed);
  EXPECT_FALSE(bad.empty());
}

TEST_F(CompanionFilterTest, CliDryrunResetAndErrors) {
  CompanionFilter f;
  bool changed = false;
  cli(f, "filter dryrun on", &changed);
  EXPECT_TRUE(changed);
  EXPECT_TRUE(f.dryrun);
  cli(f, "filter dryrun maybe", &changed);
  EXPECT_FALSE(changed);
  f.blockKey(KEY_A);
  cli(f, "filter reset", &changed);
  EXPECT_TRUE(changed);
  EXPECT_FALSE(f.enabled);
  EXPECT_FALSE(f.dryrun);
  EXPECT_FALSE(f.c.hasBlock());
  EXPECT_FALSE(cli(f, "filter nonsense").empty());
  EXPECT_FALSE(cli(f, "filter").empty());
  EXPECT_FALSE(cli(f, "filter help").empty());
  EXPECT_FALSE(cli(f, "filter stats").empty());
  EXPECT_FALSE(cli(f, "filter stats block").empty());
}

TEST_F(CompanionFilterTest, SaveLoadRoundTrip) {
  CompanionFilter f;
  f.blockKey(KEY_A);
  f.c.addSenderName("Spammer");
  f.c.age_mins = 30;
  f.c.malformed = true;
  f.hideChannel(0xCAFEBABE, "bg-mesh");
  f.dryrun = true;

  MemSink out;
  f.write(out);
  CompanionFilter g;
  MemSrc in(out.v);
  ASSERT_TRUE(g.read(in));
  EXPECT_TRUE(g.enabled);
  EXPECT_TRUE(g.dryrun);
  EXPECT_TRUE(g.c.malformed);
  EXPECT_EQ(30, g.c.age_mins);
  EXPECT_GE(g.c.blockSlot(KEY_A), 0);
  EXPECT_TRUE(g.isChannelHidden(0xCAFEBABE));
  EXPECT_STREQ("bg-mesh", g.hidden[0].name);
  EXPECT_EQ(f.signature(), g.signature());
}

TEST_F(CompanionFilterTest, ChangedTracksSavedState) {
  CompanionFilter f;
  f.markSaved();
  EXPECT_FALSE(f.changed());
  f.blockKey(KEY_A);
  EXPECT_TRUE(f.changed());
  f.markSaved();
  EXPECT_FALSE(f.changed());
  f.c.cnt.block++;   // counters are not saved: no flash write for them
  EXPECT_FALSE(f.changed());
}

TEST_F(CompanionFilterTest, DamagedFilesLeaveDefaultsOff) {
  CompanionFilter f;
  f.blockKey(KEY_A);
  f.hideChannel(5, "x");
  MemSink out;
  f.write(out);

  for (size_t cut = 0; cut < out.v.size(); cut++) {   // every truncation
    std::vector<uint8_t> part(out.v.begin(), out.v.begin() + cut);
    CompanionFilter g;
    g.enabled = true;
    MemSrc in(part);
    EXPECT_FALSE(g.read(in)) << cut;
    EXPECT_FALSE(g.enabled) << cut;
    EXPECT_FALSE(g.c.hasBlock()) << cut;
  }
  std::vector<uint8_t> bad = out.v;
  bad[0] = 'X';
  CompanionFilter g;
  MemSrc in(bad);
  EXPECT_FALSE(g.read(in));
  bad = out.v;
  bad[3] = 99;   // a version from the future
  CompanionFilter h;
  MemSrc in2(bad);
  EXPECT_FALSE(h.read(in2));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
