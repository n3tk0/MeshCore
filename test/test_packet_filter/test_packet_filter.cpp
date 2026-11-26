#include <gtest/gtest.h>

#include <string>
#include <vector>

// The repeater / room server filter, driven through its public interface: the decision
// (allowForward), the block list for local use, the `filter ...` CLI and the saved format.
#include "../../src/helpers/filter/PacketFilter.h"

namespace {

const uint32_t NOW_SECS = 1790000000;   // a set clock (after 2026)

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

mesh::Packet makePacket(uint8_t route, uint8_t type, uint8_t hash_size = 1) {
  mesh::Packet p;
  p.header = route | (type << PH_TYPE_SHIFT);
  p.setPathHashSizeAndCount(hash_size, 0);
  p.payload_len = 0;
  memset(p.payload, 0, sizeof(p.payload));
  return p;
}

uint8_t KEY_A[32], KEY_B[32];

void initKeys() {
  for (int i = 0; i < 32; i++) { KEY_A[i] = (uint8_t)(0xA0 + i); KEY_B[i] = (uint8_t)(0x10 + i); }
}

mesh::Packet advertFrom(const uint8_t* key, int path_entries = 0, uint8_t first_hop = 0x77) {
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT);
  memcpy(p.payload, key, 32);
  p.payload_len = 100;
  for (int i = 0; i < path_entries; i++) p.path[i] = (uint8_t)(first_hop + i);
  p.setPathHashSizeAndCount(1, path_entries);
  return p;
}

mesh::Packet anonFrom(const uint8_t* key) {
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ANON_REQ);
  p.payload[0] = 0x42;
  memcpy(&p.payload[1], key, 32);
  p.payload_len = 80;
  return p;
}

mesh::Packet floodAck(int path_entries, uint8_t first_hop) {
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK);
  p.payload_len = 4;
  for (int i = 0; i < path_entries; i++) p.path[i] = (uint8_t)(first_hop + i);
  p.setPathHashSizeAndCount(1, path_entries);
  return p;
}

std::string cli(PacketFilter& f, const char* cmd, int* saves = nullptr, uint32_t now_ms = 1000) {
  char buf[200];
  strncpy(buf, cmd, sizeof(buf));
  char reply[FILTER_REPLY_SIZE + 40];   // slack so an overflow shows up in the length check
  memset(reply, 'Z', sizeof(reply));
  reply[sizeof(reply) - 1] = '\0';
  int s = f.handleCommand(buf, reply, now_ms, NOW_SECS);
  if (saves) *saves = s;
  EXPECT_LT(strlen(reply), (size_t)FILTER_REPLY_SIZE) << cmd << " -> " << reply;
  return reply;
}

// The native crypto is a mock that cannot tell keys apart, so the filter gets a keyed fake: the "MAC"
// is the 16 secret bytes (not CIPHER_MAC_SIZE), and a decrypt with another secret fails like a real MAC check does.
int fakeDecrypt(const uint8_t* secret, uint8_t* dest, const uint8_t* src, int src_len) {
  if (src_len < 16 || memcmp(src, secret, 16) != 0) return 0;
  memcpy(dest, src + 16, src_len - 16);
  return src_len - 16;
}

// A group text of `ch` as fakeDecrypt() reads it: timestamp, flags, "Sender: text".
mesh::Packet grpText(const FilterChannel& ch, const char* text, uint32_t ts = NOW_SECS) {
  uint8_t plain[120];
  memcpy(plain, &ts, 4);
  plain[4] = 0;
  strcpy((char*)plain + 5, text);
  int plen = 5 + (int)strlen(text);
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
  p.payload[0] = ch.hash;
  memcpy(&p.payload[1], ch.secret, 16);
  memcpy(&p.payload[1 + 16], plain, plen);
  p.payload_len = (uint16_t)(1 + 16 + plen);
  return p;
}

// A channel name (not `avoid`) whose 1-byte hash equals that of `like`.
FilterChannel collidingChannel(const FilterChannel& like) {
  FilterChannel ch;
  for (int i = 0; i < 100000; i++) {
    char name[20];
    snprintf(name, sizeof(name), "#c%d", i);
    if (FilterChan::derive(name, &ch) && ch.hash == like.hash && strcmp(ch.name, like.name) != 0) return ch;
  }
  ADD_FAILURE() << "no colliding channel found";
  return ch;
}

bool allow(PacketFilter& f, const mesh::Packet& p, uint32_t now_ms = 1000, uint32_t now_secs = NOW_SECS) {
  return f.allowForward(&p, now_ms, now_secs);
}

}  // namespace

class PacketFilterTest : public ::testing::Test {
protected:
  void SetUp() override { initKeys(); }
};

// ---- off, direct, dry-run ---------------------------------------------------------------------

TEST_F(PacketFilterTest, EverythingPassesWhileOff) {
  PacketFilter f;
  f.c.addBlockKey(KEY_A);
  EXPECT_TRUE(allow(f, advertFrom(KEY_A)));
  EXPECT_FALSE(f.isBlocked(KEY_A));
  EXPECT_EQ(0u, f.c.cnt.block);
}

TEST_F(PacketFilterTest, DirectPacketsAreNeverFiltered) {
  PacketFilter f;
  f.p.enabled = true;
  f.c.addBlockKey(KEY_A);
  mesh::Packet p = advertFrom(KEY_A);
  p.header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  EXPECT_TRUE(allow(f, p));
  p.header = ROUTE_TYPE_TRANSPORT_DIRECT | (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  EXPECT_TRUE(allow(f, p));
  EXPECT_EQ(0u, f.c.cnt.block);
}

TEST_F(PacketFilterTest, DryRunCountsButStillForwards) {
  PacketFilter f;
  f.p.enabled = true;
  f.p.dryrun = true;
  f.c.addBlockKey(KEY_A);
  EXPECT_TRUE(allow(f, advertFrom(KEY_A)));
  EXPECT_EQ(1u, f.c.cnt.block);
  EXPECT_FALSE(f.isBlocked(KEY_A)) << "dry-run refuses nothing locally";
  EXPECT_EQ(1u, f.c.cnt.block_local) << "but counts what it would have refused";
}

TEST_F(PacketFilterTest, DryRunAppliesToEveryRule) {
  PacketFilter f;
  f.p.enabled = true;
  f.p.dryrun = true;
  f.p.min_hash = 2;
  mesh::Packet p = floodAck(0, 0);
  EXPECT_TRUE(allow(f, p));
  EXPECT_EQ(1u, f.c.cnt.hash);
}

// ---- block list ---------------------------------------------------------------------------------

TEST_F(PacketFilterTest, BlockedKeyAdvertAndAnonRequestAreNotForwarded) {
  PacketFilter f;
  f.p.enabled = true;
  EXPECT_TRUE(f.c.addBlockKey(KEY_A));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A)));
  EXPECT_FALSE(allow(f, anonFrom(KEY_A)));
  EXPECT_TRUE(allow(f, advertFrom(KEY_B)));
  EXPECT_TRUE(allow(f, anonFrom(KEY_B)));
  EXPECT_EQ(2u, f.c.cnt.block);
}

