#include <stddef.h>
#include <algorithm>
#include <gtest/gtest.h>

#include <helpers/sensors/BQ25798.h>
#include <helpers/BatteryGauge.h>
#include <string.h>
#include <vector>

// A BQ25798 register file with the behaviour the driver depends on.
class FakeBQ : public BQ25798Bus {
public:
  uint8_t r[256];
  bool fail = false;           // every transfer fails (chip gone)
  bool adc_works = true;       // a started conversion is done at the next read of ADC_CTRL
  bool zeros_after_shot = false; // a chip that clears the results when a one-shot ends
  uint16_t vbat_now = 0;       // for VSYS_STAT when VSYSMIN is written (0 = not modelled)
  bool ce_on = false;          // the ~CE pin as the MCU drives it
  int8_t jumper = -1;          // readCeJumper(): -1 no pin, 0 JP2 1-2, 1 JP2 2-3
  int jumper_reads = 0;
  std::vector<bool> ce_log;
  std::vector<uint8_t> writes; // register numbers written

  FakeBQ() { por(); }

  void put16(uint8_t reg, uint16_t v) { r[reg] = v >> 8; r[reg + 1] = v & 0xFF; }
  uint16_t get16(uint8_t reg) const { return (r[reg] << 8) | r[reg + 1]; }

  void por() {
    memset(r, 0, sizeof(r));
    r[0x00] = 0x04;            // VSYSMIN 3.5 V
    put16(0x01, 420);          // VREG 4.2 V
    put16(0x03, 100);          // ICHG 1 A
    r[0x05] = 0x24;
    put16(0x06, 0x12C);
    r[0x08] = 0xC3;
    r[0x09] = 0x05;
    r[0x0F] = 0xA2;
    r[0x10] = 0x85;            // watchdog 40 s
    r[0x11] = 0x40;            // AUTO_INDET_EN
    r[0x14] = 0x16;
    r[0x15] = 0xAA;
    r[0x17] = 0x7A;
    r[0x18] = 0x54;
    r[0x2E] = 0x30;
    r[0x48] = 0x19;            // PN 011, rev 1
  }

  void sun(bool on) {
    if (on) r[0x1B] |= 0x09; else { r[0x1B] &= ~0x09; r[0x15] &= ~0x01; }   // the chip drops EN_MPPT
  }
  void adcValues(uint16_t vbat, int16_t ibat, uint16_t vbus, int16_t ibus) {
    put16(0x31, (uint16_t)ibus);
    put16(0x33, (uint16_t)ibat);
    put16(0x35, vbus);
    put16(0x3B, vbat);
    put16(0x3D, vbat + 50);
    put16(0x41, 80);           // 40 °C
  }

  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) override {
    if (fail) return false;
    if (reg == 0x2E && adc_works && (r[0x2E] & 0xC0) == 0xC0) {   // one-shot done (continuous keeps going)
      r[0x2E] &= ~0x80;
      if (zeros_after_shot) memset(&r[0x31], 0, 0x42 - 0x31 + 1);
    }
    memcpy(buf, &r[reg], n);
    return true;
  }
  bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) override {
    if (fail) return false;
    for (uint8_t i = 0; i < n; i++) {
      uint8_t a = reg + i, v = buf[i];
      writes.push_back(a);
      if (a == 0x15 && (v & 1) && !(r[0x1B] & 0x01)) v &= ~1;              // no MPPT without VBUS
      if (a == 0x11 && ((v >> 1) & 3) && (r[0x1B] & 0x01)) v &= ~0x06;    // no ship/shutdown with VBUS
      if (a == 0x11 && !(r[0x14] & 0x80)) v &= ~0x06;                      // SDRV_CTRL locked without SFET_PRESENT
      r[a] = v;
      if (a == 0x00 && vbat_now) {                                         // VSYS_STAT follows VSYSMIN
        if (vbat_now < 2500 + (v & 0x3F) * 250) r[0x1E] |= 0x10; else r[0x1E] &= ~0x10;
      }
    }
    return true;
  }
  void setChargeEnable(bool on) override { ce_on = on; ce_log.push_back(on); }
  int8_t readCeJumper() override { jumper_reads++; return jumper; }
};

static BQ25798Settings settings(uint8_t chem = BQ_CHEM_LIION) {
  BQ25798Settings s;
  BQ25798::defaults(s);
  if (chem != s.chem) { s.chem = chem; s.cutoff_mv = BQ25798::defaultCutoff(chem); }
  return s;
}

static void runFor(BQ25798& bq, uint32_t& t, uint32_t ms) {
  for (uint32_t end = t + ms; t < end; ) { t += 10000; bq.loop(t); }
}

TEST(BQ25798, FindsThePartAndStopsTheWatchdog) {
  FakeBQ f;
  BQ25798 bq(f);
  EXPECT_TRUE(bq.begin(0));
  EXPECT_EQ(f.r[0x10] & 0x07, 0);       // watchdog off
  EXPECT_EQ(f.r[0x10] & 0x30, 0);       // VAC_OVP 26 V
  EXPECT_TRUE(f.ce_log.empty());        // charging left to the jumper until settings come

  FakeBQ other;
  other.r[0x48] = 0x09;                 // another part number
  BQ25798 bq2(other);
  EXPECT_FALSE(bq2.begin(0));

  FakeBQ gone;
  gone.fail = true;
  BQ25798 bq3(gone);
  EXPECT_FALSE(bq3.begin(0));
}

TEST(BQ25798, LiIonDefaults) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  EXPECT_EQ(f.get16(0x01), 420);
  EXPECT_EQ(f.get16(0x03), 100);
  EXPECT_EQ(f.get16(0x06), 300);
  EXPECT_EQ(f.r[0x11] & 0x40, 0);       // no D+/D- detection
  EXPECT_EQ(f.r[0x14] & 0xA0, 0xA0);    // SFET_PRESENT, EN_IBAT
  EXPECT_EQ(f.r[0x18] & 1, 1);          // TS_IGNORE (no NTC)
  EXPECT_EQ(f.r[0x0F] & 0x30, 0x20);    // EN_CHG, no ICO
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ_MPPT_BASE);
  EXPECT_TRUE(f.ce_on);
}

TEST(BQ25798, TerminationFollowsTheChargeCurrent) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  BQ25798Settings s = settings();
  bq.apply(s);
  EXPECT_EQ(f.r[0x09] & 0x1F, 3);       // 1 A -> 120 mA (not the chip's 200 mA)
  EXPECT_EQ(f.r[0x12], 0);              // CHARGER_CONTROL_3 untouched
  s.ichg_ma = 250;
  bq.apply(s);
  EXPECT_EQ(f.r[0x09] & 0x1F, 1);       // 250 mA -> 40 mA, the lowest
}

TEST(BQ25798, LiFePO4NeverChargesTowards42V) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings(BQ_CHEM_LIFEPO4));
  ASSERT_FALSE(f.ce_log.empty());
  EXPECT_FALSE(f.ce_log.front());       // CE held off first
  EXPECT_EQ(f.get16(0x01), 360);
  EXPECT_TRUE(f.ce_on);

  // a power-on reset while running (battery and panel both gone a moment): VREG back at 4.2 V
  f.ce_log.clear();
  f.por();
  f.r[0x0F] |= 0x04;                    // EN_HIZ must be cleared by enforce()
  bq.loop(20000);
  ASSERT_GE(f.ce_log.size(), 2u);
  EXPECT_FALSE(f.ce_log[0]);
  EXPECT_TRUE(f.ce_log.back());
  EXPECT_EQ(f.get16(0x01), 360);
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
  EXPECT_EQ(f.r[0x10] & 0x07, 0);
  EXPECT_EQ(bq.reconfigs(), 1);
}

TEST(BQ25798, CeEnablesAfterCurrentAndVsysminAreWritten) {
  struct TraceBQ : public FakeBQ {
    std::vector<uint8_t> ce_write_counts;
    void setChargeEnable(bool on) override {
      FakeBQ::setChargeEnable(on);
      if (on) ce_write_counts.push_back((uint8_t)writes.size());
    }
  } f;
  BQ25798 bq(f);
  bq.begin(0);
  BQ25798Settings s = settings(BQ_CHEM_LIFEPO4);
  s.ichg_ma = 400;
  f.writes.clear();
  bq.apply(s);
  ASSERT_EQ(f.ce_write_counts.size(), 1u);
  EXPECT_EQ(f.ce_write_counts[0], f.writes.size());   // all config registers written before ~CE goes low
  EXPECT_EQ(f.get16(0x03), 40);
  EXPECT_EQ(f.r[0x00] & 0x3F, 2);
}

TEST(BQ25798, VregThatWillNotTakeKeepsChargingOff) {
  // a chip that ignores the VREG write
  struct Stuck : public FakeBQ {
    bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) override {
      if (reg == 0x01) return true;
      return FakeBQ::writeRegs(reg, buf, n);
    }
  } s;
  BQ25798 bq2(s);
  bq2.begin(0);
  bq2.apply(settings(BQ_CHEM_LIFEPO4));
  EXPECT_FALSE(s.ce_on);
}

TEST(BQ25798, LiFePO4LowersVsysminSoMpptCanRun) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings(BQ_CHEM_LIFEPO4));
  EXPECT_EQ(f.r[0x00] & 0x3F, 2);       // 3.0 V
  bq.apply(settings(BQ_CHEM_LIION));
  EXPECT_EQ(f.r[0x00] & 0x3F, 4);       // 3.5 V
  EXPECT_EQ(f.get16(0x01), 420);
}

TEST(BQ25798, RestartAfterAnOutageStartsTheLowTimerAgain) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  f.adcValues(3250, -40, 0, 0);
  runFor(bq, t, 3 * 60000);
  bq.begin(t + 3600000);                // off for I2C errors for an hour, then back
  bq.apply(settings());
  t += 3600000;
  runFor(bq, t, 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 0);   // not at once: 5 more minutes first
  runFor(bq, t, 6 * 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 1);
}

TEST(BQ25798, MpptComesBackEachMorning) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  EXPECT_EQ(f.r[0x15] & 1, 0);          // night: the chip will not take it
  uint32_t t = 0;
  f.sun(true);
  bq.loop(t += 10000);
  EXPECT_EQ(f.r[0x15] & 1, 1);
  EXPECT_TRUE(bq.mpptOn());
  f.sun(false);                         // evening: the chip clears it
  bq.loop(t += 10000);
  EXPECT_FALSE(bq.mpptOn());
  f.sun(true);
  bq.loop(t += 10000);
  EXPECT_EQ(f.r[0x15] & 1, 1);

  // battery under VSYSMIN: not tried (the chip would refuse)
  f.sun(false);
  bq.loop(t += 10000);
  f.r[0x1E] |= 0x10;
  f.sun(true);
  f.writes.clear();
  bq.loop(t += 10000);
  EXPECT_EQ(f.r[0x15] & 1, 0);
}

TEST(BQ25798, FixedInputVoltage) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  BQ25798Settings s = settings();
  s.vindpm_mv = 15000;
  f.sun(true);
  f.r[0x15] |= 1;
  bq.apply(s);
  EXPECT_EQ(f.r[0x15] & 1, 0);
  EXPECT_EQ(f.r[0x05], 150);
}

TEST(BQ25798, LiFePO4MpptStartsAtOnceAfterVsysminDrops) {
  FakeBQ f;
  f.vbat_now = 3250;                    // a LiFePO4 cell on its plateau, under the 3.5 V power-on VSYSMIN
  f.r[0x1E] |= 0x10;                    // VSYS_STAT: in VSYSMIN regulation
  f.sun(true);
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings(BQ_CHEM_LIFEPO4));  // VSYSMIN 3.0 V, then MPPT in the same pass
  EXPECT_EQ(f.r[0x00] & 0x3F, 2);
  EXPECT_TRUE(f.r[0x15] & 0x01);
  EXPECT_TRUE(bq.mpptOn());
}

TEST(BQ25798, AdcGoesContinuousIfOneShotResultsReadAsZeros) {
  FakeBQ f;
  f.zeros_after_shot = true;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  f.adcValues(3950, -120, 0, 0);
  bq.loop(0);                           // one shot
  bq.loop(10000);                       // zeros: continuous instead
  EXPECT_FALSE(bq.reading().valid);
  EXPECT_TRUE(bq.adcContinuous());
  EXPECT_EQ(f.r[0x2E], BQ_ADC_CONTINUOUS);
  f.adcValues(3950, -120, 0, 0);        // the chip converts all the time now
  bq.loop(20000);
  ASSERT_TRUE(bq.reading().valid);
  EXPECT_EQ(bq.reading().vbat_mv, 3950);
  uint32_t t = 20000;
  runFor(bq, t, 600000);                // no shutdown from a "stale" ADC while it keeps reading
  EXPECT_FALSE(bq.shutdownSent());
  f.r[0x2E] = 0x30;                     // a power-on reset turns it off: back on
  bq.loop(t + 10000);
  EXPECT_EQ(f.r[0x2E], BQ_ADC_CONTINUOUS);
}

TEST(BQ25798, ReadsTheAdc) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  f.adcValues(3950, -120, 0, 0);
  bq.loop(0);                           // starts a conversion
  EXPECT_FALSE(bq.reading().valid);
  EXPECT_EQ(f.r[0x2E], BQ_ADC_START);
  EXPECT_EQ(f.r[0x2F], BQ_ADC_DIS0_VAL);
  bq.loop(10000);                       // reads it, starts the next
  ASSERT_TRUE(bq.reading().valid);
  EXPECT_EQ(bq.reading().vbat_mv, 3950);
  EXPECT_EQ(bq.reading().ibat_ma, -120);
  EXPECT_EQ(bq.reading().tdie_dc, 400);
  EXPECT_EQ(bq.adcCount(), 1u);
}

TEST(BQ25798, NtcTemperature) {
  // 5.1k / 30k divider, 10k B3435: 25 °C = 59.5 %, 0 °C = 74.2 %, 60 °C = 34.7 %
  EXPECT_NEAR(BQ25798::ntcTemp(595), 250, 5);
  EXPECT_NEAR(BQ25798::ntcTemp(742), 0, 5);
  EXPECT_NEAR(BQ25798::ntcTemp(347), 600, 8);
  EXPECT_EQ(BQ25798::ntcTemp(855), BQ_TCELL_NONE);   // open: 30k alone
  EXPECT_EQ(BQ25798::ntcTemp(0), BQ_TCELL_NONE);     // shorted / not converted
  EXPECT_LT(BQ25798::ntcTemp(800), 0);               // cold still reads
}

static int tsShots(FakeBQ& f, BQ25798& bq, uint32_t& t, uint32_t ms) {   // conversions started with TS on
  int n = 0;
  for (uint32_t end = t + ms; t < end; ) {
    t += 10000;
    bq.loop(t);
    if (f.r[0x2F] == BQ_ADC_DIS0_TS && (f.r[0x2E] & 0x80)) n++;
  }
  return n;
}

TEST(BQ25798, CellTemperatureWithSunAndEvery10MinAtNight) {
  FakeBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  BQ25798Settings s = settings();
  s.ntc = 1;
  bq.apply(s);
  f.sun(true);
  f.adcValues(3950, 500, 18000, 120);
  f.put16(0x3F, 609);                   // 59.5 %: 25 °C
  bq.loop(t);
  EXPECT_EQ(f.r[0x2F], BQ_ADC_DIS0_TS);   // TS converted with sun
  t += 10000;
  bq.loop(t);
  EXPECT_NEAR(bq.reading().tcell_dc, 250, 5);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "cell 2"), nullptr) << buf;

  f.sun(false);                         // night, battery 3.95 V: one TS conversion now, then every 10 min
  EXPECT_EQ(tsShots(f, bq, t, 10000), 1);
  EXPECT_EQ(tsShots(f, bq, t, 580000), 0);
  EXPECT_EQ(f.r[0x2F], BQ_ADC_DIS0_VAL);
  EXPECT_NEAR(bq.reading().tcell_dc, 250, 5);   // kept between the night conversions
  f.put16(0x3F, 760);                   // 74.2 %: 0 °C
  EXPECT_EQ(tsShots(f, bq, t, 30000), 1);
  t += 10000;
  bq.loop(t);
  EXPECT_NEAR(bq.reading().tcell_dc, 0, 5);

  f.adcValues(3250, -50, 0, 0);         // under 3.3 V: no more TS, the value goes after 15 min
  EXPECT_EQ(tsShots(f, bq, t, 900000), 0);
  EXPECT_EQ(bq.reading().tcell_dc, BQ_TCELL_NONE);

  f.sun(true);                          // NTC missing although `ntc on`
  f.adcValues(3950, 500, 18000, 120);
  f.put16(0x3F, 876);                   // 85.5 %
  runFor(bq, t, 30000);
  EXPECT_EQ(bq.reading().tcell_dc, BQ_TCELL_NONE);
  EXPECT_TRUE(bq.ntcOpen());
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "NTC open?"), nullptr) << buf;

  FakeBQ g;                             // ntc off: TS never converted, day or night
  BQ25798 bq2(g);
  uint32_t t2 = 0;
  bq2.begin(t2);
  bq2.apply(settings());
  g.sun(true);
  g.adcValues(3950, 500, 18000, 120);
  g.put16(0x3F, 609);
  EXPECT_EQ(tsShots(g, bq2, t2, 60000), 0);
  g.sun(false);
  EXPECT_EQ(tsShots(g, bq2, t2, 1200000), 0);
  EXPECT_EQ(bq2.reading().tcell_dc, BQ_TCELL_NONE);
}

