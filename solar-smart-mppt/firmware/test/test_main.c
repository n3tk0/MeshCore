// Host unit tests: settings, flash store (with power cuts), register map + I2C framing, gauge, BMS,
// BQ25798 driver against a fake chip.   make test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regs.h"
#include "settings.h"
#include "gauge.h"
#include "bms.h"
#include "heater.h"
#include "bq25798.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------- fake flash ----------------
static uint8_t flash[2 * STORE_SECTOR_SIZE];
static int prog_budget = -1;   // >= 0: number of 8-byte writes before a "power cut"
static bool f_erase(uint32_t a) { memset(flash + a, 0xFF, STORE_SECTOR_SIZE); return true; }
static bool f_prog(uint32_t a, const void *d, uint32_t n) {
  for (uint32_t i = 0; i < n; i += 8) {
    if (prog_budget == 0) return false;
    if (prog_budget > 0) prog_budget--;
    for (int k = 0; k < 8; k++) {
      CHECK(flash[a + i + k] == 0xFF);              // never program twice
      flash[a + i + k] &= ((const uint8_t *)d)[i + k];
    }
  }
  return true;
}
static const uint8_t *f_ptr(uint32_t a) { return flash + a; }
static const flash_ops_t FOPS = { f_erase, f_prog, f_ptr, 0 };

static void test_settings(void) {
  for (uint8_t c = 0; c < CHEM_COUNT; c++) {
    settings_t s;
    settings_defaults(&s, c);
    CHECK(settings_check(&s, c) == ERR_OK);
  }
  settings_t s;
  settings_defaults(&s, CHEM_LFP);
  s.vchg_mv = 4200;
  CHECK(settings_check(&s, CHEM_LFP) == ERR_RANGE);    // LiFePO4 never gets 4.2 V
  settings_defaults(&s, CHEM_LIION);
  s.expert_n = 1; s.expert[0] = 0x0100 | 0xFF;         // VREG high byte: refused
  CHECK(settings_check(&s, CHEM_LIION) == ERR_RANGE);
  s.expert[0] = 0x0AC3;                                // cell count: refused
  CHECK(settings_check(&s, CHEM_LIION) == ERR_RANGE);
  s.expert[0] = 0x1500 | 0x09;                         // MPPT timing bits: allowed
  CHECK(settings_check(&s, CHEM_LIION) == ERR_OK);

  // jumper moved from Li-ion to LiFePO4: voltages follow the jumper, currents and capacity stay
  settings_defaults(&s, CHEM_LIION);
  s.capacity_mah = 3000; s.ichg_ma = 700;
  uint8_t act;
  CHECK(settings_resolve_chem(&s, CHEM_LFP, &act));
  CHECK(act == CHEM_LFP && s.vchg_mv == 3600 && s.capacity_mah == 3000 && s.ichg_ma == 700);
  CHECK(!settings_resolve_chem(&s, CHEM_LFP, &act));
}

static void test_store(void) {
  memset(flash, 0xFF, sizeof(flash));
  store_init(&FOPS);
  settings_t s, r;
  settings_defaults(&s, CHEM_LIION);
  CHECK(!store_load(REC_SETTINGS, &r, sizeof(r)));
  s.capacity_mah = 1234;
  CHECK(store_save(REC_SETTINGS, &s, sizeof(s)));
  gauge_state_t g = { 5000, 2000, 7 }, gr;
  // many gauge saves: the sectors roll over several times, the settings record must survive
  for (int i = 0; i < 500; i++) { g.charge_mas = i; CHECK(store_save(REC_GAUGE, &g, sizeof(g))); }
  store_init(&FOPS);
  CHECK(store_load(REC_SETTINGS, &r, sizeof(r)) && r.capacity_mah == 1234);
  CHECK(store_load(REC_GAUGE, &gr, sizeof(gr)) && gr.charge_mas == 499);

  // power cut in every possible place of a save: the old or the new record, never neither
  for (int cut = 0; cut < 40; cut++) {
    memset(flash, 0xFF, sizeof(flash));
    store_init(&FOPS);
    s.capacity_mah = 1; store_save(REC_SETTINGS, &s, sizeof(s));
    for (int i = 0; i < 60; i++) { g.charge_mas = i; store_save(REC_GAUGE, &g, sizeof(g)); }
    s.capacity_mah = 2;
    prog_budget = cut;
    for (int i = 0; i < 20; i++) { g.charge_mas = 100 + i; store_save(REC_GAUGE, &g, sizeof(g)); }
    store_save(REC_SETTINGS, &s, sizeof(s));
    prog_budget = -1;
    store_init(&FOPS);
    CHECK(store_load(REC_SETTINGS, &r, sizeof(r)) && (r.capacity_mah == 1 || r.capacity_mah == 2));
    CHECK(store_load(REC_GAUGE, &gr, sizeof(gr)));
    // and it keeps working after the cut
    s.capacity_mah = 3;
    CHECK(store_save(REC_SETTINGS, &s, sizeof(s)));
    store_init(&FOPS);
    CHECK(store_load(REC_SETTINGS, &r, sizeof(r)) && r.capacity_mah == 3);
  }
}

// ---------------- registers + framing ----------------
static uint16_t rd(uint8_t addr, uint8_t reg) {
  i2cproto_start(addr, false); i2cproto_rx(reg); i2cproto_end(0);
  i2cproto_start(addr, true);
  uint16_t v = (uint16_t)(i2cproto_tx() << 8);
  v |= i2cproto_tx();
  i2cproto_tx();                  // prefetched byte the host never takes
  i2cproto_end(1);
  return v;
}
static void wr(uint8_t addr, uint8_t reg, uint16_t v) {
  i2cproto_start(addr, false); i2cproto_rx(reg); i2cproto_rx((uint8_t)(v >> 8)); i2cproto_rx((uint8_t)v); i2cproto_end(0);
}

