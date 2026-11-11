// Host unit tests: settings, flash store (with power cuts), register map + I2C framing, gauge, BMS,
// BQ25798 driver against a fake chip.   make test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regs.h"
#include "settings.h"
#include "gauge.h"
#include "bms.h"
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
  bms_in_t in = { .vpack_mv = 3800, .ibat_ma = -100, .sun = true };
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
  bool ship = false;
  for (int i = 0; i < 2 * BMS_DEBOUNCE; i++) { bms_step(&b, &s, CHEM_LIION, &in, &o); ship |= o.ship; }
  CHECK(ship);

  // short with sun: FET off, retry after the pause; without sun: ship at once
  bms_init(&b);
  in.vpack_mv = 3800; in.sun = true; in.short_tripped = true;
  bms_step(&b, &s, CHEM_LIION, &in, &o);
  CHECK(o.od_off && (o.status & ST_SHORT) && !o.ship);
  in.short_tripped = false;
  for (int i = 0; i < BMS_RETRY_S - 2; i++) bms_step(&b, &s, CHEM_LIION, &in, &o);
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
  CHECK(chip[BQ_REG_MPPT] == (uint8_t)((4 << 5) | 0x1E | 1));   // expert timing bits, our VOC_PCT
  CHECK((chip[BQ_REG_NTC1] & 1) == 1);                            // no NTC: TS ignored
  CHECK(chip[BQ_REG_CTRL0] == 0x02);                              // not charging yet, EN_TERM kept
  CHECK((chip[BQ_REG_CTRL1] & 7) == 5 && (chip[BQ_REG_ADC_CTRL] & 0x80));
  CHECK((chip[BQ_REG_VSYSMIN] & 0x3F) == 2);                      // 3000 mV
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
  // NTC present
  chip[BQ_REG_TS] = 0x02; chip[BQ_REG_TS + 1] = 0x60;             // 59 %
  CHECK(bq_refresh(&b) && bq_apply(&b, &s, CHEM_LIION, true) && (chip[BQ_REG_NTC1] & 1) == 0 && b.ntc_present);
  // bus error
  chip_fail = true;
  CHECK(!bq_refresh(&b) && !bq_apply(&b, &s, CHEM_LIION, true) && b.errors == 1);
  chip_fail = false;
  CHECK(bq_ship(&b) && (chip[BQ_REG_CTRL2] & 7) == 5);
}

int main(void) {
  test_settings();
  test_store();
  test_regs();
  test_gauge();
  test_bms();
  test_bq();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