TEST_F(PacketFilterTest, BlockMatchesByPrefixAndNeedsAFullEntry) {
  PacketFilter f;
  f.p.enabled = true;
  char reply[200];
  char cmd[200];
  snprintf(cmd, sizeof(cmd), "filter block add %02X%02X%02X%02X", KEY_A[0], KEY_A[1], KEY_A[2], KEY_A[3]);
  EXPECT_EQ(PacketFilter::SAVE_PREFS, f.handleCommand(cmd, reply, 0, NOW_SECS));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A)));
  uint8_t other[32];
  memcpy(other, KEY_A, 32);
  other[4] ^= 0xFF;   // same 4-byte prefix
  EXPECT_FALSE(allow(f, advertFrom(other)));
  other[3] ^= 0xFF;   // different prefix
  EXPECT_TRUE(allow(f, advertFrom(other)));
}

TEST_F(PacketFilterTest, OnlyAdvertAndAnonRequestCarryAKey) {
  PacketFilter f;
  f.p.enabled = true;
  f.c.addBlockKey(KEY_A);
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
  memcpy(p.payload, KEY_A, 32);   // a DM with bytes that happen to equal the key
  p.payload_len = 60;
  EXPECT_TRUE(allow(f, p));
  mesh::Packet shortAdvert = advertFrom(KEY_A);
  shortAdvert.payload_len = 20;   // too short to hold a key: not judged by it
  EXPECT_TRUE(allow(f, shortAdvert));
}

TEST_F(PacketFilterTest, IsBlockedForLocalUse) {
  PacketFilter f;
  f.c.addBlockKey(KEY_A);
  EXPECT_FALSE(f.isBlocked(KEY_A)) << "filter off";
  f.p.enabled = true;
  EXPECT_TRUE(f.isBlocked(KEY_A));
  EXPECT_FALSE(f.isBlocked(KEY_B));
  EXPECT_EQ(1u, f.c.cnt.block_local);
  EXPECT_TRUE(f.isBlocked(KEY_A, false));
  EXPECT_EQ(1u, f.c.cnt.block_local) << "a pure query does not count";
}

TEST_F(PacketFilterTest, BlockListCliAddListRemoveClear) {
  PacketFilter f;
  int saves;
  char hexA[65], hexB[65];
  mesh::Utils::toHex(hexA, KEY_A, 32);
  hexA[64] = 0;
  snprintf(hexB, sizeof(hexB), "%02X%02X%02X%02X", KEY_B[0], KEY_B[1], KEY_B[2], KEY_B[3]);

  EXPECT_EQ("None", cli(f, "filter block list"));
  EXPECT_NE(std::string::npos, cli(f, (std::string("filter block add ") + hexA).c_str(), &saves).find("added"));
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ("Failed", cli(f, (std::string("filter block add ") + hexA).c_str(), &saves)) << "twice";
  EXPECT_EQ(0, saves);
  cli(f, (std::string("filter block add ") + hexB).c_str());
  std::string list = cli(f, "filter block list");
  EXPECT_NE(std::string::npos, list.find("A0A1A2A3A4A5A6A7+"));
  EXPECT_NE(std::string::npos, list.find(hexB));
  EXPECT_NE(std::string::npos, cli(f, (std::string("filter block remove ") + hexB).c_str(), &saves).find("removed"));
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ("Failed", cli(f, (std::string("filter block remove ") + hexB).c_str(), &saves));
  EXPECT_EQ(0, saves);
  EXPECT_NE(std::string::npos, cli(f, "filter block clear", &saves).find("cleared"));
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ("None", cli(f, "filter block list"));
  cli(f, "filter block clear", &saves);
  EXPECT_EQ(0, saves) << "nothing to change, nothing to write";
}

TEST_F(PacketFilterTest, BlockCliRejectsBadKeysAndAFullList) {
  PacketFilter f;
  EXPECT_NE(std::string::npos, cli(f, "filter block add 1234").find("8-64 hex"));
  EXPECT_NE(std::string::npos, cli(f, "filter block add zzzzzzzz").find("8-64 hex"));
  EXPECT_NE(std::string::npos, cli(f, "filter block add 123456789").find("8-64 hex")) << "odd digit count";
  std::string seventy(66, 'A');
  EXPECT_NE(std::string::npos, cli(f, ("filter block add " + seventy).c_str()).find("8-64 hex"));
  for (int i = 0; i < FILTER_BLOCK_COUNT; i++) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "filter block add %08X", 0x01000000 + i);
    EXPECT_NE(std::string::npos, cli(f, cmd).find("added")) << i;
  }
  EXPECT_NE(std::string::npos, cli(f, "filter block add FFFFFFFF").find("full"));
  // the list pages
  std::string p0 = cli(f, "filter block list");
  std::string p1 = cli(f, "filter block list 1");
  EXPECT_NE(p0, p1);
  EXPECT_NE(std::string::npos, cli(f, "filter block list 99").find("no page"));
}

TEST_F(PacketFilterTest, ReplyBufferFitsTheTaggedMeshReply) {
  // remote CLI: temp[166], reply at offset 5, minus the 3 byte "xx|" tag
  EXPECT_LE((size_t)FILTER_REPLY_SIZE, (size_t)(166 - 5 - 3));
}

TEST_F(PacketFilterTest, BlockListPagesWithoutCommasAndWithTheRightCount) {
  PacketFilter f;
  for (int i = 0; i < 10; i++) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "filter block add %02X%02X%02X%02X%02X%02X%02X%02X%02X", 0x10 + i, 1, 2, 3, 4, 5, 6, 7, 8);
    ASSERT_NE(std::string::npos, cli(f, cmd).find("added"));
  }
  EXPECT_EQ("1001020304050607+,1101020304050607+,1201020304050607+,1301020304050607+,"
            "1401020304050607+,1501020304050607+ [6/10 p0]", cli(f, "filter block list"));
  EXPECT_EQ("1601020304050607+,1701020304050607+,1801020304050607+,1901020304050607+ [10/10 p1]",
            cli(f, "filter block list 1"));
  // full keys, two a page
  std::string full0 = cli(f, "filter block list 0 full");
  EXPECT_EQ("100102030405060708,110102030405060708 [2/10 p0]", full0);
  EXPECT_EQ("180102030405060708,190102030405060708 [10/10 p4]", cli(f, "filter block list 4 full"));
}