// the TS ADC code (0.0977 % of REGN a step) closest to a cell temperature in 0.1 °C
static uint16_t tsFor(int dc) {
  uint16_t best = 0; int err = 1 << 30;
  for (uint16_t raw = 300; raw < 860; raw++) {
    int t = BQ25798::ntcTemp((uint16_t)((raw * 1000UL + 512) / 1024));
    if (t != BQ_TCELL_NONE && abs(t - dc) < err) { err = abs(t - dc); best = raw; }
  }
  return best;
}

TEST(BQ25798, ColdCellChargesAtALowCurrentDownToMinus10) {
  FakeBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  BQ25798Settings s = settings();
  s.ntc = 1;
  s.ichg_ma = 250;
  bq.apply(s);
  f.sun(true);
  f.adcValues(3980, 0, 5800, 50);
  f.put16(0x3F, tsFor(150));            // 15 °C: the chip's own TS, full current
  runFor(bq, t, 30000);
  EXPECT_FALSE(bq.cold());
  EXPECT_EQ(f.r[0x18] & 1, 0);
  EXPECT_EQ(((f.r[0x03] << 8) | f.r[0x04]) & 0x1FF, 25);
  EXPECT_TRUE(f.r[0x0F] & 0x20);

  f.put16(0x3F, tsFor(30));             // 3 °C: TS_IGNORE, 100 mA
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.cold());
  EXPECT_EQ(f.r[0x18] & 1, 1);
  EXPECT_EQ(((f.r[0x03] << 8) | f.r[0x04]) & 0x1FF, 10);
  EXPECT_TRUE(f.r[0x0F] & 0x20);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "cold 100mA"), nullptr) << buf;

  f.put16(0x3F, tsFor(60));             // 6 °C: still cold (hysteresis to 7 °C)
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.cold());

  f.put16(0x3F, tsFor(-120));           // -12 °C: no charging, the chip's TS back
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.coldStop());
  EXPECT_FALSE(f.r[0x0F] & 0x20);
  EXPECT_EQ(f.r[0x18] & 1, 0);
  f.put16(0x3F, tsFor(-90));            // -9 °C: still stopped (to -8 °C)
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.coldStop());
  f.put16(0x3F, tsFor(-50));            // -5 °C: cold charging again
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.cold());
  EXPECT_TRUE(f.r[0x0F] & 0x20);

  f.put16(0x3F, tsFor(80));             // 8 °C: back to normal
  runFor(bq, t, 30000);
  EXPECT_FALSE(bq.cold());
  EXPECT_FALSE(bq.coldStop());
  EXPECT_EQ(f.r[0x18] & 1, 0);
  EXPECT_EQ(((f.r[0x03] << 8) | f.r[0x04]) & 0x1FF, 25);
}

TEST(BQ25798, ColdOffOrNoNtcLeavesTheChipAlone) {
  FakeBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  BQ25798Settings s = settings();
  s.ntc = 1;
  s.cold_10ma = BQ_COLD_OFF;
  bq.apply(s);
  f.sun(true);
  f.adcValues(3980, 0, 5800, 50);
  f.put16(0x3F, tsFor(20));
  runFor(bq, t, 30000);
  EXPECT_FALSE(bq.cold());
  EXPECT_EQ(f.r[0x18] & 1, 0);
  s.cold_10ma = 15;                     // 150 mA
  bq.apply(s);
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.cold());
  EXPECT_EQ(((f.r[0x03] << 8) | f.r[0x04]) & 0x1FF, 15);
  f.put16(0x3F, 870);                   // NTC open: no reading, the chip's TS
  runFor(bq, t, 30000);
  EXPECT_FALSE(bq.cold());
  EXPECT_EQ(f.r[0x18] & 1, 0);
  BQ25798Settings bad = s;
  bad.cold_10ma = 3;                    // 30 mA: under the range
  EXPECT_FALSE(BQ25798::valid(bad));
}

TEST(BQ25798, SunsetDuringATsConversionRestartsWithoutTs) {
  FakeBQ f;
  f.adc_works = false;                  // the conversion hangs (battery under 3.2 V with TS on)
  BQ25798 bq(f);
  bq.begin(0);
  BQ25798Settings s = settings();
  s.ntc = 1;
  bq.apply(s);
  f.sun(true);
  bq.loop(0);
  EXPECT_EQ(f.r[0x2F], BQ_ADC_DIS0_TS);
  f.sun(false);
  f.writes.clear();
  bq.loop(10000);
  EXPECT_EQ(f.r[0x2F], BQ_ADC_DIS0_VAL);
  EXPECT_NE(std::find(f.writes.begin(), f.writes.end(), 0x2E), f.writes.end());   // started again
  f.writes.clear();
  bq.loop(20000);                       // without TS now: no more restarts
  EXPECT_EQ(std::find(f.writes.begin(), f.writes.end(), 0x2E), f.writes.end());
}

TEST(BQ25798, EmptyBatteryWithNoSunShutsDown) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  f.adcValues(3250, -40, 0, 0);
  runFor(bq, t, 4 * 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 0);
  runFor(bq, t, 2 * 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 1);   // shutdown
  EXPECT_EQ(f.r[0x11] & 1, 0);          // with the 10 s delay
  EXPECT_TRUE(bq.shutdownSent());
}

TEST(BQ25798, NoShutdownWithSunOrWithoutShipFet) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  f.adcValues(3250, 200, 5000, 300);
  f.sun(true);
  runFor(bq, t, 10 * 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 0);

  FakeBQ g;
  BQ25798 bq2(g);
  bq2.begin(0);
  BQ25798Settings s = settings();
  s.shipfet = 0;
  bq2.apply(s);
  g.adcValues(3250, -40, 0, 0);
  t = 0;
  runFor(bq2, t, 10 * 60000);
  EXPECT_EQ((g.r[0x11] >> 1) & 3, 0);
  EXPECT_EQ(g.r[0x14] & 0x80, 0);
}

TEST(BQ25798, AdcThatStopsMeansUnder29V) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  f.adcValues(3400, -40, 0, 0);
  runFor(bq, t, 30000);
  ASSERT_TRUE(bq.reading().valid);
  f.adc_works = false;                  // the cell fell under 2.9 V: no more conversions
  runFor(bq, t, 7 * 60000);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 1);
}

TEST(BQ25798, ShipModeCommand) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  f.sun(true);
  EXPECT_EQ(bq.shipMode(), -1);
  f.sun(false);
  EXPECT_EQ(bq.shipMode(), 0);
  EXPECT_EQ((f.r[0x11] >> 1) & 3, 2);
  BQ25798Settings s = settings();
  s.shipfet = 0;
  bq.apply(s);
  EXPECT_EQ(bq.shipMode(), -2);
}

TEST(BQ25798, ChargeDoneIsReportedOnce) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  f.r[0x1C] = 3 << 5;
  bq.loop(0);
  EXPECT_FALSE(bq.takeFull());
  f.r[0x1C] = 7 << 5;
  bq.loop(10000);
  EXPECT_TRUE(bq.takeFull());
  EXPECT_FALSE(bq.takeFull());
  bq.loop(20000);
  EXPECT_FALSE(bq.takeFull());
}

TEST(BQ25798, SettingsValidation) {
  BQ25798Settings s = settings();
  EXPECT_TRUE(BQ25798::valid(s));
  s.ichg_ma = 50;
  EXPECT_FALSE(BQ25798::valid(s));
  s = settings();
  s.vindpm_mv = 3000;
  EXPECT_FALSE(BQ25798::valid(s));
  s = settings();
  s.version = 9;
  EXPECT_FALSE(BQ25798::valid(s));
  s = settings();
  s.cutoff_mv = 0;
  EXPECT_TRUE(BQ25798::valid(s));
  s = settings(BQ_CHEM_LIFEPO4);
  s.cutoff_mv = 3500;                   // over a LiFePO4 cell's resting voltage: off every evening
  EXPECT_FALSE(BQ25798::valid(s));
  s.cutoff_mv = 3000;
  EXPECT_TRUE(BQ25798::valid(s));
}

TEST(BQ25798, LiFePO4JumperHoldsChargingUntilChemIsSet) {
  FakeBQ f;
  f.jumper = 1;                         // JP2 at 2-3
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());                 // no /charger file yet: Li-ion defaults
  EXPECT_TRUE(bq.chargeHeld());
  EXPECT_FALSE(f.ce_on);
  runFor(bq, t, 60000);
  EXPECT_FALSE(f.ce_on);                // stays off at every poll
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "HELD"), nullptr) << buf;

  bq.begin(t);                          // back after I2C errors: the pin is driven now, not read again
  EXPECT_EQ(f.jumper_reads, 1);

  bq.apply(settings(BQ_CHEM_LIFEPO4));  // charger chem lifepo4
  EXPECT_FALSE(bq.chargeHeld());
  EXPECT_EQ(f.get16(0x01), 360);
  EXPECT_TRUE(f.ce_on);
}

TEST(BQ25798, LiIonJumperOrNoPinChargesAsBefore) {
  for (int8_t j : { (int8_t)0, (int8_t)-1 }) {
    FakeBQ f;
    f.jumper = j;
    BQ25798 bq(f);
    bq.begin(0);
    bq.apply(settings());
    EXPECT_FALSE(bq.chargeHeld());
    EXPECT_TRUE(f.ce_on);
  }
  FakeBQ f;
  f.jumper = 0;                         // JP2 1-2 with a LiFePO4 cell: charges, but warns
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings(BQ_CHEM_LIFEPO4));
  EXPECT_TRUE(f.ce_on);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "JP2 1-2"), nullptr) << buf;
}

// A weak panel at dawn: the converter (VINDPM under 70 % of Voc) pulls it under VPOORSRC, the chip sets
// EN_HIZ, PG = 0 and the poor source flag; the VBUS ADC reads Voc while the converter is off.
struct DawnBQ : public FakeBQ {
  uint16_t voc = 6000, loaded = 3300;
  std::vector<std::pair<uint8_t, uint8_t>> wlog;   // register, value, in order
  int poor_events = 0;
  bool stubborn = false;   // the panel never holds, whatever VINDPM is
  void measure() {                     // what the ADC converts now
    bool off = (r[0x0F] & 0x04) || !(r[0x1B] & 0x08);
    put16(0x35, (r[0x2F] & 0x20) ? 0 : off ? voc : loaded);   // VBUS_ADC_DIS: nothing
    put16(0x3B, 3900);
    put16(0x3D, 3950);
  }
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) override {
    if (reg <= 0x35 && (r[0x2E] & 0xC0) == 0x80) measure();   // continuous conversion keeps going
    bool ok = FakeBQ::readRegs(reg, buf, n);
    if (ok && reg <= 0x22 && reg + n > 0x22) r[0x22] = 0;   // the flags clear when read
    return ok;
  }
  bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) override {
    bool ok = FakeBQ::writeRegs(reg, buf, n);
    for (uint8_t i = 0; ok && i < n; i++) {
      uint8_t a = reg + i;
      wlog.push_back({ a, buf[i] });
      if (a == 0x2E && (buf[i] & 0x80)) measure();   // a conversion: Voc without load, else the loaded panel
      if (a == 0x0F && !(buf[i] & 0x04) && (r[0x1B] & 0x01)) {   // EN_HIZ cleared: the chip qualifies the panel
        if (!stubborn && r[0x05] * 100 * 10 >= voc * 7) { r[0x1B] |= 0x08; }      // VINDPM high enough: the panel holds
        else { r[0x1B] &= ~0x08; r[0x0F] |= 0x04; r[0x22] |= 0x10; poor_events++; }
      }
    }
    return ok;
  }
  void dawn() {
    sun(true);
    r[0x1B] &= ~0x08;                   // no PG
    r[0x0F] |= 0x04;                    // EN_HIZ
    r[0x22] = 0x10;                     // poor source
    r[0x05] = 0x24;                     // 3.6 V
    adcValues(3900, 0, voc, 0);
  }
  int firstWrite(uint8_t reg, int from = 0) const {
    for (size_t i = from; i < wlog.size(); i++) if (wlog[i].first == reg) return (int)i;
    return -1;
  }
};

TEST(BQ25798, VindpmFromVoc) {
  EXPECT_EQ(BQ25798::vindpmFromVoc(6000), 4800);
  EXPECT_EQ(BQ25798::vindpmFromVoc(5999), 4800);     // 81.25 %, rounded down to 100 mV
  EXPECT_EQ(BQ25798::vindpmFromVoc(4000), 3600);     // 3.2 V: clamped to the minimum
  EXPECT_EQ(BQ25798::vindpmFromVoc(0), 3600);
  EXPECT_EQ(BQ25798::vindpmFromVoc(18000), 14600);
  EXPECT_EQ(BQ25798::vindpmFromVoc(40000), 22000);   // clamped to the maximum
  EXPECT_EQ(BQ25798::vindpmFromVoc(65535), 22000);
}

TEST(BQ25798, VocPctSettingParseAndReject) {
  EXPECT_EQ(BQ25798::vocCode(63), 1);
  EXPECT_EQ(BQ25798::vocCode(69), 2);
  EXPECT_EQ(BQ25798::vocCode(75), 3);
  EXPECT_EQ(BQ25798::vocCode(81), 4);
  EXPECT_EQ(BQ25798::vocCode(88), 5);
  for (int bad : {0, 56, 62, 64, 68, 70, 80, 82, 87, 93, 100, -1}) EXPECT_EQ(BQ25798::vocCode(bad), -1) << bad;
  BQ25798Settings s = settings();
  EXPECT_EQ(s.voc_pct, 81);                          // the default
  for (int ok : {63, 69, 75, 81, 88}) { s.voc_pct = ok; EXPECT_TRUE(BQ25798::valid(s)) << ok; }
  s.voc_pct = 80;
  EXPECT_FALSE(BQ25798::valid(s));
  s.voc_pct = 0;
  EXPECT_FALSE(BQ25798::valid(s));
}

TEST(BQ25798, Reg15FollowsVocPct) {
  EXPECT_EQ(BQ25798::mpptBase(81), BQ_MPPT_BASE);
  const struct { uint8_t pct, base; } cases[] = { {63, 0x2C}, {69, 0x4C}, {75, 0x6C}, {81, 0x8C}, {88, 0xAC} };
  for (auto& c : cases) {
    FakeBQ f;
    BQ25798 bq(f);
    uint32_t t = 0;
    bq.begin(t);
    BQ25798Settings s = settings();
    s.voc_pct = c.pct;
    bq.apply(s);
    runFor(bq, t, 30000);
    EXPECT_EQ(f.r[0x15] & 0xFE, c.base) << (int)c.pct;   // VOC_PCT in 7:5, DLY / RATE as before
    EXPECT_EQ(f.r[0x15] & 0x1F, 0x0C | (f.r[0x15] & 1)) << (int)c.pct;
    s.voc_pct = 75;                                       // changed later: written again
    bq.apply(s);
    runFor(bq, t, 30000);
    EXPECT_EQ(f.r[0x15] & 0xFE, 0x6C);
  }
}

