#include "bms.h"
#include <string.h>

void bms_init(bms_t *b) {
  memset(b, 0, sizeof(*b));
  b->j7_on = false;
}

static bool count(uint8_t *c, bool cond) {
  if (!cond) { *c = 0; return false; }
  if (*c < 255) (*c)++;
  return *c >= BMS_DEBOUNCE;
}

void bms_step(bms_t *b, const settings_t *s, uint8_t chem, const bms_in_t *in, bms_out_t *o) {
  memset(o, 0, sizeof(*o));
  int cells = chem_cells(chem);
  uint16_t vcell_max = cells == 2 ? (in->vcell1_mv > in->vcell2_mv ? in->vcell1_mv : in->vcell2_mv) : in->vpack_mv;
  uint16_t vcell_min = cells == 2 ? (in->vcell1_mv < in->vcell2_mv ? in->vcell1_mv : in->vcell2_mv) : in->vpack_mv;
  uint16_t vchg_cell = (uint16_t)(s->vchg_mv / cells);

  // ---- short circuit / over-current: FET off, pause, retry (with sun) ----
  if (in->short_tripped) { b->shorted = true; b->retry_s = BMS_RETRY_S; }
  int32_t idsg = -(int32_t)in->ibat_ma;
  if (s->idsg_max_ma && count(&b->oc_count, idsg > s->idsg_max_ma)) {
    b->overcurrent = true;
    b->retry_s = BMS_RETRY_S;
  }
  if ((b->shorted || b->overcurrent) && b->retry_s) {
    if (--b->retry_s == 0) { b->shorted = false; b->overcurrent = false; b->oc_count = 0; }
  }
  bool trip = b->shorted || b->overcurrent;

  // ---- under-voltage (pack, and each cell for 2S) ----
  uint16_t od_off_cell = (uint16_t)(s->od_off_mv / cells);
  bool low = in->vpack_mv < s->od_off_mv || (cells == 2 && vcell_min < od_off_cell);
  if (count(&b->uv_count, low)) {
    if (!b->uv) o->empty = true;
    b->uv = true;
  }
  if (b->uv && in->vpack_mv >= s->od_on_mv && (cells == 1 || vcell_min >= od_off_cell + 100)) b->uv = false;

  // ---- over-voltage: charge FET off and no charging ----
  if (count(&b->ov_count, vcell_max > vchg_cell + 50)) b->ov = true;
  if (b->ov && vcell_max < vchg_cell - 100) b->ov = false;

  // ---- ship mode: no sun and (under-voltage, or a trip that cut our own supply) ----
  bool want_ship = !in->sun && (b->uv || trip || in->vpack_mv < s->ship_mv);
  if (count(&b->ship_count, want_ship)) o->ship = true;
  if (in->short_tripped && !in->sun) o->ship = true;   // don't wait: the FET is already off

  b->od_off = b->uv || trip;
  b->oc_off = b->ov;
  o->od_off = b->od_off;
  o->oc_off = b->oc_off;
  o->charge_ok = !b->ov && !in->bq_i2c_err;

  // ---- J7 with hysteresis (JP5 2-3) ----
  if (b->j7_on && in->vpack_mv < s->j7_off_mv) b->j7_on = false;
  else if (!b->j7_on && in->vpack_mv > s->j7_on_mv && !b->od_off) b->j7_on = true;
  if (b->od_off) b->j7_on = false;
  o->j7_on = b->j7_on;

  // ---- LTO 2S balancing: bleed the higher cell ----
  if (cells == 2) {
    int32_t d = (int32_t)in->vcell1_mv - (int32_t)in->vcell2_mv;
    int32_t ad = d < 0 ? -d : d;
    bool active = b->bal1 || b->bal2;
    if (!active && ad > s->bal_delta_mv && vcell_max > s->bal_start_mv) {
      b->bal1 = d > 0;
      b->bal2 = d < 0;
    } else if (active && (ad < s->bal_delta_mv / 2 || vcell_max < s->bal_start_mv - 50)) {
      b->bal1 = b->bal2 = false;
    }
    if (b->uv) b->bal1 = b->bal2 = false;
  } else {
    b->bal1 = b->bal2 = false;
  }
  o->bal1 = b->bal1;
  o->bal2 = b->bal2;

  uint16_t st = 0;
  if (b->od_off) st |= ST_OD_OFF;
  if (b->oc_off) st |= ST_OC_OFF;
  if (b->shorted) st |= ST_SHORT;
  if (b->uv) st |= ST_UV;
  if (b->ov) st |= ST_OV;
  if (b->overcurrent) st |= ST_OVERCURRENT;
  if (b->bal1 || b->bal2) st |= ST_BALANCING;
  if (b->j7_on) st |= ST_J7_ON;
  if (in->sun) st |= ST_SUN;
  if (in->bq_fault) st |= ST_BQ_FAULT;
  if (in->bq_i2c_err) st |= ST_BQ_I2C_ERR;
  o->status = st;
}

uint8_t bms_led(uint16_t st, uint32_t now_ms) {
  uint32_t ph = now_ms % 2000;
  if (st & (ST_SHORT | ST_OV | ST_OVERCURRENT | ST_BQ_FAULT | ST_BQ_I2C_ERR))
    return ph < 60 ? LED_RED : LED_OFF;
  if (st & (ST_OD_OFF | ST_UV | ST_BALANCING | ST_OC_OFF))
    return ph < 60 ? LED_YELLOW : LED_OFF;
  return LED_OFF;
}