TEST_F(PacketFilterTest, BlockRemoveTakesTheListedPrefixOfALongerKey) {
  PacketFilter f;
  std::string longkey(64, 'A');
  for (int i = 0; i < 64; i++) longkey[i] = "0123456789ABCDEF"[(i * 7 + 3) & 15];
  std::string pre = longkey.substr(0, 16);
  ASSERT_NE(std::string::npos, cli(f, ("filter block add " + longkey).c_str()).find("added"));
  // the list shows the prefix with a '+'; removing by that prefix works
  EXPECT_EQ(pre + "+", cli(f, "filter block list"));
  int saves = 0;
  EXPECT_NE(std::string::npos, cli(f, ("filter block remove " + pre).c_str(), &saves).find("removed"));
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ("None", cli(f, "filter block list"));

  // two entries sharing the prefix: ambiguous, nothing removed; the full key still works
  std::string other = pre + std::string(48, 'B');
  cli(f, ("filter block add " + longkey).c_str());
  cli(f, ("filter block add " + other).c_str());
  EXPECT_NE(std::string::npos, cli(f, ("filter block remove " + pre).c_str(), &saves).find("more than one"));
  EXPECT_EQ(0, saves);
  EXPECT_EQ(2, FilterBlock::count(f.c.block, FILTER_BLOCK_COUNT));
  EXPECT_NE(std::string::npos, cli(f, ("filter block remove " + longkey).c_str()).find("removed"));
  EXPECT_EQ(1, FilterBlock::count(f.c.block, FILTER_BLOCK_COUNT));
}

TEST_F(PacketFilterTest, MalformedScanSkipsAPacketWhoseMacDoesNotVerify) {
  PacketFilter f;
  f.p.enabled = true;
  f.c.malformed = true;
  mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
  p.payload[0] = FILTER_PUBLIC_HASH;
  p.payload_len = 1 + CIPHER_MAC_SIZE + 20;
  // the payload is zeros: the MAC of the Public secret does not verify
  EXPECT_TRUE(allow(f, p)) << "another channel with the same 1-byte hash, not malformed Public text";
  EXPECT_EQ(0u, f.c.cnt.malformed);
}

TEST_F(PacketFilterTest, AdvertWindowIsNotUsedUpByAPacketAnotherCheckDrops) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter advert 24");
  cli(f, "filter path add 77");
  EXPECT_FALSE(allow(f, advertFrom(KEY_A, 1, 0x77), 1000)) << "dropped by the path block";
  EXPECT_TRUE(allow(f, advertFrom(KEY_A, 1, 0x55), 2000)) << "the dropped copy did not start the window";
  EXPECT_FALSE(allow(f, advertFrom(KEY_A, 1, 0x56), 3000));
}

TEST_F(PacketFilterTest, NewKeyQuotaIsNotUsedUpByAPacketAnotherCheckDrops) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 24 60");
  cli(f, "filter ingress 1 60");
  EXPECT_TRUE(allow(f, floodAck(1, 0x66), 1000)) << "uses the ingress budget of 66";
  mesh::Packet a = advertFrom(KEY_A, 1, 0x66);
  EXPECT_FALSE(allow(f, a, 1100, NOW_SECS)) << "dropped by the ingress limit";
  EXPECT_TRUE(allow(f, advertFrom(KEY_A, 1, 0x67), 1200, NOW_SECS + 1)) << "the quota is still unused";
  EXPECT_FALSE(allow(f, advertFrom(KEY_A, 1, 0x68), 1300, NOW_SECS + 2));
}

TEST_F(PacketFilterTest, AirtimeIsLabelledWouldSaveInDryRun) {
  PacketFilter f;
  f.setAirFn([](void*, int) -> uint32_t { return 1500; }, nullptr);
  f.p.enabled = true;
  f.c.addBlockKey(KEY_A);
  allow(f, advertFrom(KEY_A));
  EXPECT_NE(std::string::npos, cli(f, "filter stats air").find("saved airtime 1500"));
  f.p.dryrun = true;
  std::string r = cli(f, "filter stats air");
  EXPECT_NE(std::string::npos, r.find("would save"));
}

// ---- other checks keep working ------------------------------------------------------------------

TEST_F(PacketFilterTest, MinimalHashSizeAndPathBlock) {
  PacketFilter f;
  f.p.enabled = true;
  f.p.min_hash = 2;
  mesh::Packet one = floodAck(1, 0x55);
  EXPECT_FALSE(allow(f, one));
  EXPECT_EQ(1u, f.c.cnt.hash);
  f.p.min_hash = 1;
  EXPECT_TRUE(allow(f, one));
  cli(f, "filter path add 55");
  EXPECT_FALSE(allow(f, one));
  EXPECT_EQ(1u, f.c.cnt.path);
  mesh::Packet other = floodAck(1, 0x56);
  EXPECT_TRUE(allow(f, other));
}

TEST_F(PacketFilterTest, AdvertWindowDropsTheRepeat) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter advert 1");
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A), 2000));
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000 + 3600UL * 1000 + 1));
  EXPECT_EQ(1u, f.c.cnt.advert);
}

TEST_F(PacketFilterTest, BlockedChannelByKeyNotByHashAlone) {
  PacketFilter f;
  f.setDecryptFn(fakeDecrypt);
  f.p.enabled = true;
  EXPECT_NE(std::string::npos, cli(f, "filter channel add #bot").find("added"));
  FilterChannel ch;
  ASSERT_TRUE(FilterChan::derive("#bot", &ch));
  EXPECT_FALSE(allow(f, grpText(ch, "Ann: hello")));
  EXPECT_EQ(1u, f.c.cnt.channel);

  // another channel with the same 1-byte hash is not dropped
  FilterChannel other = collidingChannel(ch);
  EXPECT_TRUE(allow(f, grpText(other, "Ann: hello")));
  EXPECT_EQ(1u, f.c.cnt.channel);
  // neither is a payload of the same hash that decrypts with nothing we hold
  mesh::Packet junk = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
  junk.payload[0] = ch.hash;
  junk.payload_len = 40;
  EXPECT_TRUE(allow(f, junk));
  junk.payload[0] = ch.hash ^ 0xFF;
  EXPECT_TRUE(allow(f, junk));
  mesh::Packet tiny = junk;
  tiny.payload_len = PATH_HASH_SIZE + CIPHER_MAC_SIZE;   // cannot hold a MAC and a message
  EXPECT_FALSE(allow(f, tiny));
  EXPECT_EQ("#bot (" + std::string(1, "0123456789abcdef"[ch.hash >> 4]) + "0123456789abcdef"[ch.hash & 15] + ")", cli(f, "filter channel list"));
}

TEST_F(PacketFilterTest, BlockedPublicChannelNeedsThePublicKey) {
  PacketFilter f;
  f.setDecryptFn(fakeDecrypt);
  f.p.enabled = true;
  cli(f, "filter channel add public");   // any case
  EXPECT_EQ("Public (11)", cli(f, "filter channel list"));
  FilterChannel pub;
  ASSERT_TRUE(FilterChan::derive("Public", &pub));
  EXPECT_FALSE(allow(f, grpText(pub, "Ann: hi")));
  FilterChannel other;
  ASSERT_TRUE(FilterChan::derive("#other", &other));
  other.hash = FILTER_PUBLIC_HASH;   // a channel that shares Public's 1-byte hash
  EXPECT_TRUE(allow(f, grpText(other, "Ann: hi")));
  EXPECT_NE(std::string::npos, cli(f, "filter channel remove PUBLIC").find("removed"));
  EXPECT_TRUE(allow(f, grpText(pub, "Ann: hi")));
}

