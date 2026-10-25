#include <gtest/gtest.h>

#include <helpers/BatteryGauge.h>
#include <helpers/PowerSaver.h>
#include <helpers/TelemetryHistory.h>
#include <helpers/LppScan.h>
#include <vector>

// ---------------- BatteryGauge ----------------

TEST(BatteryGauge, VoltageCurveIsMonotonic) {
  int last = -1;
  for (int mv = 3200; mv <= 4250; mv += 10) {
    int p = gaugeVoltagePercent(mv);
    EXPECT_GE(p, last);
    last = p;
  }
  EXPECT_EQ(gaugeVoltagePercent(3000), 0);
  EXPECT_EQ(gaugeVoltagePercent(4200), 100);
}

TEST(BatteryGauge, CountsInAndOutPerDay) {
  BatteryGauge g;
  uint32_t t = 0;
  g.update(t, 3900, 100, 1);
  for (int i = 0; i < 360; i++) { t += 10000; g.update(t, 3900, 100, 1); }   // 1 h at 100 mA out
  EXPECT_NEAR(g.outToday(), 100, 1);
  EXPECT_EQ(g.inToday(), 0);
  for (int i = 0; i < 180; i++) { t += 10000; g.update(t, 3950, -200, 1); }  // 30 min at 200 mA in
  EXPECT_NEAR(g.inToday(), 100, 1);
  t += 10000;
  g.update(t, 3950, 50, 2);   // next day
  EXPECT_NEAR(g.outYesterday(), 100, 1);
  EXPECT_NEAR(g.inYesterday(), 100, 1);
  EXPECT_EQ(g.inToday(), 0);
}

TEST(BatteryGauge, LongGapIsNotIntegrated) {
  BatteryGauge g;
  g.update(0, 3900, 100, 1);
  g.update(3600000, 3900, 100, 1);   // an hour later: the node slept or the sensor was off
  EXPECT_EQ(g.outToday(), 0);
}

TEST(BatteryGauge, FullThenEmptyLearnsCapacity) {
  BatteryGauge g;
  uint32_t t = 0;
  g.update(t, 4180, -5, 1);
  for (int i = 0; i < 20; i++) { t += 10000; g.update(t, 4180, -5, 1); }   // full: trickle at the top
  EXPECT_FALSE(g.takeCapacityChanged());
  // discharge 500 mA for 2 h = 1000 mAh, then empty
  for (int i = 0; i < 720; i++) { t += 10000; g.update(t, 3800, 500, 1); }
  t += 10000;
  g.update(t, 3290, 500, 1);
  EXPECT_TRUE(g.takeCapacityChanged());
  EXPECT_NEAR(g.capacity(), 1001, 3);
  EXPECT_TRUE(g.capacityLearnt());
  EXPECT_EQ(g.percent(), 0);
}

TEST(BatteryGauge, PercentFromCountWithKnownCapacity) {
  BatteryGauge g;
  g.setCapacity(1000);
  uint32_t t = 0;
  g.update(t, 4180, -5, 1);
  for (int i = 0; i < 20; i++) { t += 10000; g.update(t, 4180, -5, 1); }
  EXPECT_EQ(g.percent(), 100);
  for (int i = 0; i < 360; i++) { t += 10000; g.update(t, 3700, 250, 1); }   // 250 mAh out
  EXPECT_NEAR(g.percent(), 75, 1);
  EXPECT_TRUE(g.fromCount());
  EXPECT_GT(g.hoursLeft(), 0);
}

TEST(BatteryGauge, ChargingOnAverageHasNoHoursLeft) {
  BatteryGauge g;
  g.setCapacity(1000);
  uint32_t t = 0;
  for (int i = 0; i < 100; i++) { t += 10000; g.update(t, 3900, -300, 1); }
  EXPECT_EQ(g.hoursLeft(), -2);
}

// ---------------- PowerSaver ----------------

TEST(PowerSaver, NeedsThreeReadingsToGoDown) {
  PowerSaver p;
  EXPECT_EQ(p.update(3550, false), PSAVE_NORMAL);
  EXPECT_EQ(p.update(3550, false), PSAVE_NORMAL);
  EXPECT_EQ(p.update(3550, false), PSAVE_LOW);
}

TEST(PowerSaver, OneHighReadingBreaksTheRun) {
  PowerSaver p;
  p.update(3550, false);
  p.update(3550, false);
  p.update(3700, false);
  EXPECT_EQ(p.update(3550, false), PSAVE_NORMAL);
}

TEST(PowerSaver, GoesBackUpWithHysteresis) {
  PowerSaver p;
  for (int i = 0; i < 3; i++) p.update(3400, false);
  EXPECT_EQ(p.stage(), PSAVE_CRITICAL);
  EXPECT_EQ(p.update(3500, false), PSAVE_CRITICAL);   // above crit, but not by 100 mV
  EXPECT_EQ(p.update(3560, false), PSAVE_LOW);
  EXPECT_EQ(p.update(3710, false), PSAVE_NORMAL);
}

TEST(PowerSaver, ExternalPowerAndBadReadings) {
  PowerSaver p;
  for (int i = 0; i < 3; i++) p.update(3250, false);
  EXPECT_EQ(p.stage(), PSAVE_OFF);
  EXPECT_EQ(p.update(0, false), PSAVE_OFF);         // cannot measure: no change
  EXPECT_EQ(p.update(3250, true), PSAVE_NORMAL);    // on a charger
}

TEST(PowerSaver, ValidOrder) {
  PowerSaver p;
  EXPECT_TRUE(p.valid());
  p.config().crit_mv = 3700;
  EXPECT_FALSE(p.valid());
  p.config().crit_mv = 0;
  EXPECT_TRUE(p.valid());
}

