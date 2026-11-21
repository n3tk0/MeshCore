#include <gtest/gtest.h>
#include <vector>
#include <helpers/LoraOta.h>
#include <helpers/OtaIdent.h>
#include "fixture.h"

// The node's flash, in RAM: the running image and the slot for the new one.
class RamTarget : public OtaTarget {
public:
  std::vector<uint8_t> image, next;
  uint32_t max_new = 100000, max_in_place = 0, head_cfg = 1024, head = 0, pos = 0;
  std::vector<uint8_t> patch, committed_head;
  bool committed = false, aborted = false, fail_write = false;

  uint8_t platform() const override { return OTA_PLATFORM_NRF52; }
  uint32_t imageSize() override { return image.size(); }
  bool readImage(uint32_t off, uint8_t* dest, size_t len) override {
    if (off + len > image.size()) return false;
    memcpy(dest, &image[off], len);
    return true;
  }
  uint32_t maxNewSize() override { return max_new; }
  bool beginNew(uint32_t) override { next.clear(); aborted = false; head = 0; return true; }
  // in place: like the nRF one, the start of the new image is dropped and the delta kept
  uint32_t maxInPlaceSize() override { return max_in_place; }
  bool beginInPlace(uint32_t size, uint32_t) override {
    next.clear(); patch.clear(); aborted = false; pos = 0;
    head = head_cfg < size ? head_cfg : size;
    return true;
  }
  uint32_t headSize() override { return head; }
  bool writePatch(const uint8_t* src, size_t len) override { patch.insert(patch.end(), src, src + len); return true; }
  bool readPatch(uint32_t off, uint8_t* dest, size_t len) override {
    if (off + len > patch.size()) return false;
    memcpy(dest, &patch[off], len);
    return true;
  }
  void commitInPlace(const uint8_t* h) override { committed_head.assign(h, h + head); committed = true; }
  bool writeNew(const uint8_t* src, size_t len) override {
    if (fail_write) return false;
    for (size_t i = 0; i < len; i++, pos++) if (head == 0 || pos >= head) next.push_back(src[i]);
    return true;
  }
  bool finishNew() override { return true; }
  bool readNew(uint32_t off, uint8_t* dest, size_t len) override {
    if (off < head) return false;
    off -= head;
    if (off + len > next.size()) return false;
    memcpy(dest, &next[off], len);
    return true;
  }
  void abortNew() override { aborted = true; }
  void commit() override { committed = true; }
};

static const uint8_t ADMIN[32] = { 1, 2, 3, 4 };
static const uint8_t OTHER[32] = { 9, 9, 9, 9 };
static const int CHUNK = 16;

static void sha(const uint8_t* data, size_t len, uint8_t* out) {
  SHA256 s;
  s.update(data, len);
  s.finalize(out, 32);
}

class LoraOtaTest : public ::testing::Test {
protected:
  RamTarget target;
  LoraOta ota{&target};
  uint8_t out[200];
  unsigned long now = 1000;

  void SetUp() override {
    target.image.assign(fixture_old, fixture_old + sizeof(fixture_old));
    ota.setIdent("v1 test|Board");
    ota.setEnabled(true);
  }

  int send(const uint8_t* key, const std::vector<uint8_t>& args, bool zero_hop = true) {
    std::vector<uint8_t> req = { OTA_REQ_TYPE };
    req.insert(req.end(), args.begin(), args.end());
    while (req.size() % 16) req.push_back(0);   // like the decrypted request: zero padded to the AES block
    return ota.handleRequest(key, zero_hop, req.data(), req.size(), out, now);
  }

