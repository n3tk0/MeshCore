// solar-smart-mppt main loop: every 10 ms the LED, every second the BMS, the gauge and the BQ25798.
#include "hal.h"
#include "board.h"
#include "regs.h"
#include "settings.h"
#include "gauge.h"
#include "bms.h"
#include "bq25798.h"
#include <string.h>

static bq_t bq;
static gauge_t gauge;
static bms_t bms;
static settings_t saved;           // what is in flash (for ST_SETTINGS_DIRTY)
static uint8_t chem_active, chem_jumper;
static uint16_t last_sh_n_mv;
static int32_t last_shunt_uv;

static uint8_t active_chem(const settings_t *s) {
  return s->chem == CHEM_BY_JUMPER ? chem_jumper : s->chem;
}

static void load_settings(void) {
  settings_t s;
  if (store_load(REC_SETTINGS, &s, sizeof(s)) && settings_check(&s, s.chem == CHEM_BY_JUMPER ? s.chem_jumper : s.chem) == ERR_OK) {
    if (settings_resolve_chem(&s, chem_jumper, &chem_active)) {
      // the jumper moved: the voltages are the jumper's defaults now, store that
      store_save(REC_SETTINGS, &s, sizeof(s));
    }
  } else {
    settings_defaults(&s, chem_jumper);
    chem_active = chem_jumper;
  }
  saved = s;
  __disable_irq();
  R.set = s;
  __enable_irq();
}

static gauge_state_t gauge_saved;

// writes only when something moved (1 % of the charge, the learnt capacity or the cycles): a board
// that restarts over and over (weak pack, no sun) must not wear the flash out
static void save_gauge(void) {
  const gauge_state_t *g = &gauge.st, *o = &gauge_saved;
  int32_t step = (int32_t)gauge_capacity(&gauge, &R.set) * 36;   // 1 % in mA*s
  int32_t d = g->charge_mas - o->charge_mas;
  if (d < 0) d = -d;
  bool moved = g->capacity_learnt_mah != o->capacity_learnt_mah || g->cycles_x10 != o->cycles_x10 ||
               (g->charge_mas < 0) != (o->charge_mas < 0) || d > step;
  if (moved && store_save(REC_GAUGE, g, sizeof(*g))) gauge_saved = *g;
}

static uint32_t ship_tried_ms;
static bool ship_tried;

// BQ25798 ship mode: everything goes dark until the panel gives power again. If the chip does not
// take it (no I2C, or VBUS just present), carry on and try again in 10 minutes, no reboot.
static void go_ship(uint32_t now) {
  if (ship_tried && (uint32_t)(now - ship_tried_ms) < 600000u) return;
  ship_tried = true;
  ship_tried_ms = now;
  save_gauge();
  hal_charge(false);
  if (bq_ship(&bq)) hal_delay_ms(100);
}

static uint16_t run_cmd(uint8_t cmd, const settings_t *s) {
  switch (cmd) {
    case CMD_SAVE: {
      settings_t t = *s;
      t.chem_jumper = chem_jumper;
      if (!store_save(REC_SETTINGS, &t, sizeof(t))) return ERR_FLASH;
      saved = t;
      __disable_irq(); R.set.chem_jumper = chem_jumper; __enable_irq();
      return ERR_OK;
    }
    case CMD_LOAD:
      load_settings();
      return ERR_OK;
    case CMD_DEFAULTS: {
      settings_t t;
      settings_defaults(&t, chem_jumper);
      if (!store_save(REC_SETTINGS, &t, sizeof(t))) return ERR_FLASH;
      saved = t;
      __disable_irq(); R.set = t; __enable_irq();
      return ERR_OK;
    }
    case CMD_COUNTER_RESET: gauge_reset(&gauge); save_gauge(); return ERR_OK;
    case CMD_SET_FULL: gauge_full(&gauge, s); save_gauge(); return ERR_OK;
    case CMD_ZERO: {
      // current is 0 A now: what the shunt shows is offset (needs SAVE to keep it)
      int32_t z = last_shunt_uv;
      if (z > 5000 || z < -5000) return ERR_NOT_NOW;   // more than 5 mV = 250 mA: not at rest
      __disable_irq(); R.set.zero_uv = (int16_t)z; __enable_irq();
      return ERR_OK;
    }
    case CMD_SHIP: ship_tried = false; go_ship(hal_ms()); return ERR_NOT_NOW;   // still here: not taken
    case CMD_BSL: save_gauge(); hal_delay_ms(20); hal_enter_bsl(); return ERR_OK;
    case CMD_REBOOT: save_gauge(); hal_delay_ms(20); hal_reboot(); return ERR_OK;
    default: return ERR_UNKNOWN_CMD;
  }
}