TEST(BQ25798, OldChargerFileDefaultsVocPct) {
  BQ25798Settings s = settings();
  s.ichg_ma = 1500;
  s.voc_pct = 88;
  const int OLD = 10;                                   // the file before voc_pct
  ASSERT_EQ((int)offsetof(BQ25798Settings, voc_pct), OLD);
  BQ25798Settings o;
  ASSERT_TRUE(BQ25798::fromFile(&s, OLD, o));           // old file: no voc_pct byte
  EXPECT_EQ(o.ichg_ma, 1500);
  EXPECT_EQ(o.voc_pct, 81);
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s), o));     // new file
  EXPECT_EQ(o.voc_pct, 88);
  EXPECT_FALSE(BQ25798::fromFile(&s, OLD - 1, o));      // short
  s.voc_pct = 77;
  EXPECT_FALSE(BQ25798::fromFile(&s, sizeof(s), o));    // damaged
  EXPECT_EQ(o.voc_pct, 81);
}

TEST(BQ25798, StatusShowsVocPctOnlyWhenNotDefault) {
  FakeBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  runFor(bq, t, 30000);
  char buf[160];
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "mppt o"), nullptr) << buf;
  EXPECT_EQ(strstr(buf, "%"), nullptr) << buf;
  BQ25798Settings s = settings();
  s.voc_pct = 75;
  bq.apply(s);
  runFor(bq, t, 30000);
  bq.formatStatus(buf, 155);
  EXPECT_TRUE(strstr(buf, "mppt on 75%") || strstr(buf, "mppt off 75%")) << buf;
  EXPECT_LT((int)strlen(buf), 155);
}

TEST(BQ25798, AutoVindpmUsesVocPct) {
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000, 63), 6200);   // 62.5 %
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000, 69), 6800);   // 68.75 %
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000, 75), 7500);
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000, 81), 8100);
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000, 88), 8700);   // 87.5 %
  EXPECT_EQ(BQ25798::vindpmFromVoc(10000), 8100);       // default
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  BQ25798Settings s = settings();
  s.voc_pct = 75;
  bq.apply(s);
  runFor(bq, t, 60000);
  EXPECT_EQ(f.r[0x05], 45);                             // 75 % of 6.0 V
  EXPECT_EQ(bq.autoVindpm(), 4500);
}

TEST(BQ25798, WeakPanelAtDawnGetsVindpmFromVocBeforeHizIsCleared) {
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  f.wlog.clear();
  runFor(bq, t, 60000);
  EXPECT_EQ(f.r[0x05], 48);                          // 81.25 % of 6.0 V (4875, rounded down)
  EXPECT_EQ(bq.autoVindpm(), 4800);
  EXPECT_EQ(bq.autoVindpmCount(), 1);
  EXPECT_TRUE(bq.powerGood());
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
  EXPECT_EQ(f.poor_events, 0);                       // the retry already started at the new floor
  int v = f.firstWrite(0x05);
  ASSERT_GE(v, 0);
  for (size_t i = 0; i < f.wlog.size(); i++) {      // no EN_HIZ clear before the VINDPM write
    if (f.wlog[i].first == 0x0F && !(f.wlog[i].second & 0x04)) { EXPECT_GT((int)i, v); break; }
  }
  EXPECT_EQ(f.r[0x15] & 1, 1);                       // PG: the existing path hands over to the chip's MPPT
  EXPECT_TRUE(bq.mpptOn());
  EXPECT_EQ(f.firstWrite(0x05, v + 1), -1);          // written once: MPPT is running now
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "mppt on"), nullptr) << buf;
  EXPECT_EQ(strstr(buf, ", vin"), nullptr) << buf;   // the input limit is shown only while MPPT is off
}

TEST(BQ25798, LatchedEnMpptWithHizStillGetsVindpmFromVoc) {
  // the chip clears EN_MPPT only when VBUS goes under VBUS_PRESENT: a poor-source HIZ above it leaves it set
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  f.r[0x15] = BQ_MPPT_BASE | 1;
  bq.apply(settings());
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, ", vin 3.6V"), nullptr) << buf;   // MPPT "on" but not running: the limit is shown
  EXPECT_NE(strstr(buf, ", hiz"), nullptr) << buf;
  f.wlog.clear();
  runFor(bq, t, 60000);
  EXPECT_EQ(f.r[0x05], 48);
  EXPECT_EQ(bq.autoVindpm(), 4800);
  EXPECT_TRUE(bq.powerGood());
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
  EXPECT_EQ(f.poor_events, 0);
  EXPECT_EQ(f.r[0x15] & 1, 1);                       // running again, as the chip has it
  int v = f.firstWrite(0x05);
  ASSERT_GE(v, 0);
  for (size_t i = 0; i < f.wlog.size(); i++) {
    if (f.wlog[i].first == 0x0F && !(f.wlog[i].second & 0x04)) { EXPECT_GT((int)i, v); break; }
  }
  // MPPT really running (EN_MPPT, PG, no HIZ): hands off
  f.r[0x05] = 60;
  runFor(bq, t, 30000);
  EXPECT_EQ(f.r[0x05], 60);
  EXPECT_EQ(bq.autoVindpmCount(), 1);
  // latched EN_MPPT, no HIZ but no PG: also recovered
  f.voc = 7000;
  f.r[0x1B] &= ~0x08;
  runFor(bq, t, 30000);
  EXPECT_EQ(f.r[0x05], 56);
}

TEST(BQ25798, VindpmIsSetAgainAtEachDropOut) {
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 60000);
  ASSERT_EQ(f.r[0x05], 48);
  // the light drops: Voc 4.5 V, the chip falls out again (poor source, EN_MPPT dropped with it)
  f.voc = 4500;
  f.r[0x1B] &= ~0x08;
  f.r[0x0F] |= 0x04;
  f.r[0x22] |= 0x10;
  f.r[0x15] &= ~0x01;
  f.r[0x05] = 0x24;                                  // the chip put its own 3.6 V back
  runFor(bq, t, 60000);
  EXPECT_EQ(f.r[0x05], 43);                          // 81 % of 4.5 V is 3.6 V: the 4.3 V floor
  EXPECT_EQ(bq.autoVindpm(), 4300);
  EXPECT_TRUE(bq.powerGood());
  f.voc = 7000;                                      // and again, brighter
  f.r[0x1B] &= ~0x08;
  f.r[0x0F] |= 0x04;
  f.r[0x15] &= ~0x01;
  runFor(bq, t, 60000);
  EXPECT_EQ(f.r[0x05], 56);
  EXPECT_GE(bq.autoVindpmCount(), 2);
}

TEST(BQ25798, LoadedVbusIsNeverTakenForVoc) {
  // PG = 1, no HIZ, but MPPT refused (battery under VSYSMIN): the converter loads the panel
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.sun(true);
  f.r[0x1B] |= 0x08;
  f.r[0x1E] |= 0x10;
  f.adcValues(3900, 0, 3500, 100);
  f.r[0x05] = 0x24;
  bq.apply(settings());
  f.wlog.clear();
  runFor(bq, t, 60000);
  EXPECT_EQ(f.firstWrite(0x05), -1);
  EXPECT_EQ(f.r[0x05], 0x24);
  EXPECT_EQ(bq.autoVindpmCount(), 0);

  // a reading taken while the converter ran (shot started after EN_HIZ was cleared) is not Voc either:
  // the chip qualifies, falls out again; the new Voc comes only from a conversion made in HIZ
  DawnBQ g;
  BQ25798 bq2(g);
  t = 0;
  bq2.begin(t);
  g.dawn();
  g.voc = 6000; g.loaded = 5000;                     // 81 % of 5.0 V would be 4.0 V -> the 4.3 V floor
  bq2.apply(settings());
  runFor(bq2, t, 60000);
  EXPECT_EQ(g.r[0x05], 48);                          // 81.25 % of 6.0 V, not of the loaded 5.0 V
}

TEST(BQ25798, LatchedPoorSourceFlagAloneIsNotUnloaded) {
  // PG = 1, no HIZ, MPPT refused (VSYSMIN): only the latched poor source flag is set. The converter runs, so
  // VINDPM stays and the flag is just counted.
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.sun(true);
  f.r[0x1B] |= 0x08;
  f.r[0x1E] |= 0x10;
  f.adcValues(3900, 0, 3500, 100);
  f.r[0x05] = 0x24;
  f.r[0x22] = 0x10;
  bq.apply(settings());
  f.wlog.clear();
  runFor(bq, t, 60000);
  EXPECT_EQ(f.firstWrite(0x05), -1);
  EXPECT_EQ(bq.autoVindpmCount(), 0);
  EXPECT_GE(bq.poorSourceCount(), 1);
}

TEST(BQ25798, FixedVindpmKeepsItsPath) {
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  BQ25798Settings s = settings();
  s.vindpm_mv = 4500;
  bq.apply(s);
  EXPECT_EQ(f.r[0x05], 45);
  EXPECT_EQ(f.r[0x15] & 1, 0);                       // no MPPT
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ_MPPT_BASE);
  f.wlog.clear();
  f.r[0x0F] |= 0x04; f.r[0x1B] &= ~0x08;
  bq.loop(t += 10000);
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);                    // EN_HIZ cleared at the poll: no hold, no waiting for Voc
  runFor(bq, t, 30000);
  EXPECT_EQ(f.r[0x05], 45);
  EXPECT_EQ(bq.autoVindpmCount(), 0);
  EXPECT_EQ(bq.autoVindpm(), 0);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, ", vin 4.5V"), nullptr) << buf;
}

TEST(BQ25798, HizIsHeldOnlyBrieflyWithoutAnAdc) {
  DawnBQ f;
  f.adc_works = false;                               // ADC_EN never drops: no result ever
  f.stubborn = true;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  EXPECT_NE(f.r[0x0F] & 0x04, 0);                    // held for the first result
  f.wlog.clear();
  bq.loop(t += 10000);                               // held again
  EXPECT_EQ(f.firstWrite(0x0F), -1);
  bq.loop(t += 10000);                               // given up: EN_HIZ cleared as before, 10 s retries
  EXPECT_GE(f.firstWrite(0x0F), 0);
  EXPECT_GE(f.poor_events, 1);
  for (int i = 0; i < 3; i++) bq.loop(t += 10000);
  EXPECT_EQ(bq.autoVindpmCount(), 0);
  f.wlog.clear();
  bq.loop(t += 10000);                               // a minute into the loop: held for another try
  EXPECT_EQ(f.firstWrite(0x0F), -1);
}

TEST(BQ25798, ContinuousAdcGivesVocToo) {
  DawnBQ f;
  f.zeros_after_shot = true;                         // one-shot results read as zeros: continuous conversion
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 180000);
  EXPECT_TRUE(bq.adcContinuous());
  EXPECT_EQ(f.r[0x05], 48);
  EXPECT_TRUE(bq.powerGood());
  EXPECT_LT(f.poor_events, 8);                       // a few retries while the one-shot gave zeros, then Voc
  EXPECT_EQ(f.r[0x2F] & 0x20, 0);                    // VBUS ADC channel on
}

TEST(BQ25798, VbusAdcChannelIsAlwaysOn) {
  EXPECT_EQ(BQ_ADC_DIS0_VAL & 0x20, 0);              // REG2F VBUS_ADC_DIS: day, night and TS variants
  EXPECT_EQ(BQ_ADC_DIS0_TS & 0x20, 0);
  EXPECT_EQ(BQ_ADC_DIS1_VAL & 0x0F, 0);
  for (int ntc = 0; ntc <= 1; ntc++) {
    DawnBQ f;
    BQ25798 bq(f);
    uint32_t t = 0;
    bq.begin(t);
    BQ25798Settings s = settings();
    s.ntc = ntc;
    bq.apply(s);
    f.dawn();
    for (int i = 0; i < 6; i++) { bq.loop(t += 10000); EXPECT_EQ(f.r[0x2F] & 0x20, 0); EXPECT_TRUE(f.r[0x2E] & 0x80 || i > 0); }
    f.sun(false);                                    // night
    for (int i = 0; i < 6; i++) { bq.loop(t += 10000); EXPECT_EQ(f.r[0x2F] & 0x20, 0); }
  }
}

TEST(BQ25798, PoorSourceIsCountedAndShown) {
  DawnBQ f;
  f.voc = 4000;                                      // 3.2 V: VINDPM at the 4.3 V floor, over Voc: the loop goes on
  f.stubborn = true;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  EXPECT_EQ(bq.poorSourceCount(), 1);                // apply() read REG22 (and cleared the flag)
  runFor(bq, t, 120000);
  EXPECT_GT(bq.poorSourceCount(), 1);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "mppt off"), nullptr) << buf;
  EXPECT_NE(strstr(buf, ", vin 4.3V"), nullptr) << buf;
  EXPECT_NE(strstr(buf, ", poor src x"), nullptr) << buf;
  EXPECT_NE(strstr(buf, ", hiz"), nullptr) << buf;
  EXPECT_NE(strstr(buf, ", no PG"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "0m"), nullptr) << buf;
  // it goes on being shown for a while after the last one, then not
  f.r[0x1B] |= 0x08;                                 // PG now
  f.r[0x0F] &= ~0x04;
  f.r[0x22] = 0;
  f.put16(0x35, 4000);
  f.voc = 20000;                                     // the next retry holds
  t += 10000;
  f.r[0x22] = 0;
  bq.loop(t);
  uint32_t last = t;
  while (t < last + 15 * 60000UL) { t += 10000; f.r[0x22] = 0; f.r[0x0F] &= ~0x04; f.r[0x1B] |= 0x08; bq.loop(t); }
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "poor src x"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "m"), nullptr);
  while (t < last + 45 * 60000UL) { t += 10000; f.r[0x22] = 0; f.r[0x0F] &= ~0x04; f.r[0x1B] |= 0x08; bq.loop(t); }
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_EQ(strstr(buf, "poor src"), nullptr) << buf;
  EXPECT_EQ(strstr(buf, "no PG"), nullptr) << buf;
}

TEST(BQ25798, StatusLineWithAllReasonsFitsTheCliReply) {
  // the CLI gives formatStatus() 155 bytes (reply[160], a 3-byte "xx|" tag)
  const int ROOM = 155;
  DawnBQ f;
  f.jumper = 0;
  f.stubborn = true;
  f.voc = 4000;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  BQ25798Settings st = settings(BQ_CHEM_LIFEPO4);
  st.ntc = 1;
  st.cutoff_mv = 3300;
  st.ichg_ma = 2000;
  bq.apply(st);
  f.dawn();
  f.put16(0x3F, 800);
  f.r[0x20] = 0x40;
  f.r[0x21] = 0xF4;
  // a power-on reset now and then, and more than 999 poor sources
  for (int i = 0; i < 12; i++) { f.por(); f.dawn(); f.r[0x20] = 0x40; f.r[0x21] = 0xF4; bq.loop(t += 10000); }
  for (int i = 0; i < 1100; i++) { f.dawn(); f.put16(0x3F, 800); f.r[0x20] = 0x40; f.r[0x21] = 0xF4; bq.loop(t += 10000); }
  ASSERT_GT(bq.poorSourceCount(), 999);
  f.dawn();
  f.adcValues(3300, -1500, 17950, 450);
  f.put16(0x3F, 800);
  f.r[0x20] = 0x40;
  f.r[0x21] = 0xF4;
  bq.loop(t += 10000);
  char buf[ROOM];
  int n = bq.formatStatus(buf, ROOM);
  EXPECT_LT(n, ROOM);
  EXPECT_EQ((int)strlen(buf), n);
  EXPECT_NE(strstr(buf, "poor src x999"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "no PG"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "JP2 1-2!"), nullptr) << buf;   // what must not be cut: the warnings
  EXPECT_NE(strstr(buf, "FAULT 40/F4"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "reset x"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "lifepo4 3.60V 2000mA"), nullptr) << buf;
  printf("longest status line: %d chars: %s\n", n, buf);
  // any smaller buffer: cut, always terminated, never over
  for (int len = 1; len <= ROOM; len++) {
    char b2[ROOM + 1];
    memset(b2, 'X', sizeof(b2));
    int m = bq.formatStatus(b2, len);
    ASSERT_LT(m, len) << len;
    ASSERT_EQ((int)strlen(b2), m) << len;
    ASSERT_EQ(b2[len < ROOM + 1 ? len : ROOM], 'X') << len;   // nothing written past len
  }
}