TEST_F(PacketFilterTest, WatchListTriesEveryChannelWithTheSameHash) {
  PacketFilter f;
  f.setDecryptFn(fakeDecrypt);
  f.p.enabled = true;
  FilterChannel a, b;
  ASSERT_TRUE(FilterChan::derive("#aaa", &a));
  b = collidingChannel(a);
  cli(f, "filter watch add #aaa");
  cli(f, (std::string("filter watch add ") + b.name).c_str());
  cli(f, "filter sender add Bob");
  EXPECT_FALSE(allow(f, grpText(a, "Bob: hi")));
  EXPECT_FALSE(allow(f, grpText(b, "Bob: hi"))) << "the second watched channel with that hash is tried too";
  EXPECT_TRUE(allow(f, grpText(b, "Eve: hi")));
  EXPECT_EQ(2u, f.c.cnt.sender);
}

TEST_F(PacketFilterTest, PublicIsAcceptedInAnyCase) {
  FilterChannel ch;
  ASSERT_TRUE(FilterChan::derive("public", &ch));
  EXPECT_STREQ("Public", ch.name);
  EXPECT_EQ(FILTER_PUBLIC_HASH, ch.hash);
  EXPECT_EQ(0, memcmp(ch.secret, FILTER_PUBLIC_SECRET, 16));
  PacketFilter f;
  EXPECT_EQ("Failed", cli(f, "filter watch add pUBLIC")) << "Public is always watched";
}

TEST_F(PacketFilterTest, ListsPageWithoutOverflowing) {
  PacketFilter f;
  for (int i = 0; i < 8; i++) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "filter text add ^abcdefghijklmnopqrstu%d 65535 50", i);
    cli(f, cmd);
    snprintf(cmd, sizeof(cmd), "filter sender add abcdefghijklmn%d 65535 50", i);
    cli(f, cmd);
  }
  for (int i = 0; i < 16; i++) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "filter channel add #abcdefghijklmnopqrs%d", i % 10 + 10 * (i / 10));
    cli(f, cmd);
  }
  for (const char* what : { "text", "sender", "channel" }) {
    int shown = 0, total = 0;
    for (int page = 0; page < 10; page++) {
      std::string cmd = std::string("filter ") + what + " list " + std::to_string(page);
      std::string r = cli(f, cmd.c_str());
      EXPECT_EQ(std::string::npos, r.find("..")) << what << " " << r;
      size_t br = r.rfind(" [");
      ASSERT_NE(std::string::npos, br) << what << " " << r;
      int end = 0, tot = 0, pg = 0;
      ASSERT_EQ(3, sscanf(r.c_str() + br, " [%d/%d p%d]", &end, &tot, &pg)) << r;
      EXPECT_EQ(page, pg);
      EXPECT_EQ(std::string::npos, r.substr(br).find(','));
      total = tot;
      shown = end;
      if (end >= tot) break;
    }
    EXPECT_EQ(total, shown) << what;
  }
  EXPECT_NE(std::string::npos, cli(f, "filter text list 9").find("no page"));
  // a short list has no suffix
  PacketFilter g;
  cli(g, "filter sender add Bob");
  EXPECT_EQ("Bob block", cli(g, "filter sender list"));
  cli(g, "filter path add 7a");
  EXPECT_EQ("7A", cli(g, "filter path list 0"));
}

// ---- ingress ------------------------------------------------------------------------------------

TEST_F(PacketFilterTest, IngressLimitsPerFirstPathEntry) {
  PacketFilter f;
  f.p.enabled = true;
  EXPECT_NE(std::string::npos, cli(f, "filter ingress 3 60").find("OK"));
  mesh::Packet a = floodAck(2, 0xA1);   // first entry A1
  mesh::Packet b = floodAck(2, 0xB2);
  for (int i = 0; i < 3; i++) EXPECT_TRUE(allow(f, a, 1000 + i)) << i;
  EXPECT_FALSE(allow(f, a, 1010)) << "the fourth from the same ingress";
  EXPECT_TRUE(allow(f, b, 1011)) << "another ingress has its own budget";
  EXPECT_EQ(1u, f.c.cnt.ingress);
  // the same first entry behind different later hops is the same ingress
  mesh::Packet a2 = floodAck(3, 0xA1);
  EXPECT_FALSE(allow(f, a2, 1020));
  // the window expires
  EXPECT_TRUE(allow(f, a, 1000 + 61000));
}

TEST_F(PacketFilterTest, IngressDirectPacketsShareOneKey) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter ingress 2 60");
  mesh::Packet heard = floodAck(0, 0);
  EXPECT_TRUE(allow(f, heard, 1000));
  EXPECT_TRUE(allow(f, heard, 1001));
  EXPECT_FALSE(allow(f, heard, 1002));
  std::string st = cli(f, "filter stats ingress");
  EXPECT_NE(std::string::npos, st.find("direct:1")) << st;
}

TEST_F(PacketFilterTest, IngressDoesNotTouchRoutedPacketsAndIsOffByDefault) {
  PacketFilter f;
  f.p.enabled = true;
  mesh::Packet a = floodAck(2, 0xA1);
  for (int i = 0; i < 50; i++) EXPECT_TRUE(allow(f, a, 1000 + i));
  cli(f, "filter ingress 1 60");
  mesh::Packet d = a;
  d.header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
  for (int i = 0; i < 10; i++) EXPECT_TRUE(allow(f, d, 5000 + i));
}

TEST_F(PacketFilterTest, IngressSoftCutoffNeverPassesPastTheLimit) {
  PacketFilter f;
  f.p.enabled = true;
  EXPECT_NE(std::string::npos, cli(f, "filter ingress 20 60 5").find("OK"));
  EXPECT_NE(std::string::npos, cli(f, "filter ingress").find("soft 5"));
  mesh::Packet a = floodAck(1, 0xA1);
  int passed = 0;
  for (int i = 0; i < 100; i++) if (allow(f, a, 1000 + i)) passed++;
  EXPECT_GE(passed, 5) << "everything up to the soft level passes";
  EXPECT_LE(passed, 20) << "nothing past the hard limit";
}

TEST_F(PacketFilterTest, IngressTableReplacesTheLeastRecentlyUsed) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter ingress 1 3600");
  for (int i = 0; i < INGRESS_TABLE_SIZE + 5; i++) {
    mesh::Packet p = floodAck(1, (uint8_t)(i + 1));
    EXPECT_TRUE(allow(f, p, 1000 + i)) << "first packet of ingress " << i;
  }
  EXPECT_EQ(INGRESS_TABLE_SIZE, f.ingress.used());
  mesh::Packet recent = floodAck(1, (uint8_t)(INGRESS_TABLE_SIZE + 5));
  EXPECT_FALSE(allow(f, recent, 2000)) << "a recent one is remembered";
}