int main(void) {
  hal_init();
  chem_jumper = hal_read_chem_jumper();
  store_init(&hal_flash);
  load_settings();
  gauge_state_t gs;
  gauge_init(&gauge, store_load(REC_GAUGE, &gs, sizeof(gs)) ? &gs : 0);
  gauge_saved = gauge.st;
  bms_init(&bms);
  bq_init(&bq, &hal_bq_bus);
  R.ina_config = INA_CONFIG_DEFAULT;
  R.result = ERR_OK;
  R.gauge = gauge.st;
  R.live.chem_jumper = chem_jumper;
  R.live.chem = chem_active;
  analog_init();
  i2c_target_init();

  uint32_t last_s = hal_ms(), last_save = hal_ms(), last_wd = 0;
  bool first = true, was_done = false;
  for (;;) {
    uint32_t now = hal_ms();
    R.now_ms = now;
    bool sc = analog_short_tripped();
    if (sc) hal_od_off(true);

    uint8_t cmd = R.pending_cmd;
    if (cmd != CMD_NONE) {
      settings_t s;
      __disable_irq(); s = R.set; __enable_irq();
      uint16_t res = run_cmd(cmd, &s);
      __disable_irq(); R.result = res; R.pending_cmd = CMD_NONE; __enable_irq();
    }

    if (first || sc || (uint32_t)(now - last_s) >= 1000u) {
      uint32_t dt = first ? 0 : now - last_s;
      last_s = now;
      first = false;

      settings_t s;
      __disable_irq(); s = R.set; __enable_irq();
      chem_active = active_chem(&s);

      analog_t a;
      analog_read(&a);
      last_shunt_uv = a.shunt_uv;
      last_sh_n_mv = a.sh_n_mv;
      int32_t ib = (a.shunt_uv - s.zero_uv) * 1000 / (int32_t)s.shunt_uohm;   // mA, + = charging
      if (ib > 32767) ib = 32767; else if (ib < -32768) ib = -32768;
      analog_set_short(s.isc_ma, s.shunt_uohm, last_sh_n_mv);

      bq_refresh(&bq);
      bool sun = bq.ok && bq_u16(&bq, BQ_REG_VBUS) > 3500;

      bms_in_t in = {
        .vpack_mv = a.vbat_mv,
        .vcell1_mv = a.vbm_mv,
        .vcell2_mv = a.vbat_mv > a.vbm_mv ? (uint16_t)(a.vbat_mv - a.vbm_mv) : 0,
        .ibat_ma = (int16_t)ib,
        .sun = sun,
        .short_tripped = sc,
        .bq_fault = bq.ok && (bq.r[BQ_REG_FAULT0] | bq.r[BQ_REG_FAULT1]) != 0,
        .bq_i2c_err = bq.errors >= 3,
      };
      bms_out_t o;
      bms_step(&bms, &s, chem_active, &in, &o);
      if (o.empty) gauge_empty(&gauge);
      if (o.ship) go_ship(now);

      hal_od_off(o.od_off || analog_short_pending());   // a trip since 'sc' was read stays off
      hal_oc_off(o.oc_off);
      hal_j7(o.j7_on);
      hal_bal(o.bal1, o.bal2);
      bool applied = bq_apply(&bq, &s, chem_active, o.charge_ok);   // always: ADC, NTC, limits
      hal_charge(o.charge_ok && applied);   // ~CE low only with our settings in the chip
      if ((uint32_t)(now - last_wd) >= 10000u) { last_wd = now; bq_kick_watchdog(&bq); }

      uint8_t chg_stat = (uint8_t)(bq.r[BQ_REG_STAT1] >> 5);
      bool done = bq.ok && chg_stat == BQ_CHG_DONE;
      if (done && !was_done) gauge_full(&gauge, &s);   // once, on the way into "charge done"
      if (bq.ok) was_done = done;
      gauge_step(&gauge, &s, (int16_t)ib, dt);
      if ((uint32_t)(now - last_save) >= 3600000u) { last_save = now; save_gauge(); }

      bool from_v;
      uint16_t soc = gauge_soc(&gauge, &s, chem_active, a.vbat_mv, &from_v);
      uint16_t st = o.status;
      if (bq.ok && (chg_stat >= 1 && chg_stat <= 6)) st |= ST_CHARGING;
      if (bq.ok && chg_stat == BQ_CHG_DONE) st |= ST_FULL;
      if (from_v) st |= ST_SOC_FROM_V;
      if (bq.ok && !bq.ntc_present) st |= ST_NTC_MISSING;
      if (memcmp(&s, &saved, sizeof(s)) != 0) st |= ST_SETTINGS_DIRTY;

      live_t L = {0};
      L.vpack_mv = a.vbat_mv;
      if (chem_cells(chem_active) == 2) { L.vcell1_mv = in.vcell1_mv; L.vcell2_mv = in.vcell2_mv; }
      L.ibat_ma = (int16_t)ib;
      L.vbus_mv = bq_u16(&bq, BQ_REG_VBUS);
      L.ibus_ma = bq_i16(&bq, BQ_REG_IBUS);
      L.temp_x10 = (int16_t)(bq_i16(&bq, BQ_REG_TDIE) * 5);
      L.soc_x100 = soc;
      L.remaining_mah = gauge_remaining_mah(&gauge);
      L.status = st;
      L.chem = chem_active;
      L.chem_jumper = chem_jumper;
      L.bq_chg_stat = chg_stat;
      L.bq_fault0 = bq.r[BQ_REG_FAULT0];
      L.bq_fault1 = bq.r[BQ_REG_FAULT1];
      __disable_irq();
      R.live = L;
      R.gauge = gauge.st;
      memcpy(R.bq, bq.r, sizeof(R.bq));
      __enable_irq();
    }

    hal_led(bms_led(R.live.status, now));
    hal_idle();
  }
}