TEST(BQ25798, StatusLineFits) {
  FakeBQ f;
  f.jumper = 0;                         // the longest line: with the JP2 warning and the cell temperature
  BQ25798 bq(f);
  bq.begin(0);
  BQ25798Settings st = settings(BQ_CHEM_LIFEPO4);
  st.ntc = 1;
  bq.apply(st);
  f.put16(0x3F, 800);                   // -10 °C or so
  f.adcValues(3300, 1500, 17950, 450);
  f.sun(true);
  f.r[0x20] = 0x40;
  bq.loop(0);
  bq.loop(10000);
  char buf[160];
  int n = bq.formatStatus(buf, sizeof(buf));
  EXPECT_LT(n, 155);                    // the CLI reply room
  EXPECT_EQ((int)strlen(buf), n);
  EXPECT_NE(strstr(buf, "sun 17.95V 450mA"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "lifepo4 3.60V"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "cell "), nullptr) << buf;
}

TEST(BatteryGaugeChemistry, LiFePO4Curve) {
  gaugeChemistry() = 1;
  int last = -1;
  for (int mv = 2900; mv <= 3500; mv += 10) {
    int p = gaugeVoltagePercent(mv);
    EXPECT_GE(p, last);
    last = p;
  }
  EXPECT_EQ(gaugeVoltagePercent(3000), 0);
  EXPECT_EQ(gaugeVoltagePercent(3450), 100);
  EXPECT_EQ(gaugeFullMv(), GAUGE_LFP_FULL_MV);
  gaugeChemistry() = 0;
  EXPECT_EQ(gaugeVoltagePercent(4200), 100);
}

TEST(BatteryGaugeChemistry, MarkFullSetsTheCharge) {
  BatteryGauge g;
  g.setCapacity(2000);
  g.update(0, 3700, 100, 1);
  g.markFull();
  EXPECT_EQ(g.percent(), 100);
}

TEST(BatteryGaugeChemistry, ChargerFullIgnoresTheVoltageGuess) {
  BatteryGauge g;
  g.setCapacity(6800);
  g.setChargerFull(true);
  g.update(0, 3700, 0, 1);
  for (uint32_t t = 10000; t <= 600000; t += 10000) g.update(t, 4160, -250, 1);   // 4.16 V, 250 mA < C/20 going in
  EXPECT_LT(g.percent(), 100);
  g.markFull();
  EXPECT_EQ(g.percent(), 100);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---- MPPT scan ----

// A panel with a P-V curve: P = pmax * (1 - ((share - peak) / 0.45)^2) at VBUS = VINDPM; Voc with EN_HIZ.
struct PanelBQ : public FakeBQ {
  uint16_t voc = 10000;
  double peak = 0.75, pmax_mw = 3000, scale = 1.0;
  int vindpm_writes = 0, scale_after = -1;   // scale_after: the VINDPM write after which `scale` = scale2
  double scale2 = 0.5;
  void measure() {
    bool hiz = r[0x0F] & 0x04;
    uint16_t vb = hiz ? voc : (uint16_t)(r[0x05] * 100);
    double share = (double)vb / voc, d = (share - peak) / 0.45;
    double p = hiz ? 0 : pmax_mw * scale * (1 - d * d);
    if (p < 0) p = 0;
    put16(0x35, vb);
    put16(0x31, vb ? (uint16_t)(p * 1000 / vb) : 0);
    put16(0x33, 500);
    put16(0x3B, 3900);
    put16(0x3D, 3950);
    put16(0x41, 80);
  }
  bool zero_results = false;
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) override {
    if (reg <= 0x35 && reg + n > 0x31 && (r[0x2E] & 0xC0) != 0xC0) measure();   // not a pending one-shot: continuous / done
    if (reg == 0x2E && (r[0x2E] & 0xC0) == 0xC0) measure();
    bool ok = FakeBQ::readRegs(reg, buf, n);
    if (ok && zero_results) for (int i = 0; i < n; i++) if (reg + i >= 0x31 && reg + i <= 0x42) buf[i] = 0;
    return ok;
  }
  bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) override {
    bool ok = FakeBQ::writeRegs(reg, buf, n);
    for (uint8_t i = 0; ok && i < n; i++) {
      uint8_t a = reg + i;
      if (a == 0x05 && ++vindpm_writes == scale_after) scale = scale2;
      if (a == 0x0F) { if (buf[i] & 0x04) r[0x1B] &= ~0x08; else if (r[0x1B] & 0x01) r[0x1B] |= 0x08; }
    }
    return ok;
  }
  void fast() { sun(true); r[0x1C] = 0x60; }   // CHG_STAT 3
};

static void tickFor(BQ25798& bq, uint32_t& t, uint32_t ms) {
  for (uint32_t end = t + ms; t < end; ) { t += 100; if (bq.due(t)) bq.loop(t); }
}
static bool tickUntilIdle(BQ25798& bq, uint32_t& t, uint32_t max_ms) {
  for (uint32_t end = t + max_ms; t < end && bq.scanning(); ) { t += 100; if (bq.due(t)) bq.loop(t); }
  return !bq.scanning();
}
static BQ25798Settings autoSettings() { BQ25798Settings s = settings(); s.mppt_auto = 1; return s; }

TEST(BQ25798, ScanPicksThePeakOfThePvCurve) {
  const struct { double peak; uint8_t code; } cases[] = { {0.75, 75}, {0.65, 63}, {0.85, 88}, {0.81, 81}, {0.70, 69} };
  for (auto& c : cases) {
    PanelBQ f;
    f.peak = c.peak;
    f.fast();
    BQ25798 bq(f);
    uint32_t t = 0;
    bq.begin(t);
    bq.apply(autoSettings());
    tickFor(bq, t, 60000);
    EXPECT_FALSE(bq.scanning()) << c.peak;     // not before ~2 min of conditions
    EXPECT_TRUE(bq.mpptOn());
    tickFor(bq, t, 90000);
    ASSERT_TRUE(bq.scanning() || bq.scanHasResult()) << c.peak;
    uint32_t start = t;
    EXPECT_TRUE(tickUntilIdle(bq, t, 60000)) << c.peak;
    EXPECT_LT(t - start, 40000u);              // ~30 s
    ASSERT_TRUE(bq.scanHasResult()) << c.peak;
    EXPECT_EQ(bq.vocPct(), c.code) << c.peak;
    EXPECT_EQ(bq.scanChosen(), c.code);
    EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(c.code)) << c.peak;
    EXPECT_EQ(f.r[0x15] & 1, 1);               // EN_MPPT back on
    EXPECT_EQ(f.r[0x0F] & 0x04, 0);            // no EN_HIZ
    EXPECT_EQ(f.r[0x05], BQ25798::vindpmFromVoc(10000, c.code) / 100);   // VINDPM for the new share
    char buf[200];
    bq.formatStatus(buf, 155);
    char want[16];
    snprintf(want, sizeof(want), "mppt auto %u%%", c.code);
    EXPECT_NE(strstr(buf, want), nullptr) << buf;
    bq.formatScan(buf, 155);
    EXPECT_LT((int)strlen(buf), 155);
    EXPECT_NE(strstr(buf, "62:"), nullptr) << buf;
    EXPECT_NE(strstr(buf, "90:"), nullptr) << buf;
  }
}

TEST(BQ25798, ScanBestShareAndPowers) {
  PanelBQ f;
  f.peak = 0.75;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  EXPECT_EQ(bq.scanStart(), 0);
  EXPECT_EQ(bq.scanStart(), -3);                  // already running
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  ASSERT_TRUE(bq.scanHasResult());
  EXPECT_EQ(bq.vocPct(), 81);                     // manual mode: the share is not changed
  EXPECT_EQ(bq.scanShareX10(0), 625);
  EXPECT_EQ(bq.scanShareX10(11), 900);
  uint32_t best = 0;
  unsigned bi = 0;
  for (unsigned i = 0; i < BQ_SCAN_N; i++) if (bq.scanPowerMw(i) > best) { best = bq.scanPowerMw(i); bi = i; }
  EXPECT_EQ(bq.scanShareX10(bi), 750);
  EXPECT_GT(best, 2900u);
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "best 75.0%"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "81%"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "ago"), nullptr) << buf;
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(81));
  EXPECT_EQ(f.r[0x15] & 1, 1);
}

TEST(BQ25798, ScanUsesItsOwnFastTick) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  ASSERT_EQ(bq.scanStart(), 0);
  int polls = 0;
  uint32_t p0 = bq.polls();
  while (bq.scanning() && t < 100000) { t += 100; if (bq.due(t)) { bq.loop(t); polls++; } }
  EXPECT_GT(polls, 20);                           // well over one call per 10 s
  tickFor(bq, t, 12000);
  EXPECT_GT(bq.polls(), p0);                      // the 10 s poll resumes afterwards
}

TEST(BQ25798, ScanAbortsWhenPowerDrifts) {
  PanelBQ f;
  f.fast();
  f.scale_after = 13;                             // the re-measure of the first share: half the power
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 150000);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_FALSE(bq.scanHasResult());
  EXPECT_EQ(bq.vocPct(), 81);
  EXPECT_EQ(f.r[0x15] & 1, 1);                    // EN_MPPT back
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(81));
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
  EXPECT_EQ(f.r[0x05], BQ25798::vindpmFromVoc(10000, 81) / 100);   // not left at 62.5 %
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "clouds"), nullptr) << buf;
  // retried later, with a steady sky
  f.scale = 1.0;
  f.scale_after = -1;
  tickFor(bq, t, 5 * 60000);
  EXPECT_FALSE(bq.scanHasResult());               // not yet: 10 min
  tickFor(bq, t, 7 * 60000);
  EXPECT_TRUE(bq.scanHasResult());
  EXPECT_EQ(bq.vocPct(), 75);
}

TEST(BQ25798, ScanSmallDriftIsAccepted) {
  PanelBQ f;
  f.fast();
  f.scale_after = 13;
  f.scale2 = 0.9;                                 // 10 %
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 150000);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_TRUE(bq.scanHasResult());
}

TEST(BQ25798, ScanSkippedOutsideFastCharge) {
  for (uint8_t st : {0x00, 0x20, 0x40, 0x80, 0xE0}) {   // idle, trickle, pre, taper, done
    PanelBQ f;
    f.fast();
    f.r[0x1C] = st;
    BQ25798 bq(f);
    uint32_t t = 0;
    bq.begin(t);
    bq.apply(autoSettings());
    f.vindpm_writes = 0;
    tickFor(bq, t, 15 * 60000);
    EXPECT_FALSE(bq.scanning());
    EXPECT_FALSE(bq.scanHasResult());
    EXPECT_EQ(f.vindpm_writes, 0) << (int)st;
  }
  PanelBQ f;                                       // charge state 3 but no panel PG: skipped too
  f.fast();
  f.r[0x1B] &= ~0x08;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 10 * 60000);
  EXPECT_FALSE(bq.scanHasResult());
  EXPECT_EQ(bq.scanStart(), -2);
}

TEST(BQ25798, ForcedScanRunsInConstantVoltageAndNeedsMpptMode) {
  PanelBQ f;
  f.fast();
  f.r[0x1C] = 0x80;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  EXPECT_EQ(bq.scanStart(), 0);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_TRUE(bq.scanHasResult());
  BQ25798Settings s = settings();
  s.vindpm_mv = 5000;
  bq.apply(s);
  EXPECT_EQ(bq.scanStart(), -1);
}

TEST(BQ25798, ScanSchedule2MinThenHourlyThenNextSession) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 120000);
  EXPECT_EQ(bq.scanCount(), 0);                   // ~2 min after the conditions were first met
  tickFor(bq, t, 30000);
  tickUntilIdle(bq, t, 60000);
  EXPECT_EQ(bq.scanCount(), 1);
  tickFor(bq, t, 50 * 60000);
  EXPECT_EQ(bq.scanCount(), 1);                   // not within the hour
  tickFor(bq, t, 15 * 60000);
  tickUntilIdle(bq, t, 60000);
  EXPECT_EQ(bq.scanCount(), 2);
  // night, then the next day: 2 min after the conditions are met again, not an hour
  f.sun(false);
  tickFor(bq, t, 60000);
  f.fast();
  tickFor(bq, t, 4 * 60000);
  tickUntilIdle(bq, t, 60000);
  EXPECT_EQ(bq.scanCount(), 3);
}

TEST(BQ25798, ScanPutsEverythingBackWhenTheSunGoes) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 135000);
  ASSERT_TRUE(bq.scanning());
  tickFor(bq, t, 8000);                           // in the middle
  ASSERT_TRUE(bq.scanning());
  f.sun(false);
  tickFor(bq, t, 1000);
  EXPECT_FALSE(bq.scanning());
  EXPECT_FALSE(bq.scanHasResult());
  EXPECT_EQ(bq.vocPct(), 81);
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(81));
  EXPECT_NE(f.r[0x05], 0);
  EXPECT_GE(f.r[0x05], 36);                       // VINDPM not left at a probe value under 3.6 V
}

TEST(BQ25798, ScanAbortedByChargeStateLeavingFastCharge) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 135000);
  ASSERT_TRUE(bq.scanning());
  tickFor(bq, t, 6000);
  f.r[0x1C] = 0x80;                               // CV
  tickFor(bq, t, 2500);
  EXPECT_FALSE(bq.scanning());
  EXPECT_FALSE(bq.scanHasResult());
  EXPECT_EQ(f.r[0x15] & 1, 1);
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);
}

TEST(BQ25798, ScanAbortedByChipResetAndByNewSettings) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 135000);
  ASSERT_TRUE(bq.scanning());
  tickFor(bq, t, 3000);
  f.r[0x10] |= 0x05;                              // watchdog bits back: reset
  tickFor(bq, t, 2500);
  EXPECT_FALSE(bq.scanning());
  EXPECT_FALSE(bq.scanHasResult());
  tickFor(bq, t, 30000);                          // enforce() takes it from here
  EXPECT_EQ(f.r[0x10] & 0x07, 0);

  PanelBQ g;
  g.fast();
  BQ25798 b2(g);
  t = 0;
  b2.begin(t);
  b2.apply(settings());
  tickFor(b2, t, 30000);
  ASSERT_EQ(b2.scanStart(), 0);
  tickFor(b2, t, 4000);
  ASSERT_TRUE(b2.scanning());
  BQ25798Settings s = settings();
  s.voc_pct = 69;
  b2.apply(s);                                    // a CLI change in the middle
  EXPECT_FALSE(b2.scanning());
  EXPECT_EQ(g.r[0x0F] & 0x04, 0);
  EXPECT_EQ(g.r[0x15] & 0xFE, BQ25798::mpptBase(69));
  EXPECT_EQ(g.r[0x15] & 1, 1);
}

TEST(BQ25798, MpptAutoStartsAtDefaultShareAndAppliesAtOnce) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  BQ25798Settings s = settings();
  s.voc_pct = 63;
  bq.apply(s);
  tickFor(bq, t, 1000);
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(63));
  s.mppt_auto = 1;                                  // what `charger vin mppt auto` sets
  s.voc_pct = BQ_VOC_PCT_DEFAULT;
  bq.apply(s);
  EXPECT_EQ(bq.vocPct(), 81);
  EXPECT_TRUE(bq.mpptAuto());
  EXPECT_EQ(f.r[0x15] & 0xFE, BQ25798::mpptBase(81));   // REG15 at once
  // a boot with auto on and a scan result (or an earlier manual share) in the file
  BQ25798Settings saved = autoSettings(), o;
  saved.voc_pct = 88;
  ASSERT_TRUE(BQ25798::fromFile(&saved, sizeof(saved), o));
  EXPECT_EQ(o.voc_pct, 81);
}

TEST(BQ25798, ScanI2cErrorsLeaveEnHizAndMpptToTheNextPoll) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  ASSERT_EQ(bq.scanStart(), 0);
  tickFor(bq, t, 1000);                           // BEGIN done: EN_HIZ set, EN_MPPT off
  ASSERT_TRUE(bq.scanning());
  EXPECT_EQ(f.r[0x0F] & 0x04, 0x04);
  EXPECT_EQ(f.r[0x15] & 1, 0);
  f.fail = true;
  tickFor(bq, t, 600);
  EXPECT_FALSE(bq.scanning());
  f.fail = false;
  tickFor(bq, t, 40000);
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);                 // enforce(): EN_HIZ cleared
  EXPECT_EQ(f.r[0x15] & 1, 1);                    // EN_MPPT back
  EXPECT_FALSE(bq.scanHasResult());
}

