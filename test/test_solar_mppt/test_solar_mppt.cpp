#include <gtest/gtest.h>
#include <string.h>

#include <helpers/sensors/SolarMppt.h>

TEST(SolarMppt, SetpointMathMatchesTheBoard) {
  EXPECT_NEAR(SolarMppt::defaultSetpoint(), 5.18f, 0.01f);           // pin hi-Z
  EXPECT_NEAR(SolarMppt::setpointForDuty(0.0f), 7.075f, 0.01f);       // duty 0: the highest
  EXPECT_LT(SolarMppt::setpointForDuty(0.5f), SolarMppt::setpointForDuty(0.1f));
  // the duty that equals the divider alone: Vp = Vref
  EXPECT_NEAR(SolarMppt::setpointForDuty(1.205f / 3.3f), SolarMppt::defaultSetpoint(), 0.01f);
}

TEST(SolarMppt, DutyForSetpointRoundTripsAndClamps) {
  for (float v = 4.6f; v <= 6.9f; v += 0.1f) {
    EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::dutyForSetpoint(v)), v, 0.01f);
  }
  EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::dutyForSetpoint(9.0f)), SOLAR_MPPT_V_MAX, 0.01f);
  EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::dutyForSetpoint(1.0f)), SOLAR_MPPT_V_MIN, 0.01f);
  EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::dutyForSetpoint(NAN)), SOLAR_MPPT_V_MIN, 0.01f);
  EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::minDuty()), SOLAR_MPPT_V_MAX, 0.01f);
  EXPECT_NEAR(SolarMppt::setpointForDuty(SolarMppt::maxDuty()), SOLAR_MPPT_V_MIN, 0.01f);
}

TEST(SolarMppt, FixedAndOffModes) {
  SolarMppt m;
  m.setFixed(6.0f);
  EXPECT_TRUE(m.driven());
  EXPECT_EQ(m.state(), SolarMppt::ST_FIXED);
  EXPECT_NEAR(m.setpoint(), 6.0f, 0.02f);
  m.update(0, 6.0f, 0.5f, 3.9f);   // measuring does not move a fixed point
  EXPECT_NEAR(m.setpoint(), 6.0f, 0.02f);
  m.setFixed(99.0f);
  EXPECT_NEAR(m.fixedVolts(), SOLAR_MPPT_V_MAX, 0.001f);
  m.setOff();
  EXPECT_FALSE(m.driven());
  EXPECT_EQ(m.state(), SolarMppt::ST_OFF);
  EXPECT_NEAR(m.setpoint(), SolarMppt::defaultSetpoint(), 0.001f);
}

TEST(SolarMppt, FileRoundTrip) {
  SolarMppt a, b;
  uint8_t raw[SolarMppt::FILE_LEN];
  a.setFixed(5.5f);
  a.toFile(raw);
  EXPECT_TRUE(b.fromFile(raw, sizeof(raw)));
  EXPECT_EQ(b.mode(), SOLAR_MPPT_MODE_FIXED);
  EXPECT_NEAR(b.fixedVolts(), 5.5f, 0.001f);
  a.setOff();
  a.toFile(raw);
  EXPECT_TRUE(b.fromFile(raw, sizeof(raw)));
  EXPECT_EQ(b.mode(), SOLAR_MPPT_MODE_OFF);
  a.setAuto();
  a.toFile(raw);
  EXPECT_TRUE(b.fromFile(raw, sizeof(raw)));
  EXPECT_EQ(b.mode(), SOLAR_MPPT_MODE_AUTO);
  uint8_t bad[3] = {7, 0, 0};
  EXPECT_FALSE(b.fromFile(bad, 3));
  uint8_t bad_fixed[3] = {SOLAR_MPPT_MODE_FIXED, 0x10, 0x27};   // 10 V
  EXPECT_FALSE(b.fromFile(bad_fixed, 3));
  EXPECT_FALSE(b.fromFile(raw, 2));
}

// a panel whose power peaks at 5.8 V: P = 1.2 W - 0.8 * (V - 5.8)^2
static float panelPower(float v) { float d = v - 5.8f; return 1.2f - 0.8f * d * d; }

TEST(SolarMppt, AutoFindsThePeakAndStaysInRange) {
  SolarMppt m;
  uint32_t t = 0;
  for (int i = 0; i < 200; i++) {
    t += SOLAR_MPPT_STEP_MS;
    float v = m.setpoint();
    float p = panelPower(v);
    m.update(t, v, p / v, 3.8f);
    EXPECT_GE(m.setpoint(), SOLAR_MPPT_V_MIN - 0.02f);
    EXPECT_LE(m.setpoint(), SOLAR_MPPT_V_MAX + 0.02f);
  }
  EXPECT_TRUE(m.driven());
  EXPECT_NEAR(m.setpoint(), 5.8f, 0.25f);
}