TEST_F(PacketFilterTest, IngressCliValidation) {
  PacketFilter f;
  int saves;
  EXPECT_EQ("> Ingress limit off", cli(f, "filter ingress"));
  EXPECT_NE(std::string::npos, cli(f, "filter ingress 0x").find("error"));
  EXPECT_NE(std::string::npos, cli(f, "filter ingress 10 0").find("error"));
  EXPECT_NE(std::string::npos, cli(f, "filter ingress 10 60 10").find("error")) << "soft must be below the limit";
  cli(f, "filter ingress 30 120 20", &saves);
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ(30, f.p.ing_limit);
  EXPECT_EQ(120, f.p.ing_secs);
  EXPECT_EQ(20, f.p.ing_soft);
  cli(f, "filter ingress 30 soft");
  EXPECT_EQ(20, f.p.ing_soft);
  cli(f, "filter ingress off");
  EXPECT_EQ(0, f.p.ing_limit);
  EXPECT_FALSE(f.ingress.active());
}

// ---- new key quota ------------------------------------------------------------------------------

TEST_F(PacketFilterTest, NewKeyAdvertsGetOneForwardPerWindow) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 24 60");
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000, NOW_SECS));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A), 2000, NOW_SECS + 10));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A), 3000, NOW_SECS + 3000));
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 4000, NOW_SECS + 3601));
  EXPECT_TRUE(allow(f, advertFrom(KEY_B), 5000, NOW_SECS + 3602)) << "another key has its own quota";
  EXPECT_EQ(2u, f.c.cnt.newkey_advert);
}

TEST_F(PacketFilterTest, NewKeyBecomesOldAfterTheConfiguredHours) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 2 60");
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000, NOW_SECS));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A), 1100, NOW_SECS + 60));
  for (int i = 0; i < 5; i++) EXPECT_TRUE(allow(f, advertFrom(KEY_A), 2000 + i, NOW_SECS + 3 * 3600 + i)) << "three hours later it is old";
}

TEST_F(PacketFilterTest, NewKeyAnonRequestsOnePerMinute) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 24");
  EXPECT_TRUE(allow(f, anonFrom(KEY_A), 1000, NOW_SECS));
  EXPECT_FALSE(allow(f, anonFrom(KEY_A), 1001, NOW_SECS + 30));
  EXPECT_TRUE(allow(f, anonFrom(KEY_A), 1002, NOW_SECS + 61));
  EXPECT_EQ(1u, f.c.cnt.newkey_anon);
}

static bool knowsKeyA(void*, const uint8_t* k4) { return memcmp(k4, KEY_A, 4) == 0; }

TEST_F(PacketFilterTest, KeysTheNodeKnowsAreNeverNew) {
  PacketFilter f;
  f.p.enabled = true;
  f.setKnownFn(knowsKeyA, nullptr);
  cli(f, "filter newkey 24");
  for (int i = 0; i < 5; i++) EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000 + i, NOW_SECS + i));
  EXPECT_EQ(0, f.newkey.count()) << "a known key takes no table row";
  EXPECT_TRUE(allow(f, advertFrom(KEY_B), 1000, NOW_SECS));
  EXPECT_FALSE(allow(f, advertFrom(KEY_B), 1001, NOW_SECS + 1));
}

TEST_F(PacketFilterTest, NewKeyIsInactiveWithoutAClock) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 24");
  for (int i = 0; i < 5; i++) EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000 + i, 1715000000 + i));   // 2024
  EXPECT_EQ(0, f.newkey.count()) << "nothing recorded against a clock that will jump";
}

TEST_F(PacketFilterTest, NewKeyDoesNotLimitOtherPacketsOrDirect) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter newkey 24");
  mesh::Packet d = advertFrom(KEY_A);
  d.header = ROUTE_TYPE_DIRECT | (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  for (int i = 0; i < 5; i++) EXPECT_TRUE(allow(f, d));
  mesh::Packet ack = floodAck(1, 9);
  for (int i = 0; i < 5; i++) EXPECT_TRUE(allow(f, ack));
}

TEST_F(PacketFilterTest, NewKeyTableEvictsTheYoungestWhenFull) {
  NewKeyTracker t;
  t.configure(24, 60);
  uint8_t key[4];
  for (int i = 0; i < NEWKEY_CAP; i++) {
    key[0] = i & 255; key[1] = i >> 8; key[2] = 9; key[3] = 9;
    t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + (uint32_t)i * 3600);
  }
  EXPECT_EQ(NEWKEY_CAP, t.count());
  key[0] = 0xEE; key[1] = 0xEE;
  t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + 100000);
  EXPECT_EQ(NEWKEY_CAP, t.count());
  uint8_t first[4] = { 0, 0, 9, 9 };
  EXPECT_GE(t.ageHours(first, NOW_SECS + 100000), 0) << "the longest known key survives a flood";
  uint8_t youngest[4] = { (uint8_t)((NEWKEY_CAP - 1) & 255), (uint8_t)((NEWKEY_CAP - 1) >> 8), 9, 9 };
  EXPECT_EQ(-1, t.ageHours(youngest, NOW_SECS + 100000)) << "the youngest row made room";
  EXPECT_GE(t.ageHours(key, NOW_SECS + 100000), 0);
  // a flood of random keys never pushes out the first one
  for (int i = 0; i < 1000; i++) {
    uint8_t r[4] = { (uint8_t)i, (uint8_t)(i >> 8), 0xCC, 0xDD };
    t.allow(r, NewKeyTracker::ADVERT, false, NOW_SECS + 100000 + i);
  }
  EXPECT_GE(t.ageHours(first, NOW_SECS + 200000), 0);
}

TEST_F(PacketFilterTest, NewKeySavesAtMostHourlyAndOnlyWhenChanged) {
  NewKeyTracker t;
  t.configure(24, 60);
  t.setSaveClock(0);
  EXPECT_FALSE(t.saveDue(10 * 3600000UL)) << "nothing changed";
  uint8_t key[4] = { 1, 2, 3, 4 };
  t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS);
  EXPECT_TRUE(t.dirty());
  EXPECT_FALSE(t.saveDue(1000)) << "less than an hour since the last write";
  EXPECT_FALSE(t.saveDue(NEWKEY_SAVE_MS - 1));
  EXPECT_TRUE(t.saveDue(NEWKEY_SAVE_MS));
  t.markSaved(NEWKEY_SAVE_MS);
  EXPECT_FALSE(t.saveDue(NEWKEY_SAVE_MS * 5)) << "unchanged since the write";
  t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + 5);   // the same key again: no change
  EXPECT_FALSE(t.dirty());
  key[0] = 9;
  t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + 6);
  EXPECT_TRUE(t.dirty());
  EXPECT_FALSE(t.saveDue(NEWKEY_SAVE_MS + 1000));
  EXPECT_TRUE(t.saveDue(2 * NEWKEY_SAVE_MS));
}