TEST(BQ25798, ScanKeepsContinuousAdcAsItWas) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  f.zero_results = true;
  tickFor(bq, t, 40000);                          // a one-shot reads zeros: continuous
  f.zero_results = false;
  ASSERT_TRUE(bq.adcContinuous());
  ASSERT_EQ(bq.scanStart(), 0);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_TRUE(bq.scanHasResult());
  EXPECT_TRUE(bq.adcContinuous());
  EXPECT_EQ(f.r[0x2E] & 0xC0, 0x80);
}

TEST(BQ25798, ScanOneShotAdcModeIsLeftAsItWas) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  ASSERT_FALSE(bq.adcContinuous());
  ASSERT_EQ(bq.scanStart(), 0);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_FALSE(bq.adcContinuous());
  EXPECT_EQ(f.r[0x2E] & 0x80, 0);                 // no conversion left running
  tickFor(bq, t, 30000);
  EXPECT_FALSE(bq.adcContinuous());
  EXPECT_TRUE(bq.reading().valid);
}

TEST(BQ25798, AutoSettingSaveLoadAndOldFiles) {
  BQ25798Settings s = settings();
  EXPECT_EQ(s.mppt_auto, 0);
  s.mppt_auto = 1;
  s.voc_pct = 69;
  EXPECT_TRUE(BQ25798::valid(s));
  BQ25798Settings o;
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s), o));
  EXPECT_EQ(o.mppt_auto, 1);
  EXPECT_EQ(o.voc_pct, 81);                               // auto starts from the default, not the saved 69
  s.mppt_auto = 0;
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s), o));       // manual: the saved share stays
  EXPECT_EQ(o.voc_pct, 69);
  s.mppt_auto = 1;
  ASSERT_TRUE(BQ25798::fromFile(&s, 11, o));              // the file of b06ffbb: no mppt_auto yet
  EXPECT_EQ(o.mppt_auto, 0);                              // 11 bytes: unknown -> not auto
  ASSERT_TRUE(BQ25798::fromFile(&s, 10, o));              // before voc_pct
  EXPECT_EQ(o.mppt_auto, 0);
  EXPECT_EQ(o.voc_pct, 81);
  s.mppt_auto = 2;
  EXPECT_FALSE(BQ25798::valid(s));
  EXPECT_FALSE(BQ25798::fromFile(&s, sizeof(s), o));
  EXPECT_EQ(o.mppt_auto, 0);
  s.mppt_auto = 0;
  uint8_t old12[12];                                      // reserved = 0 as written before
  memcpy(old12, &s, sizeof(old12) < sizeof(s) ? sizeof(old12) : sizeof(s));
  ASSERT_TRUE(BQ25798::fromFile(old12, 12, o));
  EXPECT_EQ(o.mppt_auto, 0);
}

TEST(BQ25798, NearestVocPct) {
  EXPECT_EQ(BQ25798::nearestVocPct(625), 63);
  EXPECT_EQ(BQ25798::nearestVocPct(650), 63);
  EXPECT_EQ(BQ25798::nearestVocPct(675), 69);
  EXPECT_EQ(BQ25798::nearestVocPct(725), 75);
  EXPECT_EQ(BQ25798::nearestVocPct(775), 75);
  EXPECT_EQ(BQ25798::nearestVocPct(800), 81);
  EXPECT_EQ(BQ25798::nearestVocPct(850), 88);
  EXPECT_EQ(BQ25798::nearestVocPct(900), 88);
}

TEST(BQ25798, StatusAutoLineAndScanProgress) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  tickFor(bq, t, 30000);
  char buf[200];
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "mppt auto 81%"), nullptr) << buf;
  EXPECT_LT((int)strlen(buf), 155);
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "no scan yet"), nullptr) << buf;
  ASSERT_EQ(bq.scanStart(), 0);
  tickFor(bq, t, 6000);
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "scan "), nullptr) << buf;
  EXPECT_LT((int)strlen(buf), 155);
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "running"), nullptr) << buf;
}

// ---- review fixes: defaults, old files, low-voltage scans, `scan last` ----

TEST(BQ25798, DefaultsSetEveryFieldAndLoadPathWithGarbageIsValid) {
  BQ25798Settings s;
  memset(&s, 0xA5, sizeof(s));                          // garbage, padding included
  BQ25798::defaults(s);
  EXPECT_EQ(s.version, BQ_SETTINGS_VERSION);
  EXPECT_EQ(s.chem, BQ_CHEM_LIION);
  EXPECT_EQ(s.ichg_ma, 1000);
  EXPECT_EQ(s.vindpm_mv, 0);
  EXPECT_EQ(s.ntc, 0);
  EXPECT_EQ(s.shipfet, 1);
  EXPECT_EQ(s.cutoff_mv, BQ25798::defaultCutoff(BQ_CHEM_LIION));
  EXPECT_EQ(s.voc_pct, BQ_VOC_PCT_DEFAULT);
  EXPECT_EQ(s.mppt_auto, 0);
  EXPECT_TRUE(BQ25798::valid(s));
  // loadCharger(): no file (len -1), garbage on the stack -> defaults -> valid
  for (int fill : { 0x00, 0xA5, 0xFF }) {
    BQ25798Settings g;
    memset(&g, fill, sizeof(g));
    uint8_t raw[sizeof(g)];
    memset(raw, fill, sizeof(raw));
    if (!BQ25798::fromFile(raw, -1, g)) BQ25798::defaults(g);
    EXPECT_TRUE(BQ25798::valid(g)) << fill;
    EXPECT_EQ(g.mppt_auto, 0);
    // a damaged file of full length: also back to valid defaults
    memset(&g, fill, sizeof(g));
    if (!BQ25798::fromFile(raw, sizeof(raw), g)) BQ25798::defaults(g);
    EXPECT_TRUE(BQ25798::valid(g)) << fill;
  }
}

TEST(BQ25798, FileOf10_11_12BytesKeepsWhatItHolds) {
  BQ25798Settings s = settings();
  s.ichg_ma = 1500;
  s.voc_pct = 88;
  s.mppt_auto = 1;
  const int L10 = (int)offsetof(BQ25798Settings, voc_pct), L11 = (int)offsetof(BQ25798Settings, mppt_auto);
  ASSERT_EQ(L10, 10);
  ASSERT_EQ(L11, 11);
  ASSERT_EQ((int)offsetof(BQ25798Settings, cal), 12);
  BQ25798Settings o;
  ASSERT_TRUE(BQ25798::fromFile(&s, L10, o));
  EXPECT_EQ(o.voc_pct, 81);
  EXPECT_EQ(o.mppt_auto, 0);
  ASSERT_TRUE(BQ25798::fromFile(&s, L11, o));           // voc_pct there, no mppt_auto
  EXPECT_EQ(o.ichg_ma, 1500);
  EXPECT_EQ(o.voc_pct, 88);
  EXPECT_EQ(o.mppt_auto, 0);
  ASSERT_TRUE(BQ25798::fromFile(&s, 12, o));
  EXPECT_EQ(o.voc_pct, 81);                             // auto on: the default, not the saved 88
  EXPECT_EQ(o.mppt_auto, 1);
}

// every VINDPM write, to see what the panel was ever asked to hold
struct LowPanelBQ : public PanelBQ {
  int min_vindpm = 255;
  bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) override {
    for (uint8_t i = 0; i < n; i++) if (reg + i == 0x05 && buf[i] < min_vindpm) min_vindpm = buf[i];
    return PanelBQ::writeRegs(reg, buf, n);
  }
};

TEST(BQ25798, ScanSkipsStepsUnder3V9) {
  LowPanelBQ f;
  f.fast();
  f.voc = 5000;                                         // 62.5 % = 3.1 V ... 77.5 % = 3.8 V are skipped
  f.peak = 0.85;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());                                 // fixed share: only measures
  runFor(bq, t, 30000);
  f.min_vindpm = 255;
  ASSERT_EQ(bq.scanStart(), 0);
  ASSERT_TRUE(tickUntilIdle(bq, t, 90000));
  ASSERT_TRUE(bq.scanHasResult());
  EXPECT_GE(f.min_vindpm, 39);
  EXPECT_EQ(bq.scanPowerMw(0), 0u);
  EXPECT_EQ(bq.scanPowerMw(6), 0u);
  EXPECT_GT(bq.scanPowerMw(7), 0u);
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_EQ(strstr(buf, " 62:"), nullptr) << buf;
  EXPECT_NE(strstr(buf, " 80:"), nullptr) << buf;
  EXPECT_NE(strstr(buf, " 90:"), nullptr) << buf;
}

TEST(BQ25798, ScanWithTooFewStepsIsNotRun) {
  LowPanelBQ f;
  f.fast();
  f.voc = 4500;                                         // only 87.5 and 90 % reach 3.9 V
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(autoSettings());
  runFor(bq, t, 30000);
  f.min_vindpm = 255;
  ASSERT_EQ(bq.scanStart(), 0);
  ASSERT_TRUE(tickUntilIdle(bq, t, 90000));
  EXPECT_FALSE(bq.scanHasResult());
  EXPECT_EQ(f.r[0x05], BQ_VINDPM_FLOOR / 100);   // only the restore (81 % of 4.5 V is under the 4.3 V floor)
  EXPECT_EQ(f.r[0x0F] & 0x04, 0);                       // EN_HIZ back off
  EXPECT_EQ(f.r[0x15] & 1, 1);                          // EN_MPPT back
  EXPECT_EQ(bq.vocPct(), 81);
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "panel too low"), nullptr) << buf;
  // 4600 mV leaves 3 steps (85, 87.5, 90 %): scanned
  f.voc = 4600;
  runFor(bq, t, 30000);
  ASSERT_EQ(bq.scanStart(), 0);
  ASSERT_TRUE(tickUntilIdle(bq, t, 90000));
  EXPECT_TRUE(bq.scanHasResult());
  EXPECT_EQ(bq.scanPowerMw(8), 0u);
  EXPECT_GT(bq.scanPowerMw(9), 0u);
}

TEST(BQ25798, ScanLastKeepsTheTableAfterALaterFailure) {
  PanelBQ f;
  f.fast();
  f.pmax_mw = 800;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  runFor(bq, t, 30000);
  ASSERT_EQ(bq.scanStart(), 0);
  ASSERT_TRUE(tickUntilIdle(bq, t, 90000));
  ASSERT_TRUE(bq.scanHasResult());
  f.scale_after = f.vindpm_writes + 13;                 // clouds in the second one
  f.scale = 1.0;
  tickFor(bq, t, 12 * 60000);
  ASSERT_EQ(bq.scanStart(), 0);
  ASSERT_TRUE(tickUntilIdle(bq, t, 90000));
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_EQ(strncmp(buf, "scan ", 5), 0) << buf;
  EXPECT_NE(strstr(buf, " 62:"), nullptr) << buf;
  EXPECT_NE(strstr(buf, " 90:"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "; last try: clouds 0m"), nullptr) << buf;
  EXPECT_EQ(strstr(buf, "scan failed"), nullptr) << buf;
  EXPECT_LT((int)strlen(buf), 155);
  // not enough room: the note goes, the table stays whole
  int table_len = (int)(strstr(buf, "; last try") - buf);
  char small[200];
  int n = bq.formatScan(small, table_len + 6);
  EXPECT_EQ(strstr(small, "last try"), nullptr) << small;
  EXPECT_NE(strstr(small, " 90:"), nullptr) << small;
  EXPECT_EQ(n, (int)strlen(small));
}

// ---- IBAT calibration ----

// the driver with an ADC result: raw IBAT as given, one 10 s step each
static void adcStep(BQ25798& bq, FakeBQ& f, uint32_t& t, int16_t ibat) {
  f.adcValues(3950, ibat, 0, 0);
  bq.loop(t += 10000);
  bq.loop(t += 10000);
}

static IbatCurve noCurve() { IbatCurve c; memset(&c, 0, sizeof(c)); return c; }
static int man(IbatCurve& c, int raw, int ref) { IbatCurve d = noCurve(); return IbatCal::setManual(c, d, raw, ref); }
static int ap(const IbatCurve& c, int raw) { IbatCurve d = noCurve(); return IbatCal::apply(c, d, (int16_t)raw); }

TEST(IbatCal, OnePointIsAnOffsetEverywhere) {
  IbatCurve c = noCurve();
  EXPECT_EQ(ap(c, -24), -24);
  ASSERT_EQ(man(c, -24, -12), IBAT_SET_OK);
  EXPECT_EQ(ap(c, -24), -12);
  EXPECT_EQ(ap(c, -300), -288);   // outside: the line through the end point, gain 1
  EXPECT_EQ(ap(c, -2), 10);       // below the point: the same offset, the sign may flip
  EXPECT_EQ(ap(c, 30), 30);       // the charge direction is untouched
}

TEST(IbatCal, BucketChoice) {
  EXPECT_EQ(IbatCal::bucketOf(1), 0);
  EXPECT_EQ(IbatCal::bucketOf(7), 0);
  EXPECT_EQ(IbatCal::bucketOf(8), 1);
  EXPECT_EQ(IbatCal::bucketOf(14), 1);
  EXPECT_EQ(IbatCal::bucketOf(15), 2);
  EXPECT_EQ(IbatCal::bucketOf(60), 3);
  EXPECT_EQ(IbatCal::bucketOf(150), 5);
  EXPECT_EQ(IbatCal::bucketOf(1414), 7);
  EXPECT_EQ(IbatCal::bucketOf(1415), 8);
  EXPECT_EQ(IbatCal::bucketOf(3000), 8);
  EXPECT_EQ(IbatCal::bucketOf(3001), -1);
  EXPECT_EQ(IbatCal::bucketOf(0), -1);
  EXPECT_EQ(IbatCal::bucketOf(-1), 0);    // discharge: 3 5 8 12 20 35 60 120
  EXPECT_EQ(IbatCal::bucketOf(-3), 0);
  EXPECT_EQ(IbatCal::bucketOf(-4), 1);
  EXPECT_EQ(IbatCal::bucketOf(-6), 1);
  EXPECT_EQ(IbatCal::bucketOf(-7), 2);
  EXPECT_EQ(IbatCal::bucketOf(-12), 3);
  EXPECT_EQ(IbatCal::bucketOf(-26), 4);
  EXPECT_EQ(IbatCal::bucketOf(-27), 5);
  EXPECT_EQ(IbatCal::bucketOf(-84), 6);
  EXPECT_EQ(IbatCal::bucketOf(-85), 7);
  EXPECT_EQ(IbatCal::bucketOf(-3000), 7);
}

TEST(IbatCal, InterpolationBetweenPointsPerDirection) {
  IbatCurve c = noCurve();
  ASSERT_EQ(man(c, -24, -12), IBAT_SET_OK);     // dis bucket 12 mA
  ASSERT_EQ(man(c, -60, -48), IBAT_SET_OK);     // dis bucket 35 / 60?
  ASSERT_EQ(man(c, -230, -200), IBAT_SET_OK);   // dis bucket 120
  EXPECT_EQ(ap(c, -24), -12);
  EXPECT_EQ(ap(c, -60), -48);
  EXPECT_EQ(ap(c, -230), -200);
  EXPECT_EQ(ap(c, -42), -30);                   // half way between the first two: 12 + (48-12)/2 = 30
  EXPECT_EQ(ap(c, -145), -124);                 // half way between -60>-48 and -230>-200: 48 + 76 * 85/170
  EXPECT_EQ(ap(c, 40), 40);                     // charge: no points
  ASSERT_EQ(man(c, 50, 40), IBAT_SET_OK);
  ASSERT_EQ(man(c, 1040, 1000), IBAT_SET_OK);
  EXPECT_EQ(ap(c, 50), 40);
  EXPECT_EQ(ap(c, 545), 520);   // half way on the 50>40 .. 1040>1000 line
  EXPECT_EQ(ap(c, -42), -30);                   // discharge unchanged
}

TEST(IbatCal, ExtrapolationUsesTheFitGainAnchoredAtTheEnd) {
  IbatCurve c = noCurve();
  ASSERT_EQ(man(c, -24, -12), IBAT_SET_OK);     // low (under 60 mA)
  ASSERT_EQ(man(c, -230, -200), IBAT_SET_OK);   // high (>= 150): fit gain 188/206
  float g = 188.0f / 206.0f;
  EXPECT_EQ(ap(c, -400), (int)-lroundf(200 + 170 * g));
  EXPECT_EQ(ap(c, -10), (int)-lroundf(12 - 14 * g));
  IbatCurve d = noCurve();   // the build default (offset + gain) with no point at all
  EXPECT_EQ(IbatCal::apply(d, d, 100), 100 * BQ25798_IBAT_CHG_GAIN_X1000 / 1000 + BQ25798_IBAT_CHG_OFFSET_X10 / 10);
}