TEST(SolarMppt, ReleasesWithoutSunAndComesBack) {
  SolarMppt m;
  uint32_t t = 0;
  for (int i = 0; i < 5; i++) { t += SOLAR_MPPT_STEP_MS; m.update(t, m.setpoint(), 0.2f, 3.8f); }
  EXPECT_TRUE(m.driven());
  t += SOLAR_MPPT_STEP_MS;
  m.update(t, 4.0f, 0.001f, 3.8f);   // dark
  EXPECT_FALSE(m.driven());
  EXPECT_EQ(m.state(), SolarMppt::ST_RELEASED);
  EXPECT_FALSE(m.due(t + SOLAR_MPPT_RETRY_MS - 1));
  EXPECT_TRUE(m.due(t + SOLAR_MPPT_RETRY_MS));
  t += SOLAR_MPPT_RETRY_MS;
  m.update(t, 5.2f, 0.005f, 3.8f);   // dawn, no real current yet
  EXPECT_FALSE(m.driven());
  t += SOLAR_MPPT_RETRY_MS;
  m.update(t, 5.2f, 0.2f, 3.8f);     // sun
  EXPECT_TRUE(m.driven());
}

TEST(SolarMppt, ReleasesWhenPanelIsUnderTheBatteryOrChargeIsDone) {
  SolarMppt m;
  uint32_t t = 0;
  m.update(t += 1, 5.5f, 0.2f, 3.8f);
  EXPECT_TRUE(m.driven());
  m.update(t += SOLAR_MPPT_STEP_MS, 3.85f, 0.2f, 3.8f);   // panel pulled under battery + margin
  EXPECT_FALSE(m.driven());
  m.update(t += SOLAR_MPPT_RETRY_MS, 5.5f, 0.2f, 3.8f);
  EXPECT_TRUE(m.driven());
  m.update(t += SOLAR_MPPT_STEP_MS, 5.5f, 0.004f, 3.8f);  // charger done: tiny current
  EXPECT_FALSE(m.driven());
}

TEST(SolarMppt, PanelUnavailableReleasesInAutoOnly) {
  SolarMppt m;
  m.update(1, 5.5f, 0.2f, 3.8f);
  EXPECT_TRUE(m.driven());
  m.panelUnavailable(2, false);
  EXPECT_FALSE(m.driven());
  m.setFixed(5.0f);
  m.panelUnavailable(3, true);
  EXPECT_TRUE(m.driven());
}

TEST(SolarMppt, DueSurvivesMillisWrap) {
  SolarMppt m;
  uint32_t t = 0xFFFFFFFFUL - 5000;
  m.update(t, 5.5f, 0.2f, 3.8f);
  EXPECT_FALSE(m.due(t + 100));
  EXPECT_TRUE(m.due(t + SOLAR_MPPT_STEP_MS));   // wrapped past zero
}

TEST(SolarMppt, FileKeepsTheLimitAndReadsTheOldFile) {
  SolarMppt a, b;
  uint8_t raw[SolarMppt::FILE_LEN];
  a.setIchg(304);
  EXPECT_EQ(a.ichg(), 300);
  a.setAuto();
  a.toFile(raw);
  EXPECT_TRUE(b.fromFile(raw, sizeof(raw)));
  EXPECT_EQ(b.ichg(), 300);
  EXPECT_TRUE(b.limitSet());
  uint8_t old[3] = {SOLAR_MPPT_MODE_FIXED, 0x7C, 0x15};   // 5.5 V, no limit in the file
  EXPECT_TRUE(b.fromFile(old, 3));
  EXPECT_EQ(b.ichg(), SOLAR_MPPT_ICHG_HW_MA);
  EXPECT_FALSE(b.limitSet());
  uint8_t bad[5] = {SOLAR_MPPT_MODE_AUTO, 0, 0, 50, 0};   // 50 mA: under the minimum
  EXPECT_FALSE(b.fromFile(bad, 5));
  EXPECT_EQ(b.ichg(), SOLAR_MPPT_ICHG_HW_MA);   // a refused file changes nothing
  a.setIchg(5000);
  EXPECT_EQ(a.ichg(), SOLAR_MPPT_ICHG_HW_MA);
}

// the battery takes what the panel gives at that point, at ~4 V (a lossless charger is close enough here)
static float battAmps(float v) { return panelPower(v) / 4.0f; }

