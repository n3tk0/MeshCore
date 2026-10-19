#include <gtest/gtest.h>

#include <helpers/MailboxStore.h>

static const uint8_t OWNER = 0xA7, SENDER = 0x3C;

static MailboxStore* makeStore() {
  static MailboxStore m;
  m.reset();
  uint8_t key[MAILBOX_KEY_SIZE];
  memset(key, 0x11, sizeof(key));
  key[0] = OWNER;
  m.addOwner(key);
  m.config().enabled = 1;
  return &m;
}

// a TXT_MSG payload: dest_hash, src_hash, MAC + data ('fill' makes it unique)
static int msg(uint8_t* p, uint8_t dest, uint8_t src, int len, uint8_t fill) {
  p[0] = dest; p[1] = src;
  for (int i = 2; i < len; i++) p[i] = fill;
  return len;
}

static void hashOf(uint8_t* h, uint8_t fill) { memset(h, fill, MAILBOX_HASH_SIZE); }

TEST(MailboxStore, KeepsMessagesForAnAwayOwnerOnly) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  hashOf(h, 1);
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000));
  hashOf(h, 2);
  EXPECT_FALSE(m.onPacket(0x02, p, msg(p, 0x55, SENDER, 20, 2), h, 1000));   // someone else
  hashOf(h, 3);
  EXPECT_FALSE(m.onPacket(0x05, p, msg(p, OWNER, SENDER, 20, 3), h, 1000));  // channel message
  EXPECT_EQ(1, m.count());
}

TEST(MailboxStore, NothingKeptWhileOwnerIsHere) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  m.markHeard(0, 1000);
  hashOf(h, 1);
  EXPECT_FALSE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000 + 9 * 60));
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000 + 11 * 60));   // away again
}

TEST(MailboxStore, SamePacketOnceAndRetriesReplaced) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  hashOf(h, 1);
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000));
  EXPECT_FALSE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1001));   // heard again
  hashOf(h, 2);
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 2), h, 1010));    // retry
  EXPECT_EQ(1, m.count());
  EXPECT_EQ(2, m.at(0).payload[2]);
  hashOf(h, 3);
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 3), h, 1100));    // a later message
  hashOf(h, 4);
  EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, SENDER, 36, 4), h, 1101));    // another length
  EXPECT_EQ(3, m.count());
}

TEST(MailboxStore, PathReplyMeansDelivered) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  hashOf(h, 1);
  m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000);
  uint8_t path[2] = { 0x99, OWNER };
  EXPECT_FALSE(m.onPathReply(path, 2, 1010));   // to another node
  path[0] = SENDER;
  EXPECT_FALSE(m.onPathReply(path, 2, 1000 + 200));   // too late to be this message's
  EXPECT_TRUE(m.onPathReply(path, 2, 1030));
  EXPECT_EQ(0, m.count());
}

TEST(MailboxStore, FullDropsOldestAndExpires) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  for (int i = 0; i < MAILBOX_MAX + 3; i++) {
    hashOf(h, i + 1);
    h[7] = i >> 8;
    EXPECT_TRUE(m.onPacket(0x02, p, msg(p, OWNER, (uint8_t)i, 20, i), h, 1000 + i * 60));
  }
  EXPECT_EQ(MAILBOX_MAX, m.count());
  EXPECT_EQ(3, m.at(0).payload[1]);
  m.config().ttl_hours = 1;
  EXPECT_TRUE(m.expire(1000 + 3 * 60 + 3601));
  EXPECT_EQ(MAILBOX_MAX - 1, m.count());
}

TEST(MailboxStore, DeliveryOrderAndRemoveOwner) {
  MailboxStore& m = *makeStore();
  uint8_t key[MAILBOX_KEY_SIZE];
  memset(key, 0x22, sizeof(key));
  m.addOwner(key);
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  hashOf(h, 1); m.onPacket(0x02, p, msg(p, 0x22, SENDER, 20, 1), h, 1000);
  hashOf(h, 2); m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 2), h, 1100);
  hashOf(h, 3); m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 3), h, 1200);
  int i = m.nextFor(0);
  EXPECT_EQ(2, m.at(i).payload[2]);
  m.remove(i);
  EXPECT_EQ(3, m.at(m.nextFor(0)).payload[2]);
  EXPECT_TRUE(m.removeOwner(0));
  EXPECT_EQ(1, m.count());
  EXPECT_EQ(0, m.at(0).owner);
  EXPECT_EQ(0, m.nextFor(0));
}

TEST(MailboxStore, SavesAndLoads) {
  MailboxStore& m = *makeStore();
  uint8_t p[40], h[MAILBOX_HASH_SIZE];
  hashOf(h, 1); m.onPacket(0x02, p, msg(p, OWNER, SENDER, 20, 1), h, 1000);
  hashOf(h, 2); m.onPacket(0x02, p, msg(p, OWNER, 0x44, 33, 2), h, 1100);
  m.config().ttl_hours = 5;
  static uint8_t buf[MailboxStore::maxBytes()];
  int len = m.toBytes(buf);

  static MailboxStore l;
  EXPECT_TRUE(l.fromBytes(buf, len));
  EXPECT_EQ(2, l.count());
  EXPECT_EQ(5, l.config().ttl_hours);
  EXPECT_TRUE(l.isEnabled());
  EXPECT_EQ(33, l.at(1).len);
  EXPECT_EQ(0, memcmp(l.at(1).payload, m.at(1).payload, 33));

  EXPECT_FALSE(l.fromBytes(buf, len - 5));   // cut short
  EXPECT_EQ(0, l.count());
  EXPECT_FALSE(l.isEnabled());
  buf[0] = 99;
  EXPECT_FALSE(l.fromBytes(buf, len));       // other version
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