TEST(IbatCal, ManualErrors) {
  IbatCurve c = noCurve();
  EXPECT_EQ(man(c, -24, 0), IBAT_SET_ZERO);
  EXPECT_EQ(man(c, 0, -12), IBAT_SET_ZERO);
  EXPECT_EQ(man(c, 20, -12), IBAT_SET_DIRECTION);
  EXPECT_EQ(man(c, -24, -90000), IBAT_SET_ZERO);
  EXPECT_EQ(man(c, -5, -58), IBAT_SET_NONSENSE);      // 53 mA off
  EXPECT_EQ(man(c, -400, -160), IBAT_SET_NONSENSE);   // ratio 0.4
  EXPECT_EQ(man(c, 100, 160), IBAT_SET_NONSENSE);     // ratio 1.6
  EXPECT_TRUE(IbatCal::empty(c));
  EXPECT_EQ(man(c, -100, -100), IBAT_SET_OK);         // the old 60..149 zone is fine now
}

TEST(IbatCal, SanityLimitScalesWithCurrent) {
  IbatCurve c = noCurve();
  EXPECT_EQ(man(c, 1100, 1000), IBAT_SET_OK);    // 100 mA off, 15 % = 150 mA allowed
  EXPECT_EQ(man(c, 1150, 1000), IBAT_SET_OK);    // exactly 15 %
  EXPECT_EQ(man(c, 1160, 1000), IBAT_SET_NONSENSE);
  EXPECT_EQ(man(c, 2400, 2000), IBAT_SET_NONSENSE);   // 20 %
  EXPECT_EQ(man(c, 2300, 2000), IBAT_SET_OK);         // 15 %
  EXPECT_EQ(man(c, 3000, 2900), IBAT_SET_OK);
  EXPECT_EQ(man(c, 3000, 2600), IBAT_SET_NONSENSE);   // 400 mA > 15 % of 2600 (390)
  EXPECT_EQ(man(c, 3000, 2700), IBAT_SET_OK);         // 300 mA < 405
  EXPECT_EQ(man(c, 120, 60), IBAT_SET_NONSENSE);      // low currents: the 50 mA still rules
  EXPECT_EQ(man(c, 110, 60), IBAT_SET_OK);
  EXPECT_EQ(man(c, -400, -330), IBAT_SET_NONSENSE);   // 70 mA off, 15 % of 330 = 49.5
  EXPECT_EQ(man(c, -400, -350), IBAT_SET_OK);         // 50 mA
}

TEST(IbatCal, ManualOutOfOrderDoesNotDropAValidMiddlePoint) {
  // manual idx0 raw 10, auto idx1 raw 25, manual idx2 raw 8 (non-monotonic): only idx2 goes
  IbatCurve c = noCurve(), d = noCurve();
  c.p[IBAT_CHG][0] = { 100, 50, 1, IBAT_SRC_MANUAL };    // 10 mA raw, 5 mA true
  c.p[IBAT_CHG][1] = { 250, 100, 20, IBAT_SRC_AUTO };    // 25 mA raw, 10 mA true
  c.p[IBAT_CHG][2] = { 80, 200, 1, IBAT_SRC_MANUAL };    // 8 mA raw, 20 mA true: out of order
  IbatEff e = IbatCal::build(c, d, IBAT_CHG);
  EXPECT_EQ(e.m, 2);
  EXPECT_EQ(e.idx[0], 0);
  EXPECT_EQ(e.idx[1], 1);
  EXPECT_TRUE(e.ignored & (1u << 2));
  EXPECT_FALSE(e.ignored & (1u << 1));
  EXPECT_FALSE(e.ignored & (1u << 0));
}

TEST(IbatCal, DischargeTwoPointFitUsesAnInDirectionHighPoint) {
  IbatCurve c = noCurve(), d = noCurve();
  ASSERT_EQ(man(c, -14, -12), IBAT_SET_OK);
  ASSERT_EQ(man(c, -150, -120), IBAT_SET_OK);
  IbatEff e = IbatCal::build(c, d, IBAT_DIS);
  IbatFit f = IbatCal::fit(e, IBAT_DIS);
  EXPECT_EQ(f.points, 2);
  EXPECT_LT(f.gain, 0.9f);                  // gain found (was stuck at 1)
  EXPECT_EQ(ap(c, -200), -160);             // beyond the high point: its gain, not 1 (which would give -170)
  // charge still needs 150 mA for the high point
  IbatCurve g = noCurve();
  ASSERT_EQ(man(g, 6, 5), IBAT_SET_OK);
  ASSERT_EQ(man(g, 110, 100), IBAT_SET_OK);
  EXPECT_EQ(IbatCal::fit(IbatCal::build(g, d, IBAT_CHG), IBAT_CHG).points, 1);
}

TEST(IbatCal, ExportPagesOfAFullCurve) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  const int raw[9] = { 6, 12, 22, 55, 110, 220, 540, 1090, 2150 };
  const int tru[9] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000 };
  for (int i = 0; i < 9; i++) {
    adcStep(bq, f, t, (int16_t)raw[i]);
    ASSERT_EQ(bq.ibatCalManual(tru[i]), IBAT_SET_OK) << i;
  }
  char b1[200], b2[200];
  ASSERT_GT(bq.formatIbatExport(b1, 155, IBAT_CHG, 1), 0);
  ASSERT_GT(bq.formatIbatExport(b2, 155, IBAT_CHG, 2), 0);
  EXPECT_STREQ(b1, "-D BQ25798_IBAT_CHG_CURVE='\"6:5,12:10,22:20,55:50,110:100\"' ..2");
  EXPECT_STREQ(b2, "-D BQ25798_IBAT_CHG_CURVE2='\"220:200,540:500,1090:1000,2150:2000\"'");
  IbatCurve d = noCurve();   // the two fragments as default curves give the same curve back
  EXPECT_EQ(IbatCal::parseCurve("6:5,12:10,22:20,55:50,110:100", IBAT_CHG, d), 5);
  EXPECT_EQ(IbatCal::parseCurve("220:200,540:500,1090:1000,2150:2000", IBAT_CHG, d), 4);
  for (int i = 0; i < 9; i++) EXPECT_EQ(IbatCal::apply(noCurve(), d, (int16_t)raw[i]), tru[i]) << i;
  bq.formatIbatExport(b1, 155, IBAT_DIS, 1);
  EXPECT_NE(strstr(b1, "no points"), nullptr);
}

TEST(IbatCal, MonotonicRejection) {
  IbatCurve c = noCurve();
  ASSERT_EQ(man(c, 100, 90), IBAT_SET_OK);       // chg 100
  ASSERT_EQ(man(c, 80, 205), IBAT_SET_NONSENSE); // chg 200 with a raw below the 100 one (and ratio): not taken
  ASSERT_EQ(man(c, 210, 200), IBAT_SET_OK);
  EXPECT_EQ(man(c, 95, 500), IBAT_SET_NONSENSE);
  // a forged auto point out of order: ignored, the rest still works
  IbatCurve d = noCurve();
  d.p[IBAT_CHG][3] = { 500, 480, 20, IBAT_SRC_AUTO };    // true 48: raw 50
  d.p[IBAT_CHG][4] = { 400, 380, 20, IBAT_SRC_AUTO };    // true 100 but raw 40: out of order
  d.p[IBAT_CHG][5] = { 2100, 2000, 20, IBAT_SRC_AUTO };
  IbatEff e = IbatCal::build(d, noCurve(), IBAT_CHG);
  EXPECT_EQ(e.m, 2);
  EXPECT_TRUE(e.ignored & (1u << 4));
  // a manual point wins over a lower auto point it conflicts with: ref 72 mA is bucket 4 (true ~71..141), its raw
  // (49 mA, ratio 1.47) is below the raw of bucket 3 (50 mA): out of order, so the auto point of bucket 3 goes
  ASSERT_EQ(IbatCal::bucketOf(72), 4);
  IbatCurve m = d;
  ASSERT_EQ(IbatCal::setManual(m, noCurve(), 49, 72), IBAT_SET_OK);
  EXPECT_EQ(m.p[IBAT_CHG][4].src, IBAT_SRC_MANUAL);
  e = IbatCal::build(m, noCurve(), IBAT_CHG);
  EXPECT_TRUE(e.ignored & (1u << 3));           // the lower auto point is dropped
  EXPECT_FALSE(e.ignored & (1u << 4));          // the manual one stays
  EXPECT_EQ(e.m, 2);                            // manual 4 and auto 5
  EXPECT_EQ(e.idx[0], 4);
  // an AUTO point never displaces anything: the same pair as an auto point at bucket 4 is the one dropped
  IbatCurve a = d;
  a.p[IBAT_CHG][4] = { 490, 720, 20, IBAT_SRC_AUTO };
  e = IbatCal::build(a, noCurve(), IBAT_CHG);
  EXPECT_TRUE(e.ignored & (1u << 4));
  EXPECT_FALSE(e.ignored & (1u << 3));
}

TEST(IbatCal, ManualPointThatWouldPushOutAnotherManualOneIsRefused) {
  IbatCurve c = noCurve();
  ASSERT_EQ(man(c, 100, 98), IBAT_SET_OK);        // manual, bucket 4? true ~98 mA
  IbatCurve before = c;
  // a lower current (bucket 3, true 50) with a raw above 100: would knock the manual 100 out of the order
  EXPECT_EQ(man(c, 105, 60), IBAT_SET_NONSENSE);
  EXPECT_EQ(memcmp(&c, &before, sizeof(c)), 0);   // nothing changed
  ASSERT_EQ(man(c, 55, 50), IBAT_SET_OK);         // in order: fine
}

TEST(IbatLearner, FailedWriteStaysDirtyAndRetriesAfterThePeriod) {
  IbatCurve c = noCurve();
  IbatLearner l;
  for (int i = 0; i < 30; i++) l.feed(c, -24, -12);
  ASSERT_TRUE(l.dirty());
  uint32_t t = IBAT_SAVE_FIRST_MS;
  ASSERT_TRUE(l.saveDue(t));
  l.saveFailed(t);                                // the write failed
  EXPECT_TRUE(l.dirty());
  EXPECT_FALSE(l.saveDue(t + IBAT_SAVE_PERIOD_MS - 1));
  EXPECT_TRUE(l.saveDue(t + IBAT_SAVE_PERIOD_MS));
  l.synced(c, t + IBAT_SAVE_PERIOD_MS, true);     // a good write
  EXPECT_FALSE(l.dirty());
}

TEST(BQ25798, LegacyImportIsDirtyUntilSaved) {
  PanelBQ f;
  f.fast();
  BQ25798 bq(f);
  IbatLegacyCal L;
  memset(&L, 0, sizeof(L));
  bq.ibatCurveImportLegacy(L);
  EXPECT_FALSE(bq.ibatCalDirty());                // nothing to import: nothing to save
  L.bin[IBAT_CHG][0].raw_x10 = 240; L.bin[IBAT_CHG][0].ref_x10 = 200; L.bin[IBAT_CHG][0].n = 1;
  L.bin[IBAT_CHG][0].manual = 1;
  bq.ibatCurveImportLegacy(L);
  EXPECT_TRUE(bq.ibatCalDirty());                 // the owner writes /ibatcal at once and calls ibatCalSaved()
  bq.ibatCalSaveFailed(0);
  EXPECT_TRUE(bq.ibatCalDirty());
  bq.ibatCalSaved(0);
  EXPECT_FALSE(bq.ibatCalDirty());
}

TEST(IbatCal, StabilityFilterAveragingAndManualPriority) {
  IbatCurve c = noCurve();
  IbatLearner l;
  for (int i = 0; i < 30; i++) l.feed(c, -24, -12);
  EXPECT_EQ(c.p[IBAT_DIS][3].n, IBAT_N_CAP);   // capped
  EXPECT_EQ(c.p[IBAT_DIS][3].raw_x10, -240);
  EXPECT_EQ(c.p[IBAT_DIS][3].src, IBAT_SRC_AUTO);
  IbatCurve d = noCurve();
  IbatLearner m;
  for (int i = 0; i < 6; i++) m.feed(d, -24, -12);
  EXPECT_FALSE(m.feed(d, -60, -12));            // the raw jumps: not stable, not used
  EXPECT_EQ(d.p[IBAT_DIS][3].n, 5);
  IbatCurve g = noCurve();
  ASSERT_EQ(man(g, -24, -12), IBAT_SET_OK);
  IbatLearner k;
  for (int i = 0; i < 20; i++) k.feed(g, -26, -13);
  EXPECT_EQ(g.p[IBAT_DIS][3].raw_x10, -240);    // the manual point stays
  EXPECT_EQ(g.p[IBAT_DIS][3].n, 1);
  EXPECT_EQ(g.p[IBAT_DIS][3].src, IBAT_SRC_MANUAL);
  IbatCurve z = noCurve();
  IbatLearner q;
  for (int i = 0; i < 5; i++) { q.feed(z, 0, 5); q.feed(z, 5, -5); q.feed(z, 4000, 4000); }
  EXPECT_TRUE(IbatCal::empty(z));
}

TEST(IbatCal, LearningThresholdsAndSaveRateLimit) {
  IbatCurve c = noCurve();
  IbatLearner l;
  for (int i = 0; i < IBAT_AUTO_MIN_N; i++) l.feed(c, -24, -12);   // the first feed has no predecessor
  EXPECT_EQ(c.p[IBAT_DIS][3].n, IBAT_AUTO_MIN_N - 1);
  EXPECT_EQ(ap(c, -24), -24);                                       // not used yet
  EXPECT_FALSE(l.dirty());
  l.feed(c, -24, -12);
  EXPECT_EQ(ap(c, -24), -12);
  EXPECT_TRUE(l.dirty());
  EXPECT_FALSE(l.saveDue(IBAT_SAVE_FIRST_MS - 1));
  EXPECT_TRUE(l.saveDue(IBAT_SAVE_FIRST_MS));
  l.synced(c, IBAT_SAVE_FIRST_MS, true);
  EXPECT_FALSE(l.dirty());
  for (int i = 0; i < 10; i++) l.feed(c, -26, -12);                 // moved 0.2 mA per sample, mean drifts a little
  EXPECT_FALSE(l.dirty());
  for (int i = 0; i < 40; i++) l.feed(c, -30, -12);                 // (true - raw) moved by > 4 mA
  EXPECT_TRUE(l.dirty());
  EXPECT_FALSE(l.saveDue(IBAT_SAVE_FIRST_MS + 1000));
  EXPECT_TRUE(l.saveDue(IBAT_SAVE_FIRST_MS + IBAT_SAVE_PERIOD_MS));
  // high current: 6 samples are enough
  IbatCurve h = noCurve();
  IbatLearner n;
  for (int i = 0; i < IBAT_AUTO_MIN_N_HIGH + 1; i++) n.feed(h, 1050, 1000);
  EXPECT_EQ(ap(h, 1050), 1000);
  EXPECT_TRUE(n.dirty());
  IbatCurve lo = noCurve();
  IbatLearner n2;
  for (int i = 0; i < IBAT_AUTO_MIN_N_HIGH + 1; i++) n2.feed(lo, 25, 20);   // 20 mA needs the full 12
  EXPECT_EQ(ap(lo, 25), 25);
  // clear auto keeps the manual ones
  ASSERT_EQ(man(h, 105, 100), IBAT_SET_OK);
  IbatCal::clearAuto(h);
  EXPECT_EQ(h.p[IBAT_CHG][8].n, 0);
  EXPECT_EQ(h.p[IBAT_CHG][4].n, 1);
}