  static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xff); }

  std::vector<uint8_t> beginArgs(uint32_t freq_khz = 869470, const uint8_t* old_img = fixture_old, size_t old_len = sizeof(fixture_old),
                                 uint32_t patch_size = sizeof(fixture_patch), uint8_t flags = 0) {
    std::vector<uint8_t> a = { OTA_OP_BEGIN };
    put32(a, patch_size);
    put32(a, sizeof(fixture_new));
    uint8_t h[32];
    sha(fixture_new, sizeof(fixture_new), h);
    a.insert(a.end(), h, h + 32);
    sha(old_img, old_len, h);
    a.insert(a.end(), h, h + 32);
    a.push_back(CHUNK);
    put32(a, freq_khz);
    put32(a, 125000);
    a.push_back(7);
    a.push_back(5);
    a.push_back(60); a.push_back(0);
    a.push_back(flags);
    return a;
  }

  void sendData(const uint8_t* patch, size_t len, uint16_t seq) {
    std::vector<uint8_t> a = { OTA_OP_DATA, (uint8_t)(seq & 0xff), (uint8_t)(seq >> 8) };
    size_t off = (size_t)seq * CHUNK;
    size_t n = len - off < (size_t)CHUNK ? len - off : CHUNK;
    a.insert(a.end(), patch + off, patch + off + n);
    EXPECT_EQ(0, send(ADMIN, a));
  }

  int chunks(size_t len) { return (len + CHUNK - 1) / CHUNK; }
};

TEST_F(LoraOtaTest, InfoTellsWhatRuns) {
  int n = send(ADMIN, { OTA_OP_INFO });
  ASSERT_GE(n, 2 + 3 + 4 + 32 + 4);
  EXPECT_EQ(OTA_OP_INFO, out[0]);
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(OTA_PLATFORM_NRF52, out[2]);
  EXPECT_EQ(OTA_STATE_IDLE, out[3]);
  EXPECT_EQ(OTA_FLAG_ENABLED, out[4]);
  uint32_t size;
  memcpy(&size, &out[5], 4);
  EXPECT_EQ(sizeof(fixture_old), size);
  uint8_t h[32];
  sha(fixture_old, sizeof(fixture_old), h);
  EXPECT_EQ(0, memcmp(h, &out[9], 32));
  uint32_t in_place;
  memcpy(&in_place, &out[45], 4);
  EXPECT_EQ(0u, in_place);
  EXPECT_EQ(std::string("v1 test|Board"), std::string((char*)&out[49], n - 49));
}

TEST_F(LoraOtaTest, WholeUpdate) {
  ASSERT_EQ(3, send(ADMIN, beginArgs()));
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(CHUNK, out[2]);
  EXPECT_EQ(LoraOta::ACT_SIDE_RADIO, ota.takeAction());
  EXPECT_FLOAT_EQ(869.47f, ota.sideRadio().freq);
  EXPECT_FLOAT_EQ(125.0f, ota.sideRadio().bw);

  for (int s = 0; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);

  ASSERT_GT(send(ADMIN, { OTA_OP_STATUS }), 0);
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(OTA_STATE_RECEIVING, out[2]);
  uint16_t next;
  memcpy(&next, &out[3], 2);
  EXPECT_EQ(chunks(sizeof(fixture_patch)), next);

  ASSERT_EQ(2, send(ADMIN, { OTA_OP_END }));
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_COMMIT, ota.takeAction());
  ASSERT_EQ(sizeof(fixture_new), target.next.size());
  EXPECT_EQ(0, memcmp(fixture_new, target.next.data(), sizeof(fixture_new)));

  send(ADMIN, { OTA_OP_END });   // its reply was lost: same answer
  EXPECT_EQ(OTA_OK, out[1]);
  ota.commit();
  EXPECT_TRUE(target.committed);
}

TEST_F(LoraOtaTest, LostPacketIsSentAgainFromStatus) {
  send(ADMIN, beginArgs(0));
  EXPECT_EQ(LoraOta::ACT_NONE, ota.takeAction());   // freq 0: stays on the current radio settings
  sendData(fixture_patch, sizeof(fixture_patch), 0);
  sendData(fixture_patch, sizeof(fixture_patch), 2);   // 1 was lost
  send(ADMIN, { OTA_OP_STATUS });
  uint16_t next;
  memcpy(&next, &out[3], 2);
  EXPECT_EQ(1, next);
  for (int s = next; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  sendData(fixture_patch, sizeof(fixture_patch), 3);   // a duplicate is ignored
  send(ADMIN, { OTA_OP_END });
  EXPECT_EQ(OTA_OK, out[1]);
}

TEST_F(LoraOtaTest, EndBeforeAllDataIsRefused) {
  send(ADMIN, beginArgs());
  sendData(fixture_patch, sizeof(fixture_patch), 0);
  send(ADMIN, { OTA_OP_END });
  EXPECT_EQ(OTA_ERR_STATE, out[1]);
  EXPECT_FALSE(target.committed);
}

TEST_F(LoraOtaTest, OnlyWithNoHop) {
  EXPECT_EQ(2, send(ADMIN, beginArgs(), false));
  EXPECT_EQ(OTA_ERR_NOT_DIRECT, out[1]);
  EXPECT_FALSE(ota.isActive());
  send(ADMIN, beginArgs());
  std::vector<uint8_t> a = { OTA_OP_DATA, 0, 0 };
  a.insert(a.end(), fixture_patch, fixture_patch + CHUNK);
  EXPECT_EQ(0, send(ADMIN, a, false));
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(0, out[3]);   // not taken
}

TEST_F(LoraOtaTest, DeltaForOtherFirmwareIsRefused) {
  uint8_t other[100] = { 7 };
  send(ADMIN, beginArgs(869470, other, sizeof(other)));
  EXPECT_EQ(OTA_ERR_OLD_IMAGE, out[1]);
  EXPECT_FALSE(ota.isActive());
}

TEST_F(LoraOtaTest, TooBig) {
  target.max_new = 100;
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_ERR_TOO_BIG, out[1]);
}