TEST(SolarMppt, LimitHoldsTheBatteryCurrentByRaisingTheSetpoint) {
  SolarMppt m;
  m.setIchg(200);   // the peak gives 300 mA
  uint32_t t = 0;
  for (int i = 0; i < 300; i++) {
    t += SOLAR_MPPT_STEP_MS;
    float v = m.setpoint();
    float p = panelPower(v);
    m.update(t, v, p / v, 3.8f, m.wantsBatteryCurrent() ? battAmps(v) : NAN);
  }
  EXPECT_TRUE(m.driven());
  EXPECT_TRUE(m.limiting());
  EXPECT_GT(m.setpoint(), 5.8f);   // above the peak
  EXPECT_NEAR(battAmps(m.setpoint()), 0.200f, 0.035f);
  // the limit off: P&O goes back to the peak
  m.setIchg(SOLAR_MPPT_ICHG_HW_MA);
  for (int i = 0; i < 200; i++) {
    t += SOLAR_MPPT_STEP_MS;
    float v = m.setpoint();
    float p = panelPower(v);
    m.update(t, v, p / v, 3.8f, NAN);
  }
  EXPECT_FALSE(m.limiting());
  EXPECT_NEAR(m.setpoint(), 5.8f, 0.25f);
}

TEST(SolarMppt, LimitStuckAtTheTop) {
  SolarMppt m;
  m.setIchg(100);
  uint32_t t = 0;
  for (int i = 0; i < 100; i++) {
    t += SOLAR_MPPT_STEP_MS;
    float v = m.setpoint();
    m.update(t, v, 0.5f, 3.8f, 0.5f);   // the battery always takes 500 mA
  }
  EXPECT_TRUE(m.limitStuck());
  EXPECT_NEAR(m.setpoint(), SOLAR_MPPT_V_MAX, 0.02f);
}

TEST(SolarMppt, ScanFindsTheBestPointAndTracksFromThere) {
  SolarMppt m;
  uint32_t t = 0;
  char buf[160];
  EXPECT_FALSE(m.scanStart(t));   // not tracking yet
  m.formatScan(buf, sizeof(buf));
  EXPECT_STREQ(buf, "scan: none yet");
  m.update(t += 1, 5.2f, 0.2f, 3.8f);
  ASSERT_TRUE(m.driven());
  ASSERT_TRUE(m.scanStart(t));
  EXPECT_FALSE(m.scanStart(t));   // one at a time
  EXPECT_NEAR(m.setpoint(), SOLAR_MPPT_V_MAX, 0.02f);
  EXPECT_FALSE(m.due(t + SOLAR_MPPT_SCAN_MS - 1));
  int steps = 0;
  while (m.scanning() && steps < 50) {
    t += SOLAR_MPPT_SCAN_MS;
    EXPECT_TRUE(m.due(t));
    float v = m.setpoint();
    float d = v - 5.7f;
    float p = v > 6.6f ? 0.0f : 1.2f - 0.8f * d * d;   // peak at 5.7 V; over the open voltage nothing (no release in a scan)
    m.update(t, v, p / v, 3.8f);
    EXPECT_TRUE(m.driven());
    steps++;
  }
  EXPECT_EQ(steps, SOLAR_MPPT_SCAN_POINTS);
  EXPECT_NEAR(m.setpoint(), 5.7f, 0.02f);   // P&O starts from the best point
  int n = m.formatScan(buf, 155);
  EXPECT_LT(n, 155);
  EXPECT_EQ(strncmp(buf, "scan: best ", 11), 0);
  EXPECT_NE(strstr(buf, " 6.9:0.00"), (char*)NULL);
  EXPECT_FALSE(m.due(t + SOLAR_MPPT_STEP_MS - 1));
  m.setOff();
  EXPECT_FALSE(m.scanning());
}

TEST(SolarMppt, ScanEndsWhenThePanelIsLost) {
  SolarMppt m;
  m.update(1, 5.2f, 0.2f, 3.8f);
  ASSERT_TRUE(m.scanStart(1));
  m.panelUnavailable(2, false);
  EXPECT_FALSE(m.scanning());
  EXPECT_FALSE(m.driven());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(SolarMppt, FixedReleasesThePinWithoutSunAndDrivesAgain) {
  SolarMppt m;
  m.setFixed(5.5f);
  uint32_t t = 1000;
  m.update(t, 5.5f, 0.2f, 3.9f);                    // sun
  EXPECT_TRUE(m.driven());
  m.update(t += 60000, 3.9f, 0.0f, 3.9f);           // dark: panel under the battery
  EXPECT_FALSE(m.driven());
  EXPECT_EQ(m.state(), SolarMppt::ST_FIXED);
  EXPECT_NEAR(m.fixedVolts(), 5.5f, 0.001f);
  m.update(t += 60000, 4.0f, 0.001f, 3.9f);         // a little light, not enough to start
  EXPECT_FALSE(m.driven());
  m.update(t += 60000, 5.2f, 0.2f, 3.9f);           // sun again
  EXPECT_TRUE(m.driven());
  EXPECT_NEAR(m.setpoint(), 5.5f, 0.02f);
  m.update(t += 60000, 3.9f, 0.0f, 3.9f);
  EXPECT_FALSE(m.driven());
  m.panelUnavailable(t += 60000, false);            // no reading: driven as before
  EXPECT_TRUE(m.driven());
}
