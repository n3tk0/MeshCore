#include "heater.h"

// TS / REGN in 0.1 % for -35 .. +60 degC in 5 degC steps (10k B=3435 NTC, 5.1k from REGN, 30k to GND in parallel)
#define T_FIRST_C  (-35)
#define T_STEP_C   5
static const uint16_t TS_TAB[] = {
  835, 828, 820, 809, 796, 781, 763, 742, 718, 691, 661, 629, 595, 560, 523, 486, 450, 414, 380, 347,
};
#define TS_N ((int)(sizeof(TS_TAB) / sizeof(TS_TAB[0])))

bool heater_ts_to_temp(uint16_t ts_x10, int16_t *temp_x10) {
  if (ts_x10 > TS_TAB[0] || ts_x10 < TS_TAB[TS_N - 1]) return false;
  int i = 0;
  while (i < TS_N - 2 && ts_x10 < TS_TAB[i + 1]) i++;
  int hi = TS_TAB[i], lo = TS_TAB[i + 1];
  int t = (T_FIRST_C + i * T_STEP_C) * 10 + (hi - ts_x10) * (T_STEP_C * 10) / (hi - lo);
  *temp_x10 = (int16_t)t;
  return true;
}

void heater_init(heater_t *h) { h->on = false; h->blocked = false; }

void heater_step(heater_t *h, const settings_t *s, const heater_in_t *in, heater_out_t *out) {
  int16_t t = 0;
  bool valid = in->bq_ok && in->ntc_mode != NTC_OFF && in->ntc_present && heater_ts_to_temp(in->ts_x10, &t);
  out->temp_valid = valid;
  out->temp_x10 = valid ? t : HEAT_TEMP_NONE;

  if (h->on && in->ibat_ma < -HEAT_DSG_MA) { h->blocked = true; h->blocked_at_ms = in->now_ms; }
  if (h->blocked && (uint32_t)(in->now_ms - h->blocked_at_ms) >= HEAT_BLOCK_MS) h->blocked = false;

  if (!valid || in->bq_fault || !in->sun || h->blocked || s->heat_duty_pct == 0) {
    h->on = false;
  } else if (t < s->heat_on_x10) {
    h->on = true;
  } else if (t > s->heat_off_x10) {
    h->on = false;
  }
  out->duty = h->on ? (uint8_t)(s->heat_duty_pct > 100 ? 100 : s->heat_duty_pct) : 0;
}