TEST_F(LoraOtaTest, BrokenPatchNeverBoots) {
  std::vector<uint8_t> bad(fixture_patch, fixture_patch + sizeof(fixture_patch));
  for (size_t i = 20; i < bad.size(); i += 7) bad[i] ^= 0x5a;
  send(ADMIN, beginArgs());
  ota.takeAction();
  for (int s = 0; s < chunks(bad.size()); s++) sendData(bad.data(), bad.size(), s);
  send(ADMIN, { OTA_OP_END });
  EXPECT_NE(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());
  EXPECT_FALSE(target.committed);
  EXPECT_FALSE(ota.isActive());
}

TEST_F(LoraOtaTest, FlashErrorStopsTheSession) {
  send(ADMIN, beginArgs());
  ota.takeAction();
  target.fail_write = true;
  for (int s = 0; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(OTA_ERR_PATCH, out[1]);
  EXPECT_EQ(OTA_STATE_FAILED, out[2]);
  EXPECT_TRUE(target.aborted);
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());
}

TEST_F(LoraOtaTest, OneAdminAtATime) {
  send(ADMIN, beginArgs());
  send(OTHER, beginArgs());
  EXPECT_EQ(OTA_ERR_BUSY, out[1]);
  send(OTHER, { OTA_OP_ABORT });
  EXPECT_EQ(OTA_ERR_BUSY, out[1]);
  EXPECT_TRUE(ota.isActive());

  send(ADMIN, beginArgs());   // the same BEGIN again: its reply was lost
  EXPECT_EQ(OTA_OK, out[1]);
  send(ADMIN, { OTA_OP_ABORT });
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_FALSE(ota.isActive());
  EXPECT_TRUE(target.aborted);
}

TEST_F(LoraOtaTest, SilentAdminPauses) {
  send(ADMIN, beginArgs());
  ota.takeAction();
  now += 59000;
  EXPECT_FALSE(ota.checkTimeout(now));
  send(ADMIN, { OTA_OP_STATUS });   // activity keeps it alive
  now += 59000;
  EXPECT_FALSE(ota.checkTimeout(now));
  now += 2000;
  EXPECT_TRUE(ota.checkTimeout(now));
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());
  EXPECT_FALSE(ota.isActive());
  EXPECT_EQ(OTA_STATE_PAUSED, ota.getState());
  EXPECT_FALSE(target.aborted);

  now += OTA_PAUSE_KEEP_MS;   // nobody came back
  EXPECT_TRUE(ota.checkTimeout(now));
  EXPECT_EQ(OTA_STATE_FAILED, ota.getState());
  EXPECT_TRUE(target.aborted);
  EXPECT_EQ(LoraOta::ACT_NONE, ota.takeAction());   // already on the normal radio
}