static void test_regs(void) {
  memset(&R, 0, sizeof(R));
  settings_defaults(&R.set, CHEM_LIION);
  R.live.chem = CHEM_LIION; R.live.chem_jumper = CHEM_LIION;
  R.live.vpack_mv = 3700; R.live.ibat_ma = -500;
  R.ina_config = INA_CONFIG_DEFAULT;
  R.now_ms = 1000;

  CHECK(rd(ADDR_SMPPT, R_ID) == 0x534D);
  CHECK(rd(ADDR_INA260, INA_MFR) == 0x5449);
  CHECK(rd(ADDR_INA260, INA_VOLTAGE) == 2960);           // 3700 / 1.25
  CHECK(rd(ADDR_INA260, INA_CURRENT) == 400);            // discharge 500 mA, + = discharge
  CHECK(rd(ADDR_INA260, INA_POWER) == 185);              // 1.85 W / 10 mW

  // auto-increment on 0x42, with a prefetched byte the host never took
  i2cproto_start(ADDR_SMPPT, false); i2cproto_rx(R_VPACK); i2cproto_end(0);
  i2cproto_start(ADDR_SMPPT, true);
  uint16_t v1 = (uint16_t)(i2cproto_tx() << 8); v1 |= i2cproto_tx();
  uint16_t v2 = (uint16_t)(i2cproto_tx() << 8); v2 |= i2cproto_tx();
  i2cproto_tx();
  i2cproto_end(1);
  CHECK(v1 == 3700 && v2 == 0);
  i2cproto_start(ADDR_SMPPT, true);                      // next read continues at R_VCELL2
  i2cproto_tx(); i2cproto_tx(); i2cproto_end(0);
  i2cproto_start(ADDR_SMPPT, true);
  CHECK((uint16_t)(i2cproto_tx() << 8 | i2cproto_tx()) == (uint16_t)R.live.ibat_ma);
  i2cproto_end(0);

  // settings
  wr(ADDR_SMPPT, R_ICHG, 800);
  CHECK(R.result == ERR_OK && R.set.ichg_ma == 800);
  wr(ADDR_SMPPT, R_VCHG, 4400);
  CHECK(R.result == ERR_RANGE && R.set.vchg_mv == 4200);
  wr(ADDR_SMPPT, R_ID, 1);
  CHECK(R.result == ERR_BAD_REG);
  // chemistry change brings that chemistry's voltages
  wr(ADDR_SMPPT, R_CHEM_SET, CHEM_LFP);
  CHECK(R.result == ERR_OK && R.set.chem == CHEM_LFP && R.set.vchg_mv == 3600 && R.set.ichg_ma == 800);
  wr(ADDR_SMPPT, R_CHEM_SET, 7);
  CHECK(R.result == ERR_RANGE);
  // the next write is checked against the new chemistry at once (R.live is a second late)
  wr(ADDR_SMPPT, R_ICHG, 600);
  CHECK(R.result == ERR_OK && R.set.ichg_ma == 600);
  wr(ADDR_SMPPT, R_CHEM_SET, CHEM_BY_JUMPER);             // back to the jumper (Li-ion): its voltages
  CHECK(R.result == ERR_OK && R.set.vchg_mv == 4200);

  // protected command: locked, then unlocked once
  wr(ADDR_SMPPT, R_CMD, CMD_SAVE);
  CHECK(R.result == ERR_LOCKED && R.pending_cmd == CMD_NONE);
  wr(ADDR_SMPPT, R_UNLOCK, UNLOCK_KEY);
  CHECK(rd(ADDR_SMPPT, R_UNLOCK) == 1);
  wr(ADDR_SMPPT, R_CMD, CMD_SAVE);
  CHECK(R.result == 0xFFFF && R.pending_cmd == CMD_SAVE);
  wr(ADDR_SMPPT, R_CMD, CMD_LOAD);
  CHECK(R.result == ERR_BUSY);
  R.pending_cmd = CMD_NONE;
  wr(ADDR_SMPPT, R_CMD, CMD_SAVE);
  CHECK(R.result == ERR_LOCKED);                         // one command per unlock
  wr(ADDR_SMPPT, R_UNLOCK, UNLOCK_KEY);
  R.now_ms += 2500;                                      // too late
  wr(ADDR_SMPPT, R_CMD, CMD_SHIP);
  CHECK(R.result == ERR_LOCKED);
  wr(ADDR_SMPPT, R_CMD, CMD_SET_FULL);                   // not protected
  CHECK(R.pending_cmd == CMD_SET_FULL);
  R.pending_cmd = CMD_NONE;
  wr(ADDR_SMPPT, R_CMD, 77);
  CHECK(R.result == ERR_UNKNOWN_CMD);
  // no unlock after the 32-bit millisecond counter passes 2^31 (24.8 days)
  R.now_ms = 0x80000010u;
  wr(ADDR_SMPPT, R_CMD, CMD_SHIP);
  CHECK(R.result == ERR_LOCKED && rd(ADDR_SMPPT, R_UNLOCK) == 0);
  R.now_ms = 0xFFFFFFF0u;                                // unlock across the wrap
  wr(ADDR_SMPPT, R_UNLOCK, UNLOCK_KEY);
  R.now_ms = 0x00000100u;
  wr(ADDR_SMPPT, R_CMD, CMD_REBOOT);
  CHECK(R.pending_cmd == CMD_REBOOT);
  R.pending_cmd = CMD_NONE;

  // INA260 config reset bit, and the pointer does not move on 0x41
  wr(ADDR_INA260, INA_CONFIG, 0x8000);
  CHECK(R.ina_config == INA_CONFIG_DEFAULT);
  i2cproto_start(ADDR_INA260, false); i2cproto_rx(INA_MFR); i2cproto_end(0);
  i2cproto_start(ADDR_INA260, true);
  uint16_t a = (uint16_t)(i2cproto_tx() << 8 | i2cproto_tx());
  uint16_t b = (uint16_t)(i2cproto_tx() << 8 | i2cproto_tx());
  i2cproto_end(0);
  CHECK(a == 0x5449 && b == 0x5449);
}