TEST(IbatCal, BuildDefaultCurveParsing) {
  IbatCurve c = noCurve();
  EXPECT_EQ(IbatCal::parseCurve("-24:-12,-60:-48 , -2.4:-1.2,junk,5:3,-230:-200,x:y,-24:", IBAT_DIS, c), 4);
  EXPECT_EQ(c.p[IBAT_DIS][3].raw_x10, -240);
  EXPECT_EQ(c.p[IBAT_DIS][3].ref_x10, -120);
  EXPECT_EQ(c.p[IBAT_DIS][3].src, IBAT_SRC_BUILD);
  EXPECT_EQ(c.p[IBAT_DIS][0].raw_x10, -24);     // -1.2 mA is the first point
  EXPECT_EQ(c.p[IBAT_DIS][7].ref_x10, -2000);
  EXPECT_EQ(c.p[IBAT_CHG][0].n, 0);             // the other sign is skipped
  IbatCurve e = noCurve();
  EXPECT_EQ(IbatCal::parseCurve("", IBAT_CHG, e), 0);
  EXPECT_EQ(IbatCal::parseCurve(0, IBAT_CHG, e), 0);
  EXPECT_EQ(IbatCal::parseCurve("100:90,2100:2000,99999:1", IBAT_CHG, e), 2);
  // as a default: used where nothing is stored, a stored point wins
  IbatCurve none = noCurve();
  EXPECT_EQ(IbatCal::apply(none, c, -24), -12);
  EXPECT_EQ(IbatCal::apply(none, c, -42), -30);
  IbatCurve st = noCurve();
  ASSERT_EQ(IbatCal::setManual(st, c, -24, -10), IBAT_SET_OK);
  EXPECT_EQ(IbatCal::apply(st, c, -24), -10);
  EXPECT_EQ(IbatCal::apply(st, c, -60), -48);
}

TEST(IbatCal, FileFormat) {
  IbatCurve c = noCurve();
  ASSERT_EQ(man(c, -24, -12), IBAT_SET_OK);
  ASSERT_EQ(man(c, 1040, 1000), IBAT_SET_OK);
  c.p[IBAT_CHG][1] = { 100, 95, 17, IBAT_SRC_AUTO };
  uint8_t b[IBAT_FILE_LEN + 4];
  ASSERT_EQ(IbatCal::toFile(c, b), IBAT_FILE_LEN);
  IbatCurve o;
  ASSERT_TRUE(IbatCal::fromFile(b, IBAT_FILE_LEN, o));
  EXPECT_EQ(memcmp(&o, &c, sizeof(c)), 0);
  EXPECT_EQ(ap(o, -24), -12);
  // missing / short / long / foreign / newer / damaged
  EXPECT_FALSE(IbatCal::fromFile(b, 0, o));
  EXPECT_TRUE(IbatCal::empty(o));
  EXPECT_FALSE(IbatCal::fromFile(b, IBAT_FILE_LEN - 1, o));
  EXPECT_FALSE(IbatCal::fromFile(b, IBAT_FILE_LEN + 1, o));
  uint8_t x[IBAT_FILE_LEN];
  memcpy(x, b, sizeof(x)); x[0] = 'X';
  EXPECT_FALSE(IbatCal::fromFile(x, IBAT_FILE_LEN, o));
  memcpy(x, b, sizeof(x)); x[3] = 2;
  EXPECT_FALSE(IbatCal::fromFile(x, IBAT_FILE_LEN, o));
  for (int i = 0; i < IBAT_FILE_LEN; i++) {   // any single flipped bit is caught
    memcpy(x, b, sizeof(x)); x[i] ^= 0x10;
    EXPECT_FALSE(IbatCal::fromFile(x, IBAT_FILE_LEN, o)) << i;
  }
  // a valid CRC over forged points: the points are dropped by sanitize
  IbatCurve f = noCurve();
  f.p[IBAT_DIS][0] = { 240, -120, 5, 0 };   // wrong sign for discharge
  f.p[IBAT_CHG][2] = { 100, 95, 99, 0 };    // n over the cap, and the wrong bucket
  f.p[IBAT_CHG][0] = { 100, 95, 5, 7 };     // unknown source
  IbatCal::toFile(f, x);
  ASSERT_TRUE(IbatCal::fromFile(x, IBAT_FILE_LEN, o));
  EXPECT_TRUE(IbatCal::empty(o));
}

TEST(IbatCal, LegacyFilesOfTheTwoPointBuildImport) {
  IbatLegacyCal L; memset(&L, 0, sizeof(L));
  L.bin[IBAT_DIS][0] = { -240, -120, 30, 0 };    // low, auto
  L.bin[IBAT_DIS][1] = { -2300, -2000, 1, 1 };   // high, manual
  L.bin[IBAT_CHG][0] = { 100, 900, 3, 0 };       // kept in the file, ignored at use (90 mA off)
  L.bin[IBAT_CHG][1] = { 5000, 4800, 99, 0 };    // n over the cap: dropped
  IbatCurve c = noCurve();
  IbatCal::importLegacy(c, L);
  EXPECT_EQ(c.p[IBAT_DIS][3].n, 0);   // 30 > cap: dropped
  EXPECT_EQ(c.p[IBAT_DIS][7].src, IBAT_SRC_MANUAL);
  EXPECT_EQ(c.p[IBAT_CHG][5].n, 0);
  EXPECT_EQ(IbatCal::bucketOf(-200), 7);
  L.bin[IBAT_DIS][0].n = 20;
  c = noCurve();
  IbatCal::importLegacy(c, L);
  EXPECT_EQ(c.p[IBAT_DIS][3].n, 20);
  EXPECT_EQ(ap(c, -24), -12);
}

TEST(BQ25798, IbatDriverAutoLearnsFromReference) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  for (int i = 0; i < 40; i++) {
    adcStep(bq, f, t, -24);                 // the ADC says 24 mA out
    if (bq.reading().valid) bq.setBatteryReference(-12, t);   // the INA219 says 12 mA out (+ = charging)
  }
  EXPECT_EQ(bq.reading().ibat_raw_ma, -24);
  EXPECT_EQ(bq.reading().ibat_ma, -12);     // corrected
  EXPECT_EQ(bq.batteryCurrentMa(), -12);
  EXPECT_TRUE(bq.ibatCalSaveDue(t));
  char buf[200];
  bq.formatIbat(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "dis auto 1p"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "chg default 0p"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "DIS_OFFSET_X10=120 DIS_GAIN_X1000=1000"), nullptr) << buf;
  EXPECT_LT(strlen(buf), 155u);
  bq.formatIbatCurve(buf, sizeof(buf), IBAT_DIS, 1);
  EXPECT_STREQ(buf, "dis 12:-24>-12a24");
  bq.formatIbatExport(buf, sizeof(buf), IBAT_DIS);
  EXPECT_STREQ(buf, "-D BQ25798_IBAT_DIS_CURVE='\"-24:-12\"'");
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "-12mA"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "(bq -24)"), nullptr) << buf;
  // the reference gone: the corrected ADC, still -12
  t += 100000;
  adcStep(bq, f, t, -24);
  EXPECT_FALSE(bq.hasBatteryReference());
  EXPECT_EQ(bq.batteryCurrentMa(), -12);
  bq.ibatCalSaved(t);
  EXPECT_FALSE(bq.ibatCalSaveDue(t + 1000));
  // the file carries it, and /charger does not
  uint8_t fb[IBAT_FILE_LEN];
  ASSERT_EQ(bq.ibatCurveToFile(fb), IBAT_FILE_LEN);
  BQ25798 b2(f);
  EXPECT_TRUE(b2.ibatCurveLoad(fb, IBAT_FILE_LEN));
  EXPECT_EQ(IbatCal::apply(b2.ibatCurve(), b2.ibatDefaultCurve(), -24), -12);
  EXPECT_EQ(bq.settings().cal.bin[IBAT_DIS][0].n, 0);
}

TEST(BQ25798, IbatManualCalAndClear) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  EXPECT_EQ(bq.ibatCalManual(-12), -1);     // no ADC result yet
  uint32_t t = 0;
  adcStep(bq, f, t, -24);
  ASSERT_TRUE(bq.reading().valid);
  EXPECT_EQ(bq.ibatCalManual(12), IBAT_SET_DIRECTION);
  EXPECT_EQ(bq.ibatCalManual(-12), IBAT_SET_OK);
  bq.setBatteryReference(-5, t + 7);   // a reference a few ms after the last loop() counts at once
  EXPECT_EQ(bq.batteryCurrentMa(), -5);
  bq.clearBatteryReference();
  EXPECT_EQ(bq.reading().ibat_ma, -12);
  EXPECT_EQ(bq.batteryCurrentMa(), -12);
  char buf[200];
  bq.formatIbat(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "DIS_OFFSET_X10=120"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "dis manual 1p"), nullptr) << buf;
  bq.formatIbatCurve(buf, sizeof(buf), IBAT_DIS, 1);
  EXPECT_STREQ(buf, "dis 12:-24>-12m");
  bq.formatIbatCurve(buf, sizeof(buf), IBAT_CHG, 1);
  EXPECT_STREQ(buf, "chg -");
  bq.ibatCalClear(true);                    // auto only: the manual point stays
  EXPECT_EQ(bq.reading().ibat_ma, -12);
  bq.ibatCalClear(false);
  EXPECT_EQ(bq.reading().ibat_ma, -24);
  EXPECT_TRUE(IbatCal::empty(bq.ibatCurve()));
}

TEST(BQ25798, IbatCurveFileMissingAndCorrupt) {
  FakeBQ f;
  BQ25798 bq(f);
  EXPECT_FALSE(bq.ibatCurveLoad(nullptr, 0));
  EXPECT_TRUE(IbatCal::empty(bq.ibatCurve()));
  uint8_t junk[IBAT_FILE_LEN];
  memset(junk, 0xA5, sizeof(junk));
  EXPECT_FALSE(bq.ibatCurveLoad(junk, sizeof(junk)));
  EXPECT_TRUE(IbatCal::empty(bq.ibatCurve()));
}

TEST(BQ25798, Ibat934d2daChargerFileImportsIntoTheCurve) {
  BQ25798Settings s = settings();
  s.ichg_ma = 1500;
  s.cal.bin[IBAT_DIS][0] = { -240, -120, 20, 0 };
  s.cal.bin[IBAT_DIS][1] = { -2300, -2000, 1, 1 };
  BQ25798Settings o;
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s), o));     // the file as written by that build: same size
  EXPECT_EQ(o.ichg_ma, 1500);
  FakeBQ f;
  BQ25798 bq(f);
  bq.apply(o);
  EXPECT_EQ(bq.settings().cal.bin[IBAT_DIS][0].n, 0);   // not kept in the settings
  EXPECT_FALSE(bq.ibatCurveLoad(nullptr, 0));           // no /ibatcal yet
  bq.ibatCurveImportLegacy(o.cal);
  EXPECT_EQ(IbatCal::apply(bq.ibatCurve(), bq.ibatDefaultCurve(), -24), -12);
  EXPECT_EQ(IbatCal::apply(bq.ibatCurve(), bq.ibatDefaultCurve(), -230), -200);
  EXPECT_TRUE(bq.ibatCalSaveDue(IBAT_SAVE_FIRST_MS));   // to be written to /ibatcal
  // shorter files: no calibration, the rest kept
  const int CAL = (int)offsetof(BQ25798Settings, cal);
  for (int len = CAL; len < CAL + (int)sizeof(IbatLegacyCal); len++) {
    ASSERT_TRUE(BQ25798::fromFile(&s, len, o));
    EXPECT_EQ(o.cal.bin[IBAT_DIS][0].n, 0) << len;
    EXPECT_EQ(o.ichg_ma, 1500);
  }
  ASSERT_TRUE(BQ25798::fromFile(&s, 10, o));
  ASSERT_TRUE(BQ25798::fromFile(&s, 12, o));
  EXPECT_EQ(o.cal.bin[IBAT_DIS][1].n, 0);
  // a damaged legacy block costs the points, not the settings
  BQ25798Settings bad = s;
  memset(&bad.cal, 0xFF, sizeof(bad.cal));
  ASSERT_TRUE(BQ25798::fromFile(&bad, sizeof(bad), o));
  IbatCurve c = noCurve();
  IbatCal::importLegacy(c, o.cal);
  EXPECT_TRUE(IbatCal::empty(c));
  EXPECT_EQ(o.ichg_ma, 1500);
}

// ---- INA219 position and the IBUS calibration ----

// one ADC round with the panel on: IBAT and IBUS raw
static void sunStep(BQ25798& bq, FakeBQ& f, uint32_t& t, int16_t ibat, int16_t ibus) {
  f.sun(true);
  f.adcValues(3950, ibat, 18000, ibus);
  bq.loop(t += 10000);
  bq.loop(t += 10000);
}

static BQ25798Settings sunSettings() { BQ25798Settings s = settings(); s.ina_pos = BQ_INA_SUN; return s; }

TEST(BQ25798, InaPositionDefaultsToBatAndIsPersistent) {
  BQ25798Settings d;
  BQ25798::defaults(d);
  EXPECT_EQ(d.ina_pos, BQ_INA_BAT);
  BQ25798Settings s = sunSettings(), o;
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s), o));
  EXPECT_EQ(o.ina_pos, BQ_INA_SUN);
  // the files before the position: bat
  ASSERT_TRUE(BQ25798::fromFile(&s, (int)offsetof(BQ25798Settings, ina_pos), o));
  EXPECT_EQ(o.ina_pos, BQ_INA_BAT);
  // anything else is a damaged file
  s.ina_pos = 2;
  EXPECT_FALSE(BQ25798::fromFile(&s, sizeof(s), o));
  FakeBQ f;
  BQ25798 bq(f);
  EXPECT_FALSE(bq.inaSun());
  bq.begin(0);
  bq.apply(sunSettings());
  EXPECT_TRUE(bq.inaSun());
  BQ25798Settings back = bq.settings();
  EXPECT_EQ(back.ina_pos, BQ_INA_SUN);
  char buf[200];
  bq.formatIna(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "ina sun"), nullptr) << buf;
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "ina sun"), nullptr) << buf;
  bq.apply(settings());
  EXPECT_FALSE(bq.inaSun());
  bq.formatIna(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "ina bat"), nullptr) << buf;
}

TEST(BQ25798, IbusLearnsOnlyWithTheInaOnSunAndIbatIsLeftAlone) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(sunSettings());
  uint32_t t = 0;
  for (int i = 0; i < 40; i++) {
    sunStep(bq, f, t, -24, 330);                         // IBUS says 330 mA, the INA219 on the panel line 300
    if (bq.reading().valid) {
      bq.setBatteryReference(-12, t);                    // a battery reference in sun mode: ignored
      bq.setSunReference(300, t);
    }
  }
  EXPECT_EQ(bq.reading().ibus_raw_ma, 330);
  EXPECT_EQ(bq.reading().ibus_ma, 300);
  EXPECT_TRUE(IbatCal::empty(bq.ibatCurve()));           // IBAT untouched
  EXPECT_FALSE(bq.ibatCalDirty());
  EXPECT_FALSE(bq.hasBatteryReference());
  EXPECT_TRUE(bq.hasSunReference());
  EXPECT_EQ(bq.reading().ibat_ma, -24);
  EXPECT_EQ(bq.batteryCurrentMa(), -24);                 // the BQ's own IBAT, no INA219 override
  EXPECT_TRUE(bq.ibusCalSaveDue(t));
  char buf[200];
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "sun 18.00V 300mA"), nullptr) << buf;
  bq.formatIbus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "bq 330>300 ina +300 | auto 1p"), nullptr) << buf;
  bq.formatIbat(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "paused: ina on sun"), nullptr) << buf;
  EXPECT_LT(strlen(buf), 155u);

  // on the battery it is the other way round
  FakeBQ g;
  BQ25798 b2(g);
  b2.begin(0);
  b2.apply(settings());
  t = 0;
  for (int i = 0; i < 40; i++) {
    sunStep(b2, g, t, -24, 330);
    if (b2.reading().valid) {
      b2.setSunReference(300, t);                        // ignored
      b2.setBatteryReference(-12, t);
    }
  }
  EXPECT_TRUE(IbatCal::empty(b2.ibusCurve()));
  EXPECT_FALSE(b2.ibusCalDirty());
  EXPECT_EQ(b2.reading().ibus_ma, 330);
  EXPECT_EQ(b2.reading().ibat_ma, -12);
  EXPECT_EQ(b2.batteryCurrentMa(), -12);
  b2.formatIbus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "paused: ina on bat"), nullptr) << buf;
  b2.formatStatus(buf, 155);
  EXPECT_EQ(strstr(buf, "ina sun"), nullptr) << buf;
}

TEST(BQ25798, MovingTheInaDropsTheOldReference) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  sunStep(bq, f, t, -24, 330);
  bq.setBatteryReference(-12, t);
  EXPECT_TRUE(bq.hasBatteryReference());
  bq.apply(sunSettings());
  EXPECT_FALSE(bq.hasBatteryReference());
  EXPECT_FALSE(bq.hasSunReference());
  EXPECT_EQ(bq.batteryCurrentMa(), bq.reading().ibat_ma);
}