TEST_F(PacketFilterTest, NewKeyTableSerializesAndLoads) {
  NewKeyTracker t;
  t.configure(24, 60);
  uint8_t key[4];
  for (int i = 0; i < 10; i++) {
    key[0] = i; key[1] = key[2] = key[3] = 7;
    t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + i * 3600);
  }
  std::vector<uint8_t> img(t.serializedSize());
  EXPECT_EQ(img.size(), t.serialize(img.data()));
  NewKeyTracker u;
  EXPECT_TRUE(u.load(img.data(), img.size()));
  EXPECT_EQ(10, u.count());
  EXPECT_FALSE(u.dirty());
  key[0] = 3;
  EXPECT_EQ(0, u.ageHours(key, NOW_SECS + 3 * 3600));
  EXPECT_EQ(2, u.ageHours(key, NOW_SECS + 5 * 3600));
  // damaged images are refused and leave an empty table
  img[0] ^= 0xFF;
  EXPECT_FALSE(u.load(img.data(), img.size()));
  EXPECT_EQ(0, u.count());
  img[0] ^= 0xFF;
  EXPECT_FALSE(u.load(img.data(), img.size() - 1)) << "truncated";
  EXPECT_FALSE(u.load(nullptr, 0));
}

TEST_F(PacketFilterTest, NewKeyCliAndClear) {
  PacketFilter f;
  int saves;
  EXPECT_NE(std::string::npos, cli(f, "filter newkey").find("off"));
  EXPECT_NE(std::string::npos, cli(f, "filter newkey 0").find("off"));
  EXPECT_NE(std::string::npos, cli(f, "filter newkey 9999").find("error"));
  EXPECT_NE(std::string::npos, cli(f, "filter newkey 24 0").find("error"));
  cli(f, "filter newkey 24 90", &saves);
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_EQ(24, f.p.nk_hours);
  EXPECT_EQ(90, f.p.nk_mins);
  EXPECT_NE(std::string::npos, cli(f, "filter newkey").find("24h"));
  uint8_t key[4] = { 1, 2, 3, 4 };
  f.newkey.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS);
  cli(f, "filter newkey clear", &saves);
  EXPECT_EQ(PacketFilter::SAVE_SEEN, saves);
  EXPECT_EQ(0, f.newkey.count());
  cli(f, "filter newkey off", &saves);
  EXPECT_EQ(0, f.p.nk_hours);
}

// ---- the CLI in general -------------------------------------------------------------------------

TEST_F(PacketFilterTest, CliBasics) {
  PacketFilter f;
  int saves;
  EXPECT_NE(std::string::npos, cli(f, "filter").find("Filter off"));
  EXPECT_NE(std::string::npos, cli(f, "filter help").find("block"));
  EXPECT_EQ("> Filter: on", cli(f, "filter on", &saves));
  EXPECT_EQ(PacketFilter::SAVE_PREFS, saves);
  EXPECT_TRUE(f.enabled());
  EXPECT_EQ("> Filter: dry-run off", cli(f, "filter dryrun"));
  cli(f, "filter dryrun on");
  EXPECT_TRUE(f.p.dryrun);
  EXPECT_NE(std::string::npos, cli(f, "filter").find("(dry-run)"));
  EXPECT_EQ("> Filter: command error", cli(f, "filter bogus"));
  EXPECT_NE(std::string::npos, cli(f, "filter hash 9").find("error"));
  cli(f, "filter hash 2");
  EXPECT_EQ(2, f.p.min_hash);
  EXPECT_NE(std::string::npos, cli(f, "filter advert 9999").find("error"));
  EXPECT_NE(std::string::npos, cli(f, "filter age 5").find("OK"));
  EXPECT_EQ(5, f.c.age_mins);
  cli(f, "filter malformed on");
  EXPECT_TRUE(f.c.malformed);
  cli(f, "filter sender add Bot* 30 50");
  cli(f, "filter text add ^BEACON");
  EXPECT_NE(std::string::npos, cli(f, "filter sender list").find("Bot* throttle 30s 50%"));
  EXPECT_EQ("^BEACON block", cli(f, "filter text list"));
  EXPECT_NE(std::string::npos, cli(f, "filter reset").find("reset"));
  EXPECT_FALSE(f.enabled());
  EXPECT_EQ(2 - 1, f.p.min_hash);
  EXPECT_EQ("None", cli(f, "filter sender list"));
}

TEST_F(PacketFilterTest, EveryStatsTopicAnswersWithinTheReplyBuffer) {
  PacketFilter f;
  f.p.enabled = true;
  const char* topics[] = { "hash", "path", "advert", "channel", "malformed", "age", "sender", "text", "block", "ingress", "newkey", "air", "top", "bogus", "" };
  for (const char* t : topics) {
    std::string r = cli(f, (std::string("filter stats ") + t).c_str());
    EXPECT_FALSE(r.empty()) << t;
  }
  EXPECT_NE(std::string::npos, cli(f, "filter stats block").find("empty"));
  f.c.addBlockKey(KEY_A);
  f.p.ing_limit = 5;
  f.p.nk_hours = 12;
  f.newkey.configure(12, 60);
  f.ingress.configure(5, 60, 0);
  allow(f, advertFrom(KEY_A));
  EXPECT_NE(std::string::npos, cli(f, "filter stats block").find("dropped 1"));
  EXPECT_NE(std::string::npos, cli(f, "filter stats newkey").find("12h/60m"));
  EXPECT_NE(std::string::npos, cli(f, "filter stats reset").find("reset"));
  EXPECT_EQ(0u, f.c.cnt.block);
}

TEST_F(PacketFilterTest, SaturatedCountersStayInsideTheReplyBuffer) {
  PacketFilter f;
  f.p.enabled = true;
  Counters& c = f.c.cnt;
  c.hash = c.path = c.advert = c.channel = c.malformed = c.age = c.sender = c.text = c.block = c.ingress = 0xFFFFFFFFu;
  c.newkey_advert = c.newkey_anon = 0xFFFFFFFFu;
  c.block_local = 0xFFFFFFFFu;
  EXPECT_NE(std::string::npos, cli(f, "filter").find("Filter on"));
  for (int i = 0; i < 32; i++) f.c.block[i].len = 4, f.c.block[i].key[0] = i + 1, c.block_slot[i] = 0xFFFFFFFFu;
  cli(f, "filter stats block");
  cli(f, "filter stats block 1");
}

// ---- the saved format ---------------------------------------------------------------------------

