#include <gtest/gtest.h>

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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