TEST_F(LoraOtaTest, PausedUpdateGoesOn) {
  send(ADMIN, beginArgs());
  ota.takeAction();
  int half = chunks(sizeof(fixture_patch)) / 2;
  for (int s = 0; s < half; s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  now += 61000;
  EXPECT_TRUE(ota.checkTimeout(now));
  ota.takeAction();

  send(OTHER, beginArgs());   // the paused update is kept for its admin
  EXPECT_EQ(OTA_ERR_BUSY, out[1]);
  send(OTHER, { OTA_OP_STATUS });
  EXPECT_EQ(OTA_ERR_BUSY, out[1]);

  now += 10 * 60 * 1000UL;
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_SIDE_RADIO, ota.takeAction());
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(OTA_STATE_RECEIVING, out[2]);
  uint16_t next;
  memcpy(&next, &out[3], 2);
  EXPECT_EQ(half, next);
  for (int s = next; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  send(ADMIN, { OTA_OP_END });
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(0, memcmp(fixture_new, target.next.data(), sizeof(fixture_new)));
}

TEST_F(LoraOtaTest, AdminPausesAndKeepsItAlive) {
  send(ADMIN, beginArgs());
  ota.takeAction();
  sendData(fixture_patch, sizeof(fixture_patch), 0);
  send(ADMIN, { OTA_OP_PAUSE });
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());
  EXPECT_EQ(OTA_STATE_PAUSED, ota.getState());
  now += OTA_PAUSE_KEEP_MS - 1000;
  send(ADMIN, { OTA_OP_STATUS });   // still there: kept for another while
  now += OTA_PAUSE_KEEP_MS - 1000;
  EXPECT_FALSE(ota.checkTimeout(now));
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_OK, out[1]);
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(1, out[3]);
}

TEST_F(LoraOtaTest, OtherUpdateDropsThePausedOne) {
  send(ADMIN, beginArgs());
  sendData(fixture_patch, sizeof(fixture_patch), 0);
  now += 61000;
  ota.checkTimeout(now);
  send(ADMIN, beginArgs(869470, fixture_old, sizeof(fixture_old), sizeof(fixture_patch) + 1));
  EXPECT_EQ(OTA_OK, out[1]);
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(0, out[3]);   // from the start
}

TEST_F(LoraOtaTest, TurningOffStopsTheUpdate) {
  send(ADMIN, beginArgs());
  ota.takeAction();
  sendData(fixture_patch, sizeof(fixture_patch), 0);
  ota.setEnabled(false);
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());
  EXPECT_FALSE(ota.isActive());
  EXPECT_TRUE(target.aborted);
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(OTA_STATE_IDLE, out[2]);
}

TEST_F(LoraOtaTest, OffUntilTurnedOn) {
  ota.setEnabled(false);
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_ERR_DISABLED, out[1]);
  EXPECT_FALSE(ota.isActive());
  send(ADMIN, { OTA_OP_INFO });
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(0, out[4]);
}

TEST_F(LoraOtaTest, InPlaceOnlyWhenTheAdminAgrees) {
  target.max_new = 100;
  target.max_in_place = 100000;
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_ERR_TOO_BIG, out[1]);
  send(ADMIN, beginArgs(869470, fixture_old, sizeof(fixture_old), sizeof(fixture_patch), OTA_BEGIN_IN_PLACE));
  EXPECT_EQ(OTA_OK, out[1]);
}

TEST_F(LoraOtaTest, InPlaceMakesTheStartAgainAtCommit) {
  target.max_new = 100;
  target.max_in_place = 100000;
  send(ADMIN, beginArgs(0, fixture_old, sizeof(fixture_old), sizeof(fixture_patch), OTA_BEGIN_IN_PLACE));
  for (int s = 0; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  ASSERT_EQ(2, send(ADMIN, { OTA_OP_END }));
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_COMMIT, ota.takeAction());
  EXPECT_EQ(sizeof(fixture_new) - target.head, target.next.size());
  EXPECT_EQ(sizeof(fixture_patch), target.patch.size());
  ota.commit();
  ASSERT_TRUE(target.committed);
  std::vector<uint8_t> all = target.committed_head;
  all.insert(all.end(), target.next.begin(), target.next.end());
  ASSERT_EQ(sizeof(fixture_new), all.size());
  EXPECT_EQ(0, memcmp(fixture_new, all.data(), all.size()));
}

TEST_F(LoraOtaTest, InPlaceBrokenTailNeverBoots) {
  target.max_new = 100;
  target.max_in_place = 100000;
  send(ADMIN, beginArgs(0, fixture_old, sizeof(fixture_old), sizeof(fixture_patch), OTA_BEGIN_IN_PLACE));
  for (int s = 0; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);
  send(ADMIN, { OTA_OP_END });
  ota.takeAction();
  target.next[10] ^= 0xff;   // the kept end of the image changed in flash
  ota.commit();
  EXPECT_FALSE(target.committed);
  send(ADMIN, { OTA_OP_STATUS });
  EXPECT_EQ(OTA_STATE_FAILED, out[2]);
  EXPECT_EQ(OTA_ERR_HASH, out[1]);
}