TEST_F(PacketFilterTest, PrefsSurviveASaveAndLoad) {
  PacketFilter f;
  f.p.enabled = true;
  f.p.dryrun = true;
  cli(f, "filter hash 2");
  cli(f, "filter advert 6");
  cli(f, "filter age 90");
  cli(f, "filter malformed on");
  cli(f, "filter path add A1B2");
  cli(f, "filter channel add #bot");
  cli(f, "filter watch add #ops");
  cli(f, "filter sender add Spam* 10 50");
  cli(f, "filter text add buy");
  cli(f, "filter ingress 25 30 10");
  cli(f, "filter newkey 12 45");
  f.c.addBlockKey(KEY_A);
  cli(f, "filter block add DEADBEEF");

  MemSink out;
  f.writePrefs(out);
  PacketFilter g;
  MemSrc in(out.v);
  ASSERT_TRUE(g.readPrefs(in));
  MemSink out2;
  g.writePrefs(out2);
  ASSERT_EQ(out.v.size(), out2.v.size());
  for (size_t i = 0; i < out.v.size(); i++) ASSERT_EQ(out.v[i], out2.v[i]) << "byte " << i;
  EXPECT_EQ(f.prefsSignature(), g.prefsSignature());
  EXPECT_TRUE(g.p.enabled);
  EXPECT_TRUE(g.p.dryrun);
  EXPECT_EQ(2, g.p.min_hash);
  EXPECT_EQ(6, g.p.advert_hours);
  EXPECT_EQ(6, g.advert.getWindowHours());
  EXPECT_EQ(90, g.c.age_mins);
  EXPECT_TRUE(g.c.malformed);
  EXPECT_EQ(25, g.p.ing_limit);
  EXPECT_EQ(25, g.ingress.limit());
  EXPECT_EQ(12, g.newkey.hours());
  EXPECT_EQ(45, g.newkey.mins());
  EXPECT_EQ(2, FilterBlock::count(g.c.block, FILTER_BLOCK_COUNT));
  EXPECT_EQ(0, g.c.blockSlot(KEY_A));
  EXPECT_STREQ("#bot", g.p.blocked_ch[0].name);
  FilterChannel ch;
  FilterChan::derive("#bot", &ch);
  EXPECT_EQ(ch.hash, g.p.blocked_ch[0].hash) << "the key is derived again, never read from the file";
  EXPECT_STREQ("#ops", g.p.watch_ch[0].name);
  EXPECT_EQ("Spam* throttle 10s 50%", cli(g, "filter sender list"));
}

TEST_F(PacketFilterTest, WritesOnlyWhenSomethingChanged) {
  PacketFilter f;
  f.markPrefsSaved();
  EXPECT_FALSE(f.prefsChanged());
  cli(f, "filter on");
  EXPECT_TRUE(f.prefsChanged());
  f.markPrefsSaved();
  cli(f, "filter on");
  EXPECT_FALSE(f.prefsChanged()) << "setting what is already set changes nothing to write";
  f.c.cnt.block = 99;
  EXPECT_FALSE(f.prefsChanged()) << "counters are not saved";
  uint8_t key[4] = { 1, 2, 3, 4 };
  f.newkey.configure(12, 60);
  f.newkey.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS);
  EXPECT_FALSE(f.prefsChanged()) << "the seen table has its own file";
}

TEST_F(PacketFilterTest, DamagedOrUnknownFilesLeaveDefaults) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter block add DEADBEEF");
  MemSink out;
  f.writePrefs(out);

  PacketFilter g;
  g.p.min_hash = 3;
  std::vector<uint8_t> bad = out.v;
  bad[0] = 'X';
  MemSrc a(bad);
  EXPECT_FALSE(g.readPrefs(a));
  EXPECT_FALSE(g.p.enabled);
  EXPECT_EQ(1, g.p.min_hash);

  bad = out.v;
  bad[3] = FILTER_PREFS_VER + 1;   // written by a newer firmware
  MemSrc b(bad);
  EXPECT_FALSE(g.readPrefs(b));

  for (size_t cut : { (size_t)0, (size_t)3, (size_t)10, out.v.size() / 2, out.v.size() - 1 }) {
    std::vector<uint8_t> t(out.v.begin(), out.v.begin() + cut);
    MemSrc c(t);
    EXPECT_FALSE(g.readPrefs(c)) << cut;
    EXPECT_FALSE(g.p.enabled) << cut;
    EXPECT_EQ(0, FilterBlock::count(g.c.block, FILTER_BLOCK_COUNT)) << cut;
  }

  // a block entry count that does not fit
  bad = out.v;
  size_t at = 4 + 4 + 14 + 8 * 5 + 8 * (FILTER_SENDER_LEN + 3) + 8 * (FILTER_TEXT_LEN + 3);
  bad[at] = 200;
  MemSrc d(bad);
  EXPECT_FALSE(g.readPrefs(d));
}

TEST_F(PacketFilterTest, LoadedValuesAreClamped) {
  PacketFilter f;
  f.p.min_hash = 9;
  f.p.advert_hours = 5000;
  f.p.ing_secs = 0;
  f.p.ing_limit = 5;
  f.p.ing_soft = 7;
  f.p.nk_hours = 5000;
  f.p.nk_mins = 0;
  f.c.age_mins = 60000;
  f.sanitize();
  EXPECT_EQ(1, f.p.min_hash);
  EXPECT_EQ(ADVERT_MAX_HOURS, f.p.advert_hours);
  EXPECT_EQ(60, f.p.ing_secs);
  EXPECT_EQ(0, f.p.ing_soft);
  EXPECT_EQ(NEWKEY_MAX_HOURS, f.p.nk_hours);
  EXPECT_EQ(60, f.p.nk_mins);
  EXPECT_EQ(FILTER_AGE_MAX_MINS, f.c.age_mins);
}

// ---- the block list on its own ------------------------------------------------------------------

TEST(BlockList, ParseFormatAndMatch) {
  BlockEntry e;
  EXPECT_TRUE(FilterBlock::parse("deadbeef", &e));
  EXPECT_EQ(4, e.len);
  char hex[65];
  FilterBlock::format(hex, sizeof(hex), e);
  EXPECT_STREQ("DEADBEEF", hex);
  EXPECT_FALSE(FilterBlock::parse("dead", &e)) << "shorter than 4 bytes";
  EXPECT_FALSE(FilterBlock::parse("deadbee", &e));
  EXPECT_FALSE(FilterBlock::parse(nullptr, &e));
  EXPECT_FALSE(FilterBlock::parse("", &e));

  BlockEntry list[FILTER_BLOCK_COUNT] = {};
  EXPECT_TRUE(FilterBlock::parse("deadbeef", &e));
  EXPECT_TRUE(FilterBlock::addRaw(list, FILTER_BLOCK_COUNT, e.key, e.len));
  uint8_t key[32] = { 0xDE, 0xAD, 0xBE, 0xEF, 1, 2 };
  EXPECT_EQ(0, FilterBlock::findMatch(list, FILTER_BLOCK_COUNT, key, 32));
  EXPECT_EQ(-1, FilterBlock::findMatch(list, FILTER_BLOCK_COUNT, key, 3)) << "an entry longer than the known key part";
  key[3] = 0;
  EXPECT_EQ(-1, FilterBlock::findMatch(list, FILTER_BLOCK_COUNT, key, 32));
  EXPECT_EQ(-1, FilterBlock::findMatch(list, FILTER_BLOCK_COUNT, nullptr, 32));
}