TEST(PowerSaver, DisabledIsNormal) {
  PowerSaver p;
  p.config().enabled = 0;
  for (int i = 0; i < 5; i++) EXPECT_EQ(p.update(3100, false), PSAVE_NORMAL);
}

// ---------------- TelemetryHistory ----------------

static TelemetrySample sample(int16_t mv) {
  TelemetrySample s;
  TelemetryHistory::emptySample(s);
  s.v[TH_F_BATT_MV] = mv;
  s.v[TH_F_RX] = 10;
  return s;
}

TEST(TelemetryHistory, DayRingWrapsAndHoursAverage) {
  static TelemetryHistory h;
  h.clear();
  uint32_t t = 1750000000;
  for (int i = 0; i < 200; i++) { t += 600; h.add(sample(3000 + i), t); }
  EXPECT_EQ(h.count(0), TH_DAY_SLOTS);
  EXPECT_EQ(h.at(0, 0)->v[TH_F_BATT_MV], 3000 + 200 - TH_DAY_SLOTS);
  EXPECT_EQ(h.at(0, TH_DAY_SLOTS - 1)->v[TH_F_BATT_MV], 3199);
  EXPECT_EQ(h.count(1), 200 / 6);
  EXPECT_EQ(h.at(1, 0)->v[TH_F_BATT_MV], (3000 + 3005) / 2);   // average of six
  EXPECT_EQ(h.at(1, 0)->v[TH_F_RX], 60);                     // counters add up
}

TEST(TelemetryHistory, GapsAreFilledWithEmptySlots) {
  static TelemetryHistory h;
  h.clear();
  uint32_t t = 1750000000;
  h.add(sample(3900), t);
  h.add(sample(3800), t + 600 * 4);   // three slots missed
  EXPECT_EQ(h.count(0), 5);
  EXPECT_EQ(h.at(0, 1)->v[TH_F_BATT_MV], TH_NONE);
  EXPECT_EQ(h.at(0, 4)->v[TH_F_BATT_MV], 3800);
}

TEST(TelemetryHistory, UnknownClockDoesNotFill) {
  static TelemetryHistory h;
  h.clear();
  h.add(sample(3900), 100);
  h.add(sample(3800), 100000);
  EXPECT_EQ(h.count(0), 2);
}

TEST(TelemetryHistory, ReplyFormat) {
  static TelemetryHistory h;
  h.clear();
  uint32_t t = 1750000000;
  for (int i = 0; i < 10; i++) { t += 600; h.add(sample(3000 + i), t); }
  uint8_t req[5] = { 0, TH_F_BATT_MV, 2, 0, 3 };
  uint8_t out[64];
  int n = h.formatReply(req, 5, out, sizeof(out));
  ASSERT_EQ(n, 12 + 3 * 2);
  EXPECT_EQ(out[2] | (out[3] << 8), 10);  // total
  EXPECT_EQ(out[6], 3);
  EXPECT_EQ(out[11], TH_DAY_MINS);
  EXPECT_EQ((int16_t)(out[12] | (out[13] << 8)), 3002);
  uint8_t bad[5] = { 2, 0, 0, 0, 1 };
  EXPECT_EQ(h.formatReply(bad, 5, out, sizeof(out)), 0);
}

struct MemFile {
  std::vector<uint8_t> buf;
  size_t pos = 0;
  void write(const uint8_t* p, int n) { buf.insert(buf.end(), p, p + n); }
  int read(uint8_t* p, int n) {
    int k = (int)std::min((size_t)n, buf.size() - pos);
    memcpy(p, &buf[pos], k);
    pos += k;
    return k;
  }
};

TEST(TelemetryHistory, SaveAndLoad) {
  static TelemetryHistory a, b;
  a.clear();
  uint32_t t = 1750000000;
  for (int i = 0; i < 20; i++) { t += 600; a.add(sample(3500 + i), t); }
  MemFile f;
  a.save(f);
  ASSERT_TRUE(b.load(f));
  EXPECT_EQ(b.count(0), 20);
  EXPECT_EQ(b.at(0, 19)->v[TH_F_BATT_MV], 3519);
  EXPECT_EQ(b.lastTime(0), t);
  MemFile shortf;
  shortf.buf.assign(f.buf.begin(), f.buf.begin() + 100);
  EXPECT_FALSE(b.load(shortf));
  EXPECT_EQ(b.count(0), 0);
}

// ---------------- LppScan ----------------

TEST(LppScan, FindsTemperatureAfterOtherValues) {
  // ch1 voltage (116, 2 bytes), ch2 humidity (104, 1 byte), ch2 temperature (103) -12.5 C
  uint8_t buf[] = { 1, 116, 0x01, 0x80, 2, 104, 90, 2, 103, 0xFF, 0x83 };
  int32_t raw;
  ASSERT_TRUE(lppScanFirst(buf, sizeof(buf), LPP_SCAN_TEMPERATURE, &raw));
  EXPECT_EQ(raw, -125);
  ASSERT_TRUE(lppScanFirst(buf, sizeof(buf), LPP_SCAN_HUMIDITY, &raw));
  EXPECT_EQ(raw, 90);
  EXPECT_FALSE(lppScanFirst(buf, sizeof(buf), LPP_SCAN_BAROMETER, &raw));
  uint8_t unknown[] = { 1, 250, 0, 2, 103, 0, 10 };
  EXPECT_FALSE(lppScanFirst(unknown, sizeof(unknown), LPP_SCAN_TEMPERATURE, &raw));
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