TEST_F(LoraOtaTest, LowBatteryPausesBeforeTheCopy) {
  static bool power_ok;
  power_ok = false;
  ota.setPowerCheck([](void*) { return power_ok; }, NULL);
  send(ADMIN, beginArgs());
  ota.takeAction();
  for (int s = 0; s < chunks(sizeof(fixture_patch)); s++) sendData(fixture_patch, sizeof(fixture_patch), s);

  ASSERT_EQ(2, send(ADMIN, { OTA_OP_END }));
  EXPECT_EQ(OTA_ERR_LOW_BATTERY, out[1]);
  EXPECT_EQ(LoraOta::ACT_RESTORE_RADIO, ota.takeAction());   // back on the normal radio, no reboot
  EXPECT_EQ(OTA_STATE_PAUSED, ota.getState());
  EXPECT_FALSE(target.committed);

  power_ok = true;   // charged: the same BEGIN goes on, nothing is missing, END boots
  now += 5 * 60 * 1000UL;
  send(ADMIN, beginArgs());
  EXPECT_EQ(OTA_OK, out[1]);
  ota.takeAction();
  send(ADMIN, { OTA_OP_END });
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(LoraOta::ACT_COMMIT, ota.takeAction());
  EXPECT_EQ(0, memcmp(fixture_new, target.next.data(), sizeof(fixture_new)));
}

// The hash of the running image can be kept over restarts (the repeater keeps it in a file).
struct ShaStore {
  bool have = false;
  uint32_t size = 0;
  uint8_t sha[32];
  int loads = 0, saves = 0, drops = 0;
  static void drop(void* ctx) {
    ShaStore* s = (ShaStore*)ctx;
    s->drops++;
    s->have = false;
  }
  static bool load(void* ctx, uint32_t size, uint8_t* out) {
    ShaStore* s = (ShaStore*)ctx;
    s->loads++;
    if (!s->have || s->size != size) return false;
    memcpy(out, s->sha, 32);
    return true;
  }
  static void save(void* ctx, uint32_t size, const uint8_t* in) {
    ShaStore* s = (ShaStore*)ctx;
    s->saves++;
    s->have = true;
    s->size = size;
    memcpy(s->sha, in, 32);
  }
};

TEST_F(LoraOtaTest, ImageShaIsHashedOnceAndKept) {
  ShaStore store;
  ota.setShaStore(ShaStore::load, ShaStore::save, &store);
  uint8_t h[32], want[32];
  uint32_t size = 0;
  sha(fixture_old, sizeof(fixture_old), want);
  ASSERT_TRUE(ota.getImageSha(h, &size));
  EXPECT_EQ(sizeof(fixture_old), size);
  EXPECT_EQ(0, memcmp(want, h, 32));
  EXPECT_EQ(1, store.saves);
  // after a restart: a new LoraOta takes it from the store and does not hash (a wrong image would show in the hash)
  RamTarget other;
  other.image.assign(fixture_old, fixture_old + sizeof(fixture_old));
  other.image[0] ^= 0xff;   // it would hash differently
  LoraOta again(&other);
  again.setShaStore(ShaStore::load, ShaStore::save, &store);
  uint8_t h2[32];
  ASSERT_TRUE(again.getImageSha(h2, &size));
  EXPECT_EQ(0, memcmp(want, h2, 32));
  EXPECT_EQ(1, store.saves);
}

TEST_F(LoraOtaTest, KeptShaForAnotherSizeIsNotUsed) {
  ShaStore store;
  store.have = true;
  store.size = sizeof(fixture_old) + 1;
  memset(store.sha, 0x55, 32);
  ota.setShaStore(ShaStore::load, ShaStore::save, &store);
  uint8_t h[32], want[32];
  uint32_t size;
  sha(fixture_old, sizeof(fixture_old), want);
  ASSERT_TRUE(ota.getImageSha(h, &size));
  EXPECT_EQ(0, memcmp(want, h, 32));
  EXPECT_EQ(1, store.saves);   // hashed and kept again
}