TEST(BlockList, RemoveReusesTheSlot) {
  BlockEntry list[FILTER_BLOCK_COUNT] = {};
  uint8_t a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 };
  int s;
  EXPECT_TRUE(FilterBlock::addRaw(list, FILTER_BLOCK_COUNT, a, 4, &s));
  EXPECT_EQ(0, s);
  EXPECT_TRUE(FilterBlock::addRaw(list, FILTER_BLOCK_COUNT, b, 4, &s));
  EXPECT_EQ(1, s);
  EXPECT_FALSE(FilterBlock::addRaw(list, FILTER_BLOCK_COUNT, a, 4, &s));
  EXPECT_EQ(0, s);
  EXPECT_EQ(0, FilterBlock::removeRaw(list, FILTER_BLOCK_COUNT, a, 4));
  EXPECT_EQ(-1, FilterBlock::removeRaw(list, FILTER_BLOCK_COUNT, a, 4));
  EXPECT_TRUE(FilterBlock::addRaw(list, FILTER_BLOCK_COUNT, a, 4, &s));
  EXPECT_EQ(0, s);
}

TEST(IngressLimiter, KeysAndTop) {
  uint8_t path[3] = { 0xA1, 0xB2, 0xC3 };
  uint8_t key[INGRESS_KEY_MAX], len;
  IngressLimiter::keyOf(path, 1, 3, key, &len);
  EXPECT_EQ(1, len);
  EXPECT_EQ(0xA1, key[0]);
  IngressLimiter::keyOf(path, 2, 1, key, &len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0xB2, key[1]);
  IngressLimiter::keyOf(path, 1, 0, key, &len);
  EXPECT_EQ(IngressLimiter::KEY_DIRECT, len);

  IngressLimiter l;
  l.configure(1, 60, 0);
  uint8_t k1[1] = { 1 }, k2[1] = { 2 };
  for (int i = 0; i < 5; i++) l.allow(k1, 1, 1000 + i, 0);
  for (int i = 0; i < 3; i++) l.allow(k2, 1, 1000 + i, 0);
  IngressLimiter::Top top[INGRESS_TOP_COUNT];
  EXPECT_EQ(2, l.top(top, INGRESS_TOP_COUNT));
  EXPECT_EQ(4, top[0].drops);
  EXPECT_EQ(2, top[1].drops);
  l.configure(0, 60, 0);
  EXPECT_TRUE(l.allow(k1, 1, 5000, 0)) << "off";
  EXPECT_EQ(0, l.used());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---- review fixes -------------------------------------------------------------------------------

TEST_F(PacketFilterTest, IngressQuotaIsNotUsedUpByAPacketAContentRuleDrops) {
  PacketFilter f;
  f.setDecryptFn(fakeDecrypt);
  f.p.enabled = true;
  cli(f, "filter ingress 2 60");
  cli(f, "filter sender add Bob");
  FilterChannel pub;
  ASSERT_TRUE(FilterChan::derive("Public", &pub));
  mesh::Packet bad = grpText(pub, "Bob: spam");
  bad.path[0] = 0x55;
  bad.setPathHashSizeAndCount(1, 1);
  mesh::Packet good = grpText(pub, "Ann: hi");
  good.path[0] = 0x55;
  good.setPathHashSizeAndCount(1, 1);
  for (int i = 0; i < 10; i++) EXPECT_FALSE(allow(f, bad));
  EXPECT_TRUE(allow(f, good));
  EXPECT_TRUE(allow(f, good));
  EXPECT_FALSE(allow(f, good)) << "the third accepted packet is over the limit of two";
  EXPECT_EQ(1u, f.c.cnt.ingress);
}

TEST_F(PacketFilterTest, AdvertClockSeesAWrapThroughTickAlone) {
  PacketFilter f;
  f.p.enabled = true;
  cli(f, "filter advert 1");   // one advert per origin and hour
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 1000));
  EXPECT_FALSE(allow(f, advertFrom(KEY_A), 2000));
  // no packet for longer than the millis wrap, but the main loop ticks
  for (uint64_t t = 1000; t < 2ULL * 4294967296ULL; t += 1000000000ULL) f.tick((uint32_t)t);
  EXPECT_TRUE(allow(f, advertFrom(KEY_A), 2000)) << "the window has long elapsed";
}

TEST_F(PacketFilterTest, NewKeyClassIsLimitedAsAWhole) {
  NewKeyTracker t;
  t.configure(24, 60);
  int passed = 0;
  for (int i = 0; i < 200; i++) {
    uint8_t key[4] = { (uint8_t)i, (uint8_t)(i >> 8), 0x31, 0x32 };   // 200 one-time keys, each new
    if (t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + i / 20)) passed++;   // ~20 a second
  }
  EXPECT_LE(passed, NEWKEY_CLASS_LIMIT);
  EXPECT_GT(passed, 0);
  // the next minute has a fresh quota
  uint8_t k[4] = { 0xFE, 0xFE, 1, 1 };
  EXPECT_TRUE(t.allow(k, NewKeyTracker::ADVERT, false, NOW_SECS + 120));
}

TEST_F(PacketFilterTest, NewKeyLookDoesNotChangeTheTable) {
  NewKeyTracker t;
  t.configure(24, 60);
  uint8_t k[4] = { 1, 2, 3, 4 };
  EXPECT_TRUE(t.allow(k, NewKeyTracker::ADVERT, false, NOW_SECS, false));
  EXPECT_EQ(0, t.count());
  EXPECT_FALSE(t.dirty());
  EXPECT_TRUE(t.allow(k, NewKeyTracker::ADVERT, false, NOW_SECS, true));
  EXPECT_EQ(1, t.count());
  EXPECT_TRUE(t.dirty());
  t.markSaved(1000);
  EXPECT_FALSE(t.allow(k, NewKeyTracker::ADVERT, false, NOW_SECS + 5, false)) << "inside its quota";
  EXPECT_FALSE(t.dirty());
}

TEST_F(PacketFilterTest, NewKeysCannotEvictEachOtherInAFullTable) {
  NewKeyTracker t;
  t.configure(24, 60);
  uint8_t key[4];
  for (int i = 0; i < NEWKEY_CAP; i++) {
    key[0] = i & 255; key[1] = i >> 8; key[2] = 9; key[3] = 9;
    t.allow(key, NewKeyTracker::ADVERT, false, NOW_SECS + (uint32_t)i * 3600);
  }
  uint8_t a[4] = { 0xA1, 0xA1, 0, 0 }, b[4] = { 0xB2, 0xB2, 0, 0 };
  uint32_t now = NOW_SECS + 400000;
  int passed = 0;
  for (int i = 0; i < 20; i++) {   // alternating, 2 seconds apart: 40 seconds
    if (t.allow(a, NewKeyTracker::ADVERT, false, now + i * 2)) passed++;
    if (t.allow(b, NewKeyTracker::ADVERT, false, now + i * 2 + 1)) passed++;
  }
  EXPECT_EQ(1, passed) << "one admission, then a minute of nothing";
}