TEST(BQ25798, IbusNegativePairsAndZeroAreNotLearntAndNotCorrected) {
  IbatCurve c = noCurve();
  IbatLearner l(true);
  for (int i = 0; i < 40; i++) { l.feed(c, -30, -20); l.feed(c, 30, -20); l.feed(c, -30, 20); }
  EXPECT_TRUE(IbatCal::empty(c));
  for (int i = 0; i < 40; i++) l.feed(c, 105, 100);
  EXPECT_EQ(c.p[IBAT_CHG][4].n, IBAT_N_CAP);
  EXPECT_EQ(c.p[IBAT_DIS][3].n, 0);
  IbatCurve d = noCurve();
  EXPECT_EQ(IbatCal::applyBus(c, d, 105), 100);
  EXPECT_EQ(IbatCal::applyBus(c, d, 0), 0);
  EXPECT_EQ(IbatCal::applyBus(c, d, -7), -7);
  EXPECT_EQ(IbatCal::applyBus(c, d, 1), 0);     // never negative: the line through (105, 100) would say -4
  IbatCurve none = noCurve();
  EXPECT_EQ(IbatCal::applyBus(none, d, 250), 250);   // no points, no build IBAT gain / offset on IBUS
}

TEST(BQ25798, IbusManualCalCurveExportRoundtripAndFile) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(sunSettings());
  EXPECT_EQ(bq.ibusCalManual(100), -1);                  // no ADC result yet
  uint32_t t = 0;
  sunStep(bq, f, t, 0, 55);
  ASSERT_TRUE(bq.reading().valid);
  EXPECT_EQ(bq.ibusCalManual(0), IBAT_SET_ZERO);
  EXPECT_EQ(bq.ibusCalManual(-50), IBAT_SET_ZERO);
  EXPECT_EQ(bq.ibusCalManual(50), IBAT_SET_OK);          // raw 55 is really 50
  sunStep(bq, f, t, 0, 1030);
  EXPECT_EQ(bq.ibusCalManual(1000), IBAT_SET_OK);
  sunStep(bq, f, t, 0, 300);
  EXPECT_EQ(bq.reading().ibus_ma, 289);   // 300 raw: between (55>50) and (1030>1000): 50 + 950 * 245 / 975
  EXPECT_EQ(IbatCal::applyBus(bq.ibusCurve(), bq.ibusDefaultCurve(), 55), 50);
  EXPECT_EQ(IbatCal::applyBus(bq.ibusCurve(), bq.ibusDefaultCurve(), 1030), 1000);
  char buf[200];
  bq.formatIbusCurve(buf, sizeof(buf), 1);
  EXPECT_STREQ(buf, "ibus 50:55>50m ..2") << buf;   // 1000 mA is point 7: page 2
  bq.formatIbusCurve(buf, sizeof(buf), 2);
  EXPECT_STREQ(buf, "ibus 1000:1030>1000m") << buf;
  bq.formatIbusExport(buf, sizeof(buf), 1);
  EXPECT_STREQ(buf, "-D BQ25798_IBUS_CURVE='\"55:50\"' ..2") << buf;
  char p2[200];
  ASSERT_GT(bq.formatIbusExport(p2, sizeof(p2), 2), 0);
  EXPECT_STREQ(p2, "-D BQ25798_IBUS_CURVE2='\"1030:1000\"'") << p2;
  // the exported values as the build default give the same correction
  IbatCurve def = noCurve();
  IbatCal::parseCurve("55:50", IBAT_CHG, def);
  IbatCal::parseCurve("1030:1000", IBAT_CHG, def);
  IbatCurve none = noCurve();
  for (int raw = 1; raw < 2100; raw += 37)
    EXPECT_EQ(IbatCal::applyBus(none, def, (int16_t)raw), IbatCal::applyBus(bq.ibusCurve(), bq.ibusDefaultCurve(), (int16_t)raw)) << raw;
  // the file: own magic, the IBAT file is not an IBUS one and the other way round
  uint8_t fb[IBAT_FILE_LEN];
  ASSERT_EQ(bq.ibusCurveToFile(fb), IBAT_FILE_LEN);
  EXPECT_EQ(memcmp(fb, "IBU", 3), 0);
  BQ25798 b2(f);
  EXPECT_TRUE(b2.ibusCurveLoad(fb, IBAT_FILE_LEN));
  EXPECT_EQ(memcmp(&b2.ibusCurve(), &bq.ibusCurve(), sizeof(IbatCurve)), 0);
  EXPECT_FALSE(b2.ibatCurveLoad(fb, IBAT_FILE_LEN));
  uint8_t ib[IBAT_FILE_LEN];
  bq.ibatCurveToFile(ib);
  EXPECT_FALSE(b2.ibusCurveLoad(ib, IBAT_FILE_LEN));
  EXPECT_TRUE(IbatCal::empty(b2.ibusCurve()));
  EXPECT_FALSE(b2.ibusCurveLoad(nullptr, 0));
  // a forged IBUS file with discharge points: they are dropped
  IbatCurve forged = noCurve();
  forged.p[IBAT_DIS][3] = { -100, -95, 5, IBAT_SRC_MANUAL };
  forged.p[IBAT_CHG][4] = { 1050, 1000, 1, IBAT_SRC_MANUAL };
  IbatCal::toFile(forged, fb, IBUS_FILE_MAGIC);
  ASSERT_TRUE(b2.ibusCurveLoad(fb, IBAT_FILE_LEN));
  EXPECT_EQ(b2.ibusCurve().p[IBAT_DIS][3].n, 0);
  EXPECT_EQ(b2.ibusCurve().p[IBAT_CHG][4].n, 1);
  // clear
  bq.ibusCalClear(true);                                 // auto only: the manual points stay
  EXPECT_EQ(bq.reading().ibus_ma, IbatCal::applyBus(bq.ibusCurve(), bq.ibusDefaultCurve(), bq.reading().ibus_raw_ma));
  bq.ibusCalClear(false);
  EXPECT_TRUE(IbatCal::empty(bq.ibusCurve()));
  EXPECT_EQ(bq.reading().ibus_ma, bq.reading().ibus_raw_ma);
  bq.formatIbusExport(buf, sizeof(buf), 1);
  EXPECT_STREQ(buf, "no points for ibus");
}

TEST(BQ25798, OldVersion1IbatcalFileStillLoads) {
  // /ibatcal as the build before the IBUS curve wrote it: "IBC", 1, 18 points of 6 bytes, CRC16
  uint8_t b[IBAT_FILE_LEN];
  memset(b, 0, sizeof(b));
  b[0] = 'I'; b[1] = 'B'; b[2] = 'C'; b[3] = 1;
  // discharge point 3 (12 mA): raw -240 (0.1 mA), true -120, 20 auto samples
  int o = 4 + (IBAT_NP + 3) * 6;
  b[o] = (uint8_t)(-240 & 0xFF); b[o + 1] = (uint8_t)((-240) >> 8);
  b[o + 2] = (uint8_t)(-120 & 0xFF); b[o + 3] = (uint8_t)((-120) >> 8);
  b[o + 4] = 20; b[o + 5] = IBAT_SRC_AUTO;
  uint16_t crc = IbatCal::crc16(b, IBAT_FILE_LEN - 2);
  b[IBAT_FILE_LEN - 2] = (uint8_t)(crc & 0xFF); b[IBAT_FILE_LEN - 1] = (uint8_t)(crc >> 8);
  FakeBQ f;
  BQ25798 bq(f);
  ASSERT_TRUE(bq.ibatCurveLoad(b, IBAT_FILE_LEN));
  EXPECT_EQ(IbatCal::apply(bq.ibatCurve(), bq.ibatDefaultCurve(), -24), -12);
  EXPECT_TRUE(IbatCal::empty(bq.ibusCurve()));
  EXPECT_FALSE(bq.ibusCurveLoad(b, IBAT_FILE_LEN));      // an IBAT file is no IBUS one
}

// the scan's powers (scanStart / formatScan) are VBUS x the corrected IBUS, like the status line
static uint32_t scanBestMw(bool calibrate, int* raw_out, int* cal_out) {
  PanelBQ f;
  f.peak = 0.75;
  f.fast();
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  bq.apply(settings());
  tickFor(bq, t, 30000);
  *raw_out = bq.reading().ibus_raw_ma;
  if (calibrate) EXPECT_EQ(bq.ibusCalManual(*raw_out * 112 / 100), IBAT_SET_OK);
  *cal_out = bq.reading().ibus_ma;
  EXPECT_EQ(bq.scanStart(), 0);
  EXPECT_TRUE(tickUntilIdle(bq, t, 60000));
  EXPECT_TRUE(bq.scanHasResult());
  uint32_t best = 0;
  for (unsigned i = 0; i < BQ_SCAN_N; i++) if (bq.scanPowerMw(i) > best) best = bq.scanPowerMw(i);
  char buf[200];
  bq.formatScan(buf, 155);
  EXPECT_NE(strstr(buf, "best 75.0%"), nullptr) << buf;
  return best;
}

TEST(BQ25798, ScanPowersUseTheCorrectedIbus) {
  int raw0, cal0, raw1, cal1;
  uint32_t plain = scanBestMw(false, &raw0, &cal0);
  uint32_t corrected = scanBestMw(true, &raw1, &cal1);
  ASSERT_GT(raw0, 0);
  EXPECT_EQ(raw0, raw1);
  EXPECT_EQ(cal0, raw0);                  // no calibration: the raw value (build default curve is empty here)
  EXPECT_GT(cal1, cal0);                  // the manual point lifted it
  EXPECT_GT(corrected, plain);            // and so did the scan's power (VBUS x corrected IBUS)
  // about the same ratio as the corrected reading (the curve is nearly a straight gain at these currents)
  EXPECT_NEAR((double)corrected / plain, (double)cal1 / cal0, 0.08);
}

TEST(BQ25798, ScanPowerAndStatusUseTheCorrectedIbus) {
  FakeBQ f;
  BQ25798 bq(f);
  bq.begin(0);
  bq.apply(settings());
  uint32_t t = 0;
  sunStep(bq, f, t, 0, 105);
  EXPECT_EQ(bq.ibusCalManual(100), IBAT_SET_OK);
  char buf[200];
  bq.formatStatus(buf, 155);
  EXPECT_NE(strstr(buf, "sun 18.00V 100mA"), nullptr) << buf;
}

TEST(BQ25798, InaPositionIsKnownWithoutTheChip) {
  FakeBQ f;
  f.r[0x48] = 0;   // no BQ25798 answers as PN 011b
  BQ25798 bq(f);
  EXPECT_FALSE(bq.begin(0));
  EXPECT_FALSE(bq.inaSun());
  bq.setInaPos(BQ_INA_SUN);                  // from /charger at boot, before anything is applied
  EXPECT_TRUE(bq.inaSun());
  EXPECT_FALSE(bq.applied());
  char buf[200];
  bq.formatIna(buf, 155);
  EXPECT_NE(strstr(buf, "ina sun"), nullptr) << buf;
  EXPECT_NE(strstr(buf, "no BQ25798 at 0x6B"), nullptr) << buf;
}

// ---- small panels, as a BQ25606 (floor 4.3 V, 2 s retries, hybrid weak / MPPT) ----

TEST(BQ25798, AutoVindpmHasA43VFloor) {
  DawnBQ f;
  f.voc = 4800;                                      // 81 % = 3.9 V -> 4.3 V
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 60000);
  EXPECT_EQ(bq.autoVindpm(), 4300);
  EXPECT_EQ(f.r[0x05], 43);
}

TEST(BQ25798, PollsEvery2sWhilePoorSourceKeepsHiz) {
  DawnBQ f;
  f.voc = 4000;
  f.stubborn = true;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  uint32_t p0 = bq.polls();
  for (int i = 0; i < 10; i++) bq.loop(t += 2000);   // 20 s
  EXPECT_GE(bq.polls() - p0, 8u);                    // not 2 (10 s)
  // the panel gone: back to 10 s
  f.sun(false);
  f.r[0x0F] &= ~0x04;
  bq.loop(t += 2000);
  p0 = bq.polls();
  for (int i = 0; i < 10; i++) bq.loop(t += 2000);
  EXPECT_LE(bq.polls() - p0, 2u);
}

TEST(BQ25798, HybridStartsWeakThenMpptThenFallsBack) {
  DawnBQ f;
  f.voc = 6000;
  BQ25798 bq(f);
  bq.setHybrid(true);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 30000);
  EXPECT_TRUE(bq.weak());
  EXPECT_EQ(f.r[0x05], 44);                          // fixed 4.4 V
  EXPECT_EQ(f.r[0x15] & 1, 0);                       // no MPPT, no Voc stops
  EXPECT_TRUE(bq.powerGood());
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, ", weak 4.4V"), nullptr) << buf;
  // weak sun: stays weak
  f.put16(0x31, 80);
  runFor(bq, t, 180000);
  EXPECT_TRUE(bq.weak());
  // strong sun for a minute: MPPT
  f.put16(0x31, 300);
  runFor(bq, t, 90000);
  EXPECT_FALSE(bq.weak());
  EXPECT_EQ(f.r[0x15] & 1, 1);
  // a poor source in MPPT: weak at once, MPPT not tried again for 10 min
  f.r[0x22] |= 0x10;
  runFor(bq, t, 20000);
  EXPECT_TRUE(bq.weak());
  EXPECT_EQ(bq.weakFalls(), 1);
  EXPECT_EQ(f.r[0x15] & 1, 0);
  EXPECT_EQ(f.r[0x05], 44);
  runFor(bq, t, 300000);
  EXPECT_TRUE(bq.weak());
  runFor(bq, t, 400000);
  EXPECT_FALSE(bq.weak());
  // MPPT with little current in fast charge for 5 min: weak again
  f.r[0x1C] = 3 << 5;
  f.put16(0x31, 50);
  runFor(bq, t, 330000);
  EXPECT_TRUE(bq.weak());
  EXPECT_EQ(bq.weakFalls(), 2);
  // evening, next morning: starts weak
  f.put16(0x31, 300);
  runFor(bq, t, 700000);
  EXPECT_FALSE(bq.weak());
  f.sun(false);
  runFor(bq, t, 20000);
  f.dawn();
  runFor(bq, t, 20000);
  EXPECT_TRUE(bq.weak());
}

TEST(BQ25798, HybridOffOrFixedVinLeavesTheModesAlone) {
  DawnBQ f;
  BQ25798 bq(f);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 60000);
  EXPECT_FALSE(bq.weak());                           // BQ_HYBRID_DEFAULT 0 in the tests
  EXPECT_EQ(f.r[0x05], 48);
  BQ25798Settings s = settings();
  s.vindpm_mv = 5000;
  bq.setHybrid(true);
  bq.apply(s);
  runFor(bq, t, 30000);
  EXPECT_FALSE(bq.weak());
  EXPECT_EQ(f.r[0x05], 50);
}

TEST(BQ25798, HybridLittleCurrentOutsideFastChargeIsNotWeakSun) {
  DawnBQ f;
  f.voc = 6000;
  BQ25798 bq(f);
  bq.setHybrid(true);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  f.put16(0x31, 300);
  f.r[0x1C] = 3 << 5;                                // fast charge
  runFor(bq, t, 120000);
  ASSERT_FALSE(bq.weak());
  runFor(bq, t, 60000);
  f.r[0x1C] = 7 << 5;                                // done: the battery, not the sun, limits IBUS
  f.put16(0x31, 15);
  runFor(bq, t, 600000);
  EXPECT_FALSE(bq.weak());
  EXPECT_EQ(bq.weakFalls(), 0);
}

TEST(BQ25798, HybridWeakIsNotShownAtNightAndBlocksAScan) {
  DawnBQ f;
  f.voc = 6000;
  BQ25798 bq(f);
  bq.setHybrid(true);
  uint32_t t = 0;
  bq.begin(t);
  f.dawn();
  bq.apply(settings());
  runFor(bq, t, 30000);
  ASSERT_TRUE(bq.weak());
  EXPECT_EQ(bq.scanStart(), -2);
  f.sun(false);
  runFor(bq, t, 20000);
  char buf[160];
  bq.formatStatus(buf, sizeof(buf));
  EXPECT_EQ(strstr(buf, "weak"), nullptr) << buf;
}