static void test_gauge(void) {
  settings_t s;
  settings_defaults(&s, CHEM_LIION);
  gauge_t g;
  gauge_init(&g, 0);
  bool fv;
  gauge_soc(&g, &s, CHEM_LIION, 3700, &fv);
  CHECK(fv);                                     // capacity unknown: from voltage
  CHECK(soc_from_voltage(CHEM_LIION, 4200) == 10000 && soc_from_voltage(CHEM_LIION, 2900) == 0);
  CHECK(soc_from_voltage(CHEM_LTO2S, 5000) == 8000);

  // learn: full, then 2000 mAh out, then empty
  gauge_full(&g, &s);
  for (int i = 0; i < 7200; i++) gauge_step(&g, &s, -1000, 1000);
  gauge_empty(&g);
  CHECK(g.st.capacity_learnt_mah == 2000);
  CHECK(g.st.cycles_x10 == 0);                   // no capacity while learning: counted from now on
  gauge_full(&g, &s);
  uint16_t soc = gauge_soc(&g, &s, CHEM_LIION, 4200, &fv);
  CHECK(!fv && soc == 10000 && gauge_remaining_mah(&g) == 2000);
  for (int i = 0; i < 3600; i++) gauge_step(&g, &s, -1000, 1000);
  soc = gauge_soc(&g, &s, CHEM_LIION, 3700, &fv);
  CHECK(soc == 5000 && gauge_remaining_mah(&g) == 1000);
  CHECK(g.st.cycles_x10 == 5);
  // small currents are not lost
  int32_t before = g.st.charge_mas;
  for (int i = 0; i < 1000; i++) gauge_step(&g, &s, 3, 10);   // 3 mA for 10 s in 10 ms steps
  CHECK(g.st.charge_mas == before + 30);
  // the setting wins over the learnt value
  s.capacity_mah = 4000;
  CHECK(gauge_capacity(&g, &s) == 4000);
  gauge_reset(&g);
  gauge_soc(&g, &s, CHEM_LIION, 3700, &fv);
  CHECK(fv);
}

static void test_bms(void) {
  settings_t s;
  settings_defaults(&s, CHEM_LIION);
  bms_t b;
  bms_init(&b);
  bms_out_t o;
  bms_in_t in = { .vpack_mv = 3800, .ibat_ma = -100, .sun = true, .second = true };
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(!o.od_off && !o.oc_off && o.charge_ok && o.j7_on && !o.ship);

  // under-voltage with sun: FET off after the debounce, back on above od_on
  in.vpack_mv = 2950;
  for (int i = 0; i < BMS_DEBOUNCE - 1; i++) { bms_step(&b, &s, CHEM_LIION, &in, &o); CHECK(!o.od_off); }
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off && o.empty && !o.ship && !o.j7_on && (o.status & ST_UV));
  in.vpack_mv = 3200;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off);
  in.vpack_mv = 3350;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(!o.od_off);

  // under-voltage without sun: ship mode
  bms_init(&b);
  in.vpack_mv = 2950; in.sun = false;
  // the FET must stay on (it feeds the MCU) and ship comes in the same step the under-voltage does
  for (int i = 0; i < BMS_DEBOUNCE - 1; i++) { bms_step(&b, &s, CHEM_LIION, &in, &o); CHECK(!o.ship && !o.od_off); }
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.ship && !o.od_off && !o.j7_on && (o.status & ST_UV));
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.ship && !o.od_off && !o.j7_on);
  // the sun comes back while still low: FET off, no ship
  in.sun = true;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off && !o.ship);
  in.sun = false;

  // a COMP0 step between seconds counts nothing
  bms_init(&b);
  in.vpack_mv = 2950; in.sun = true; in.second = false;
  for (int i = 0; i < 2 * BMS_DEBOUNCE; i++) bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(!o.od_off);
  in.second = true;

  // short with sun: FET off, retry after the pause; without sun: ship at once
  bms_init(&b);
  in.vpack_mv = 3800; in.sun = true; in.short_tripped = true;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off && (o.status & ST_SHORT) && !o.ship);
  in.short_tripped = false; in.second = false;
  bms_step(&b, &s, CHEM_LIION, &in, &o);   // extra COMP0-driven pass: the pause does not move
  in.second = true;
  for (int i = 0; i < BMS_RETRY_S - 1; i++) bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off);
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(!o.od_off);
  bms_init(&b);
  in.sun = false; in.short_tripped = true;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.ship);

  // over-voltage: charge FET off, no charging
  bms_init(&b);
  in.short_tripped = false; in.sun = true; in.vpack_mv = 4300;
  for (int i = 0; i < BMS_DEBOUNCE; i++) bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.oc_off && !o.charge_ok);
  in.vpack_mv = 4050;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(!o.oc_off && o.charge_ok);

  // LTO 2S balancing: the higher cell bleeds, stops at half the delta
  settings_defaults(&s, CHEM_LTO2S);
  bms_init(&b);
  in.vpack_mv = 5300; in.vcell1_mv = 2680; in.vcell2_mv = 2620;
  bms_step(&b, &s, CHEM_LTO2S, &in, &o);
  CHECK(o.bal1 && !o.bal2);
  in.vcell1_mv = 2660; in.vcell2_mv = 2640;
  bms_step(&b, &s, CHEM_LTO2S, &in, &o);
  CHECK(o.bal1);
  in.vcell1_mv = 2655; in.vcell2_mv = 2645;
  bms_step(&b, &s, CHEM_LTO2S, &in, &o);
  CHECK(!o.bal1 && !o.bal2);
  // one LTO cell too low trips the pack even when the pack voltage is fine
  in.vpack_mv = 4000; in.vcell1_mv = 1700; in.vcell2_mv = 2300;
  for (int i = 0; i < BMS_DEBOUNCE; i++) bms_step(&b, &s, CHEM_LTO2S, &in, &o);
  CHECK(o.od_off);

  CHECK(bms_led(ST_SHORT, 0) == LED_RED && bms_led(ST_SHORT, 500) == LED_OFF);
  CHECK(bms_led(ST_BALANCING, 10) == LED_YELLOW && bms_led(0, 10) == LED_OFF);
}

