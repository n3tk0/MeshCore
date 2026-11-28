#include <gtest/gtest.h>
#include <helpers/nrf52/BootloaderInfo.h>
#include <string>

// The text as it sits in the bootloader's flash (initialised data), with bytes around it.
static std::string image(const char* text) {
  std::string s(200, '\xFF');
  s += text;
  s.append(384 - strlen(text), '\0');
  s.append(100, '\x5A');
  return s;
}

TEST(BootloaderInfo, OursFromXiao) {
  std::string img = image(
      "UF2 Bootloader 0.9.2-OTAFIX2.3-BP1.4-mcota1 lib/nrfx (7a4c9d9) lib/tinyusb (9775e76) lib/uf2 (adb)\r\n"
      "Model: Seeed XIAO nRF52840\r\nBoard-ID: nRF52840-SeeedXiaoSense-v1\r\nDate: Oct 22 2026\r\n");
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  ASSERT_TRUE(bl_parseInfoText(img.data(), img.data() + img.size(), bi));
  EXPECT_EQ(BL_OTAFIX, bi.kind);
  EXPECT_STREQ("0.9.2-OTAFIX2.3-BP1.4-mcota1", bi.version);
  EXPECT_STREQ("Seeed XIAO nRF52840", bi.model);
  EXPECT_STREQ("nRF52840-SeeedXiaoSense-v1", bi.board);
  EXPECT_STREQ("2026-10-22", bi.date);

  bi.kind = BL_OURS;   // the MOT1 word, read from flash on the device
  bi.features = 3;
  strcpy(bi.softdevice, "S140/6.1.1");
  char out[156];
  formatBootloaderInfo(bi, out, 155);
  EXPECT_STREQ("ours ver=0.9.2-OTAFIX2.3-BP1.4-mcota1 board=nRF52840-SeeedXiaoSense-v1 date=2026-10-22 "
               "sd=S140/6.1.1 feat=staged,rollback", out);
}

TEST(BootloaderInfo, StockAdafruitSpaceInBoardAndShortDay) {
  std::string img = image("UF2 Bootloader 0.6.1 lib/nrfx (v2.0.0)\r\nModel: Tracker\r\nBoard-ID: TRACKER L1\r\nDate: Jan  5 2022\r\n");
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  ASSERT_TRUE(bl_parseInfoText(img.data(), img.data() + img.size(), bi));
  EXPECT_EQ(BL_ADAFRUIT, bi.kind);
  EXPECT_STREQ("0.6.1", bi.version);
  EXPECT_STREQ("2022-01-05", bi.date);
  char out[156];
  formatBootloaderInfo(bi, out, 155);
  EXPECT_STREQ("adafruit ver=0.6.1 board=TRACKER_L1 date=2022-01-05", out);
}

TEST(BootloaderInfo, NotFound) {
  std::string img(500, '\xFF');
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.kind = BL_OTHER;
  bi.start = 0xF4000;
  EXPECT_FALSE(bl_parseInfoText(img.data(), img.data() + img.size(), bi));
  char out[156];
  formatBootloaderInfo(bi, out, 155);
  EXPECT_STREQ("other at=F4000", out);
}

TEST(BootloaderInfo, FieldsStopAtTheTextEnd) {
  // a line key after the NUL is not the info text's
  std::string img = image("UF2 Bootloader 1.0\r\n");
  img += "Model: X\r\n";
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  ASSERT_TRUE(bl_parseInfoText(img.data(), img.data() + img.size(), bi));
  EXPECT_STREQ("", bi.model);
}

TEST(BootloaderInfo, LongLineIsCut) {
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  bi.kind = BL_OURS;
  memset(bi.version, 'v', sizeof(bi.version) - 1);
  memset(bi.board, 'b', sizeof(bi.board) - 1);
  strcpy(bi.date, "2026-10-22");
  strcpy(bi.softdevice, "S140/7.3.0");
  bi.features = 3;
  char out[156];
  out[155] = 'X';
  formatBootloaderInfo(bi, out, 155);
  EXPECT_LT(strlen(out), 155u);
  EXPECT_EQ('X', out[155]);
}

TEST(BootloaderInfo, ShortDateIsCopied) {
  std::string img = image("UF2 Bootloader 1.0\r\nDate: X\r\n");
  BootloaderInfo bi;
  memset(&bi, 0, sizeof(bi));
  ASSERT_TRUE(bl_parseInfoText(img.data(), img.data() + img.size(), bi));
  EXPECT_STREQ("X", bi.date);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
