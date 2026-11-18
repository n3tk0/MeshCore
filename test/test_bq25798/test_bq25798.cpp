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