// ---------------- fake BQ25798 ----------------
static uint8_t chip[BQ_NREGS];
static int chip_writes;
static bool chip_fail;
static bool c_read(uint8_t reg, uint8_t *buf, uint8_t n) {
  if (chip_fail) return false;
  memcpy(buf, chip + reg, n);
  return true;
}
static bool c_write(uint8_t reg, const uint8_t *buf, uint8_t n) {
  if (chip_fail) return false;
  memcpy(chip + reg, buf, n);
  chip_writes++;
  return true;
}
static const bq_bus_t CBUS = { c_read, c_write };

static void test_bq(void) {
  memset(chip, 0, sizeof(chip));
  chip[BQ_REG_TS] = 0x03; chip[BQ_REG_TS + 1] = 0x6C;   // 876/1024 = 85.5 %: no NTC
  bq_t b;
  bq_init(&b, &CBUS);
  settings_t s;
  settings_defaults(&s, CHEM_LIION);
  s.expert_n = 2;
  s.expert[0] = 0x1500 | 0xFF;      // tries to change VOC_PCT + EN_MPPT too: ours must win
  s.expert[1] = 0x0F00 | 0x22;      // EN_CHG + EN_TERM
  CHECK(bq_refresh(&b));
  CHECK(bq_apply(&b, &s, CHEM_LIION, false));
  CHECK(bq_u16(&b, BQ_REG_VREG) == 420 && chip[BQ_REG_VREG] == 0x01 && chip[BQ_REG_VREG + 1] == 0xA4);
  CHECK((chip[BQ_REG_ICHG] << 8 | chip[BQ_REG_ICHG + 1]) == 100);
  CHECK((chip[BQ_REG_IINDPM] << 8 | chip[BQ_REG_IINDPM + 1]) == 300);
  CHECK(chip[BQ_REG_MPPT] == (uint8_t)((4 << 5) | 0x1E));       // expert timing bits, our VOC_PCT; no EN_MPPT without PG
  CHECK((chip[BQ_REG_NTC1] & 1) == 1);                            // no NTC: TS ignored
  CHECK(chip[BQ_REG_CTRL0] == 0x02);                              // not charging yet, EN_TERM kept
  CHECK((chip[BQ_REG_CTRL1] & 7) == 5 && (chip[BQ_REG_ADC_CTRL] & 0x80));
  CHECK((chip[BQ_REG_VSYSMIN] & 0x3F) == 2);                      // 3000 mV
  CHECK((chip[BQ_REG_CTRL5] & 0x80) == 0x80);                     // SFET_PRESENT (Li-ion): ship mode works
  CHECK((chip[BQ_REG_ADC_DIS0] & 0x04) == 0x04);                  // no VBUS: TS ADC off
  // nothing to do the second time
  chip_writes = 0;
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, false) && chip_writes == 0);
  CHECK(bq_apply(&b, &s, CHEM_LIION, true) && chip[BQ_REG_CTRL0] == 0x22);
  // chip reset (watchdog): everything is written again
  memset(chip, 0, 0x30);
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true) && bq_u16(&b, BQ_REG_VREG) == 420);
  // EN_HIZ read back as 1 (caught during a VOC check or a fault) is never written back
  chip[BQ_REG_CTRL0] |= 0x04;
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true) && chip[BQ_REG_CTRL0] == 0x22);
  // VBUS: TS ADC on; AUTO_INDET_EN and EN_ICO always cleared
  chip[BQ_REG_STAT0] = 0x01;
  chip[BQ_REG_CTRL2] |= 0x40;
  chip[BQ_REG_CTRL0] |= 0x10;
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true));
  CHECK((chip[BQ_REG_ADC_DIS0] & 0x04) == 0 && (chip[BQ_REG_CTRL2] & 0x40) == 0 && chip[BQ_REG_CTRL0] == 0x22);
  // NTC present (judged once TS converts)
  chip[BQ_REG_TS] = 0x02; chip[BQ_REG_TS + 1] = 0x60;             // 59 %
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true) && (chip[BQ_REG_NTC1] & 1) == 0 && b.ntc_present);
  // night: TS off again, and a TS of 0 is not taken as an NTC either way
  chip[BQ_REG_STAT0] = 0;
  chip[BQ_REG_TS] = 0; chip[BQ_REG_TS + 1] = 0;
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true) && (chip[BQ_REG_ADC_DIS0] & 0x04) && b.ntc_present);
  chip[BQ_REG_TS] = 0x02; chip[BQ_REG_TS + 1] = 0x60;
  // bus error
  chip_fail = true;
  CHECK(!bq_refresh(&b) && !bq_apply(&b, &s, CHEM_LIION, true) && b.errors == 1);
  chip_fail = false;
  CHECK(bq_ship(&b) && (chip[BQ_REG_CTRL2] & 7) == 5);
  // still powered after the ship command: bq_apply must not undo it
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, false) && (chip[BQ_REG_CTRL2] & 7) == 5);
  b.ship_sent = false;                                            // the sun is back
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, false) && (chip[BQ_REG_CTRL2] & 6) == 0);
  // LiFePO4: no SFET_PRESENT (the BQ would stop at 3.2 V on battery), so no ship command either
  settings_t lfp;
  settings_defaults(&lfp, CHEM_LFP);
  CHECK(bq_refresh(&b) && bq_apply(&b, &lfp, CHEM_LFP, false) && (chip[BQ_REG_CTRL5] & 0x80) == 0);
  CHECK(bq_refresh(&b) && !bq_ship(&b) && !b.ship_sent && (chip[BQ_REG_CTRL2] & 6) == 0);
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, false) && (chip[BQ_REG_CTRL5] & 0x80));
  // after a chip reset the ADC is off and TS reads 0: no NTC must not turn into "NTC present"
  memset(chip, 0, sizeof(chip));
  bq_t c;
  bq_init(&c, &CBUS);
  CHECK(bq_refresh(&c) && bq_apply(&c, &s, CHEM_LIION, false) && !c.ntc_present && (chip[BQ_REG_NTC1] & 1) == 1);
  chip[BQ_REG_TS] = 0x03; chip[BQ_REG_TS + 1] = 0x6C;             // ADC running now: still no NTC
  CHECK(bq_refresh(&c) && bq_apply(&c, &s, CHEM_LIION, false) && !c.ntc_present);
}