// an incremental local build: same size and key, other image. The kept hash is of the old build.
TEST_F(LoraOtaTest, StaleKeptShaIsDroppedAndHashedAgainAtBegin) {
  ShaStore store;
  store.have = true;
  store.size = sizeof(fixture_old);
  memset(store.sha, 0x55, 32);   // not the hash of the running image
  ota.setShaStore(ShaStore::load, ShaStore::save, &store, ShaStore::drop);
  send(ADMIN, beginArgs());      // the app hashed the real image
  EXPECT_EQ(OTA_OK, out[1]);
  EXPECT_EQ(1, store.drops);
  uint8_t h[32], want[32];
  uint32_t size;
  sha(fixture_old, sizeof(fixture_old), want);
  ASSERT_TRUE(ota.getImageSha(h, &size));
  EXPECT_EQ(0, memcmp(want, h, 32));
}

TEST_F(LoraOtaTest, KeptShaIsKeptWhenTheDeltaIsForOtherFirmware) {
  uint8_t other[100] = { 7 };
  ShaStore store;
  ota.setShaStore(ShaStore::load, ShaStore::save, &store, ShaStore::drop);
  uint8_t h[32];
  uint32_t size;
  ASSERT_TRUE(ota.getImageSha(h, &size));   // hashed, kept (not "from the store")
  send(ADMIN, beginArgs(869470, other, sizeof(other)));
  EXPECT_EQ(OTA_ERR_OLD_IMAGE, out[1]);
  EXPECT_EQ(0, store.drops);
}


static std::string ident(const char* ver, const char* commit, const char* env, const char* manuf = "Seeed Xiao") {
  char buf[61];
  formatOtaIdent(buf, sizeof(buf), ver, "21-Nov-2026", commit, env, manuf);
  return buf;
}

TEST(OtaIdent, ReleaseBuildKeepsVersionAndEnv) {
  EXPECT_EQ("v1.18.0 21-Nov-2026 abc1234|Xiao_nrf52_solar_lite_repeater",
            ident("v1.18.0", "abc1234", "Xiao_nrf52_solar_lite_repeater"));
}

TEST(OtaIdent, BranchBuildDoesNotRepeatTheCommit) {
  // build.sh: FIRMWARE_VERSION is "<version>-<sha7>"
  EXPECT_EQ("v1.18.0 21-Nov-2026 abc1234|RAK_4631_repeater", ident("v1.18.0-abc1234", "abc1234", "RAK_4631_repeater"));
}

TEST(OtaIdent, LongBranchNameIsCutFromItsStartAndTheCommitSurvives) {
  std::string s = ident("claude/some-very-long-branch-name-for-a-test-thread-abc1234", "abc1234", "Xiao_nrf52_solar_lite_repeater");
  EXPECT_LE(s.size(), 60u) << s;
  EXPECT_NE(std::string::npos, s.find(" 21-Nov-2026 abc1234|Xiao_nrf52_solar_lite_repeater"));
  EXPECT_EQ(0u, s.find("st-thread 21-Nov")) << s;
}

TEST(OtaIdent, NoEnvUsesTheManufacturerAndNothingOverflows) {
  EXPECT_EQ("v1.18.0 21-Nov-2026 abc1234|Seeed Xiao", ident("v1.18.0-abc1234", "abc1234", NULL));
  EXPECT_EQ("v1.18.0 21-Nov-2026 local|Seeed Xiao", ident("v1.18.0", "local", ""));
}

TEST(OtaIdent, TooLongEnvFallsBackToTheManufacturerAndAlwaysFitsInAnySize) {
  std::string env(80, 'e');
  std::string s = ident("v1.18.0", "abc1234", env.c_str());
  EXPECT_EQ("v1.18.0 21-Nov-2026 abc1234|Seeed Xiao", s);
  for (size_t cap = 0; cap < 80; cap++) {   // never writes past the buffer
    char buf[100];
    memset(buf, 'X', sizeof(buf));
    formatOtaIdent(buf, cap, "a-very-long-version-string-abc1234", "21-Nov-2026", "abc1234", env.c_str(), "M");
    if (cap) EXPECT_LT(strlen(buf), cap);
    for (size_t i = cap; i < sizeof(buf); i++) ASSERT_EQ('X', buf[i]);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
