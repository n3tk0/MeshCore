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
  const struct { uint8_t pct, base; } cases[] = { {63, 0x2A}, {69, 0x4A}, {75, 0x6A}, {81, 0x8A}, {88, 0xAA} };
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
    EXPECT_EQ(f.r[0x15] & 0x1F, 0x0A | (f.r[0x15] & 1)) << (int)c.pct;
    s.voc_pct = 75;                                       // changed later: written again
    bq.apply(s);
    runFor(bq, t, 30000);
    EXPECT_EQ(f.r[0x15] & 0xFE, 0x6A);
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
  EXPECT_EQ(f.r[0x05], 36);
  EXPECT_EQ(bq.autoVindpm(), 3600);
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
  g.voc = 5000; g.loaded = 4000;                     // 80 % of 4.0 V would be 3.2 V -> 3600
  bq2.apply(settings());
  runFor(bq2, t, 60000);
  EXPECT_EQ(g.r[0x05], 40);                          // 81.25 % of 5.0 V, not of the loaded 4.0 V
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
  f.voc = 4000;                                      // 3.2 V: VINDPM stays at the clamp, the loop goes on
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
  EXPECT_NE(strstr(buf, ", vin 3.6V"), nullptr) << buf;
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
  EXPECT_EQ(o.voc_pct, 69);
  ASSERT_TRUE(BQ25798::fromFile(&s, sizeof(s) - 1, o));   // the file of b06ffbb had 12 bytes with 0 at the end
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