// ---------------- weak panel at dawn: VINDPM from Voc ----------------
// The converter (VINDPM under 70 % of Voc) pulls the panel under VPOORSRC: EN_HIZ, PG = 0, poor source flag.
static uint16_t dawn_voc = 6000, dawn_loaded = 3300;
static bool dawn_stubborn;
static int dawn_poor;
static uint8_t dawn_vindpm_at_hiz_clear;
static void dawn_measure(void) {
  bool off = (chip[BQ_REG_CTRL0] & 0x04) || !(chip[BQ_REG_STAT0] & 0x08);
  uint16_t v = off ? dawn_voc : dawn_loaded;
  chip[BQ_REG_VBUS] = (uint8_t)(v >> 8); chip[BQ_REG_VBUS + 1] = (uint8_t)v;
}
static bool d_read(uint8_t reg, uint8_t *buf, uint8_t n) {
  if (chip_fail) return false;
  if (chip[BQ_REG_ADC_CTRL] & 0x80) dawn_measure();            // continuous conversion
  memcpy(buf, chip + reg, n);
  if (reg <= BQ_REG_FLAG0 && reg + n > BQ_REG_FLAG0) chip[BQ_REG_FLAG0] = 0;   // the flags clear when read
  return true;
}
static bool d_write(uint8_t reg, const uint8_t *buf, uint8_t n) {
  if (chip_fail) return false;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t a = (uint8_t)(reg + i);
    bool was_hiz = chip[BQ_REG_CTRL0] & 0x04;
    chip[a] = buf[i];
    chip_writes++;
    if (a == BQ_REG_CTRL0 && was_hiz && !(buf[i] & 0x04) && (chip[BQ_REG_STAT0] & 1)) {   // retry of the qualification
      dawn_vindpm_at_hiz_clear = chip[BQ_REG_VINDPM];
      if (!dawn_stubborn && chip[BQ_REG_VINDPM] * 100 * 10 >= dawn_voc * 7) chip[BQ_REG_STAT0] |= 0x08;
      else { chip[BQ_REG_STAT0] &= (uint8_t)~0x08; chip[BQ_REG_CTRL0] |= 0x04; chip[BQ_REG_FLAG0] |= 0x10; dawn_poor++; }
    }
  }
  return true;
}
static const bq_bus_t DBUS = { d_read, d_write };

// one second of the main loop: refresh, then apply
static void dawn_second(bq_t *b, const settings_t *s) {
  CHECK(bq_refresh(b));
  CHECK(bq_apply(b, s, CHEM_LIION, true));
}
static void dawn_start(bq_t *b, settings_t *s, uint16_t voc) {
  memset(chip, 0, sizeof(chip));
  chip[BQ_REG_TS] = 0x03; chip[BQ_REG_TS + 1] = 0x6C;
  chip[BQ_REG_STAT0] = 0x01;            // VBUS present, no PG
  chip[BQ_REG_CTRL0] = 0x06;            // EN_HIZ (the chip set it) + EN_TERM
  chip[BQ_REG_FLAG0] = 0x10;            // poor source
  chip[BQ_REG_VINDPM] = 0x24;           // 3.6 V
  chip[BQ_REG_ADC_CTRL] = 0x80;
  dawn_voc = voc; dawn_loaded = 3300; dawn_stubborn = false; dawn_poor = 0; chip_fail = false;
  bq_init(b, &DBUS);
  settings_defaults(s, CHEM_LIION);
}

static void test_bq_dawn(void) {
  CHECK(bq_vindpm_from_voc(6000) == 4800 && bq_vindpm_from_voc(5999) == 4700);
  CHECK(bq_vindpm_from_voc(4000) == 3600 && bq_vindpm_from_voc(0) == 3600);
  CHECK(bq_vindpm_from_voc(40000) == 22000 && bq_vindpm_from_voc(65535) == 22000);

  bq_t b; settings_t s;
  dawn_start(&b, &s, 6000);
  int hiz_clear_second = -1, vin_second = -1;
  for (int i = 0; i < 12; i++) {
    bool had_hiz = chip[BQ_REG_CTRL0] & 4;
    dawn_second(&b, &s);
    if (vin_second < 0 && chip[BQ_REG_VINDPM] == 48) vin_second = i;
    if (hiz_clear_second < 0 && had_hiz && !(chip[BQ_REG_CTRL0] & 4)) hiz_clear_second = i;
  }
  CHECK(chip[BQ_REG_VINDPM] == 48 && b.auto_vin_mv == 4800 && b.auto_vin_count == 1);
  CHECK(vin_second >= 0 && vin_second == hiz_clear_second);          // same pass: VINDPM first, then EN_HIZ
  CHECK(dawn_vindpm_at_hiz_clear == 48);                              // the retry already started at the new floor
  CHECK(dawn_poor == 0);
  CHECK(chip[BQ_REG_STAT0] & 0x08);                                   // PG
  CHECK(chip[BQ_REG_MPPT] & 1);                                       // MPPT now (PG)
  CHECK(b.poorsrc_count == 1 && bq_poorsrc_age_min(&b) == 0);
  CHECK(bq_why(&b) == ST2_POOR_SRC);                                  // PG, no HIZ any more, poor source seen < 30 min ago
  // MPPT runs: VINDPM is the chip's now, we leave it alone
  chip[BQ_REG_VINDPM] = 60;
  for (int i = 0; i < 5; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 60 && b.auto_vin_count == 1);

  // the light drops: poor source again, the chip puts 3.6 V back and drops EN_MPPT; Voc is 4.5 V now
  dawn_voc = 4500;
  chip[BQ_REG_STAT0] &= (uint8_t)~0x08; chip[BQ_REG_CTRL0] |= 4; chip[BQ_REG_FLAG0] |= 0x10;
  chip[BQ_REG_MPPT] &= 0xFE; chip[BQ_REG_VINDPM] = 0x24;
  for (int i = 0; i < 12; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 36 && b.auto_vin_mv == 3600 && (chip[BQ_REG_STAT0] & 0x08));
  CHECK(b.poorsrc_count == 2);
  dawn_voc = 7000;                                                    // brighter, another drop-out
  chip[BQ_REG_STAT0] &= (uint8_t)~0x08; chip[BQ_REG_CTRL0] |= 4; chip[BQ_REG_MPPT] &= 0xFE;
  for (int i = 0; i < 12; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 56);
  CHECK(b.auto_vin_count >= 2);

  // EN_MPPT latched (the chip clears it only when VBUS goes under VBUS_PRESENT) with HIZ / no PG: same recovery
  dawn_start(&b, &s, 6000);
  chip[BQ_REG_MPPT] |= 1;
  for (int i = 0; i < 12; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 48 && b.auto_vin_count == 1 && dawn_poor == 0 && (chip[BQ_REG_STAT0] & 0x08));
  CHECK(dawn_vindpm_at_hiz_clear == 48 && (chip[BQ_REG_MPPT] & 1));
  chip[BQ_REG_VINDPM] = 60;                                           // running (EN_MPPT, PG, no HIZ): hands off
  for (int i = 0; i < 5; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 60 && b.auto_vin_count == 1);
  dawn_voc = 7000;
  chip[BQ_REG_STAT0] &= (uint8_t)~0x08;                               // no PG, no HIZ, EN_MPPT still set
  for (int i = 0; i < 12; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 56);

  // a loaded reading is never Voc: PG = 1, no HIZ, MPPT refused (VBAT < VSYSMIN)
  dawn_start(&b, &s, 6000);
  chip[BQ_REG_STAT0] = 0x09; chip[BQ_REG_CTRL0] = 0x02; chip[BQ_REG_STAT3] = 0x10; chip[BQ_REG_FLAG0] = 0;
  dawn_loaded = 3500;
  for (int i = 0; i < 12; i++) dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 0x24 && b.auto_vin_count == 0 && !(chip[BQ_REG_MPPT] & 1));   // no EN_MPPT under VSYSMIN
  CHECK(bq_why(&b) == 0);

  // EN_MPPT only with PG and VSYS_STAT = 0
  chip[BQ_REG_STAT3] = 0;
  dawn_second(&b, &s);
  CHECK(chip[BQ_REG_MPPT] & 1);

  // fixed input voltage: no auto VINDPM, no hold
  dawn_start(&b, &s, 6000);
  s.vindpm_mv = 4500;
  dawn_second(&b, &s);
  CHECK(chip[BQ_REG_VINDPM] == 45 && !(chip[BQ_REG_CTRL0] & 4) && !(chip[BQ_REG_MPPT] & 1));
  for (int i = 0; i < 10; i++) {
    chip[BQ_REG_CTRL0] |= 4; chip[BQ_REG_STAT0] &= (uint8_t)~0x08;
    dawn_second(&b, &s);
    CHECK(!(chip[BQ_REG_CTRL0] & 4) && chip[BQ_REG_VINDPM] == 45);
  }
  CHECK(b.auto_vin_count == 0 && b.auto_vin_mv == 0);

  // no ADC: EN_HIZ is held 2 s of every 6, the rest cleared as before
  dawn_start(&b, &s, 6000);
  dawn_stubborn = true;
  for (int i = 0; i < 12; i++) {
    chip[BQ_REG_ADC_CTRL] = 0;           // the ADC never gives a result (bq_apply switches it on again each second)
    dawn_second(&b, &s);
  }
  CHECK(dawn_poor == 8 && b.auto_vin_count == 0);   // held 4 s of 12 (2 of every 6), cleared (and failed again) the other 8

  // bq_why keeps ST2_HIZ although bq_apply cleared EN_HIZ in r[] in the same pass
  dawn_start(&b, &s, 6000);
  CHECK(bq_refresh(&b) && b.hiz_seen);
  CHECK(bq_apply(&b, &s, CHEM_LIION, true));
  CHECK((bq_why(&b) & (ST2_HIZ | ST2_NO_PG)) == (ST2_HIZ | ST2_NO_PG));   // from what the chip had at the refresh

  // the latched poor-source flag alone (PG, no HIZ) does not mean the converter is off, and the TS ADC channel
  dawn_start(&b, &s, 6000);
  chip[BQ_REG_STAT0] = 0x09; chip[BQ_REG_CTRL0] = 0x02; chip[BQ_REG_FLAG0] = 0x10;
  CHECK(bq_refresh(&b));
  CHECK(!b.unl && !b.hiz_seen && b.poorsrc_count == 1);               // counted, not "unloaded"
  CHECK(bq_apply(&b, &s, CHEM_LIION, true));
  CHECK(!(chip[BQ_REG_ADC_DIS0] & 0x04));                             // VBUS, no HIZ: TS on

  // hiz_cleared marks only the first refresh after our EN_HIZ clear, even while the panel stays unloaded
  dawn_start(&b, &s, 6000);
  dawn_stubborn = true;
  chip[BQ_REG_CTRL0] = 0x02; chip[BQ_REG_STAT0] = 0x01;               // no PG, no HIZ
  b.hiz_cleared = true;
  CHECK(bq_refresh(&b) && b.unl && !b.hiz_cleared);
  chip[BQ_REG_CTRL0] |= 4;                                            // EN_HIZ seen: TS channel off during the hold
  CHECK(bq_refresh(&b) && b.hiz_seen);
  CHECK(bq_apply(&b, &s, CHEM_LIION, true));
  CHECK(chip[BQ_REG_ADC_DIS0] & 0x04);
}

static void test_heater(void) {
  settings_t s;
  settings_defaults(&s, CHEM_LIION);
  CHECK(s.heat_on_x10 == 20 && s.heat_off_x10 == 50 && s.heat_duty_pct == 100);
  CHECK(sizeof(settings_t) <= 96);                                  // fits one flash record
  // settings limits
  settings_t t = s;
  t.heat_off_x10 = 20; CHECK(settings_check(&t, CHEM_LIION) == ERR_RANGE);   // off must be above on
  t = s; t.heat_on_x10 = -300; t.heat_off_x10 = -290; CHECK(settings_check(&t, CHEM_LIION) == ERR_OK);
  t = s; t.heat_on_x10 = -301; CHECK(settings_check(&t, CHEM_LIION) == ERR_RANGE);
  t = s; t.heat_off_x10 = 501; CHECK(settings_check(&t, CHEM_LIION) == ERR_RANGE);
  t = s; t.heat_on_x10 = 401; t.heat_off_x10 = 450; CHECK(settings_check(&t, CHEM_LIION) == ERR_RANGE);
  t = s; t.heat_duty_pct = 101; CHECK(settings_check(&t, CHEM_LIION) == ERR_RANGE);
  t = s; t.heat_duty_pct = 0; CHECK(settings_check(&t, CHEM_LIION) == ERR_OK);
  // another JP4 chemistry keeps the heater settings
  t = s; t.heat_on_x10 = -50; t.heat_off_x10 = 0; t.heat_duty_pct = 40; t.chem_jumper = CHEM_LIION;
  uint8_t act;
  CHECK(settings_resolve_chem(&t, CHEM_LFP, &act) && t.heat_on_x10 == -50 && t.heat_off_x10 == 0 && t.heat_duty_pct == 40);

  // TS -> temperature (10k B3435, 5.1k / 30k): table points and in between
  int16_t T;
  CHECK(heater_ts_to_temp(595, &T) && T == 250);
  CHECK(heater_ts_to_temp(742, &T) && T == 0);
  CHECK(heater_ts_to_temp(718, &T) && T == 50);
  CHECK(heater_ts_to_temp(347, &T) && T == 600);
  CHECK(heater_ts_to_temp(835, &T) && T == -350);
  CHECK(heater_ts_to_temp(730, &T) && T > 0 && T < 50 && T >= 20 && T <= 30);     // about 2.5 degC
  CHECK(!heater_ts_to_temp(850, &T) && !heater_ts_to_temp(346, &T) && !heater_ts_to_temp(0, &T));
  int16_t prev = -10000;
  for (uint16_t ts = 835; ts >= 347; ts--) {                                   // monotonic: hotter = lower TS
    CHECK(heater_ts_to_temp(ts, &T) && T >= prev);
    prev = T;
  }

  // logic: cold battery, panel there
  heater_t h; heater_init(&h);
  heater_in_t in = { .sun = true, .bq_ok = true, .bq_fault = false, .ntc_present = true, .ntc_mode = NTC_AUTO, .ts_x10 = 742 }; // 0 degC
  heater_out_t o;
  heater_step(&h, &s, &in, &o);
  CHECK(o.temp_valid && o.temp_x10 == 0 && o.duty == 100 && h.on);
  in.ts_x10 = 730;                                   // ~2.5 degC: between on (2.0) and off (5.0): stays on
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);
  in.ts_x10 = 712;                                   // ~5.6 degC: above heat_off -> off
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !h.on && o.temp_x10 > 50);
  in.ts_x10 = 726;                                   // ~3.4 degC: between, was off: stays off
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && o.temp_valid);
  in.ts_x10 = 730;                                   // ~2.5 degC: still not below 2.0
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0);
  in.ts_x10 = 742;                                   // 0 degC: on again
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);

  // weak panel: the battery discharges with the heater on -> off for 60 s, then it may try again
  in.ibat_ma = -150; in.now_ms = 1000;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && h.blocked);
  in.ibat_ma = 0; in.now_ms = 1000 + HEAT_BLOCK_MS - 1;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0);
  in.now_ms = 1000 + HEAT_BLOCK_MS;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100 && !h.blocked);
  in.ibat_ma = -50;                                  // small discharge: stays on
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);
  in.ibat_ma = 0;

  // no sun: off, but the temperature is still reported; sun back: on again
  in.sun = false;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && o.temp_valid && o.temp_x10 == 0);
  in.sun = true;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);

  // NTC missing, NTC off, no I2C, fault, TS out of range: all off
  in.ntc_present = false; in.ts_x10 = 855;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !o.temp_valid && o.temp_x10 == HEAT_TEMP_NONE);
  in.ntc_present = true; in.ts_x10 = 742; in.ntc_mode = NTC_OFF;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !o.temp_valid);
  in.ntc_mode = NTC_ON;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);
  in.bq_ok = false;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !o.temp_valid);
  in.bq_ok = true; in.bq_fault = true;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && o.temp_valid);
  in.bq_fault = false;
  in.ts_x10 = 0;                                     // shorted NTC reads hot: off
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !o.temp_valid);
  in.ts_x10 = 742;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);
  // a fault in between: after it clears the state starts from off (cold = on again at once)
  in.bq_fault = true; heater_step(&h, &s, &in, &o); CHECK(!h.on);
  in.bq_fault = false; in.ts_x10 = 726;              // ~3.4 degC, between the limits: stays off after a fault
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0);

  // duty limit and disabled
  s.heat_duty_pct = 35;
  in.ts_x10 = 742;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 35);
  s.heat_duty_pct = 0;
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0 && !h.on && o.temp_valid);
  s.heat_duty_pct = 100; s.heat_on_x10 = -100; s.heat_off_x10 = -50;   // thresholds from the settings
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0);                     // 0 degC is above -5
  in.ts_x10 = 781;                                                       // -10 degC: not below -10 yet
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 0);
  in.ts_x10 = 790;                                                       // about -12 degC
  heater_step(&h, &s, &in, &o); CHECK(o.duty == 100);

  // registers: signed thresholds, status2, live values
  memset(&R, 0, sizeof(R));
  settings_defaults(&R.set, CHEM_LIION);
  R.live.chem_jumper = CHEM_LIION; R.live.chem = CHEM_LIION;
  R.live.bat_temp_x10 = -75; R.live.heat_pct = 60; R.live.status2 = ST2_HEATING;
  CHECK(rd(ADDR_SMPPT, R_BAT_TEMP) == (uint16_t)-75 && rd(ADDR_SMPPT, R_HEAT_PCT) == 60 && rd(ADDR_SMPPT, R_STATUS2) == 1);
  CHECK(rd(ADDR_SMPPT, R_HEAT_ON) == 20 && rd(ADDR_SMPPT, R_HEAT_OFF) == 50 && rd(ADDR_SMPPT, R_HEAT_DUTY) == 100);
  wr(ADDR_SMPPT, R_HEAT_ON, (uint16_t)-30);
  CHECK(R.result == ERR_OK && R.set.heat_on_x10 == -30 && rd(ADDR_SMPPT, R_HEAT_ON) == (uint16_t)-30);
  wr(ADDR_SMPPT, R_HEAT_OFF, 10);
  CHECK(R.result == ERR_OK && R.set.heat_off_x10 == 10);
  wr(ADDR_SMPPT, R_HEAT_OFF, (uint16_t)-30);
  CHECK(R.result == ERR_RANGE && R.set.heat_off_x10 == 10);
  wr(ADDR_SMPPT, R_HEAT_DUTY, 50);
  CHECK(R.result == ERR_OK && R.set.heat_duty_pct == 50);
  wr(ADDR_SMPPT, R_HEAT_DUTY, 150);
  CHECK(R.result == ERR_RANGE && R.set.heat_duty_pct == 50);
  wr(ADDR_SMPPT, R_SET_LAST + 1, 1);
  CHECK(R.result == ERR_BAD_REG);
  R.live.poorsrc_count = 3; R.live.poorsrc_age_min = 2; R.live.status2 = ST2_HIZ | ST2_NO_PG | ST2_POOR_SRC;
  CHECK(rd(ADDR_SMPPT, R_POORSRC) == 3 && rd(ADDR_SMPPT, R_POORSRC_AGE) == 2 && rd(ADDR_SMPPT, R_STATUS2) == 0x1C);
  CHECK(R_STATUS2 == 0x13 && R_POORSRC == 0x14 && (ST2_HEATING | ST2_V03) == 3);   // new bits only above the old ones
  CHECK(R_SET_LAST == 0x3F && R_EXPERT0 == 0x35 && R_CYCLES == 0x10 && R_CMD == 0x61);   // old addresses did not move
}

int main(void) {
  test_settings();
  test_store();
  test_regs();
  test_gauge();
  test_bms();
  test_bq();
  test_bq_dawn();
  test_heater();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
