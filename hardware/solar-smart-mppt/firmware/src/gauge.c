#include "gauge.h"
#include <string.h>

void gauge_init(gauge_t *g, const gauge_state_t *saved) {
  memset(g, 0, sizeof(*g));
  if (saved) g->st = *saved;
  else g->st.charge_mas = -1;
}

uint16_t gauge_capacity(const gauge_t *g, const settings_t *s) {
  return s->capacity_mah ? s->capacity_mah : g->st.capacity_learnt_mah;
}

void gauge_step(gauge_t *g, const settings_t *s, int16_t ibat_ma, uint32_t dt_ms) {
  g->frac_mams += (int32_t)ibat_ma * (int32_t)dt_ms;
  int32_t d = g->frac_mams / 1000;        // whole mA*s, sign kept
  if (d == 0) return;
  g->frac_mams -= d * 1000;

  if (g->learning) {
    g->since_full_mas -= d;
    if (g->since_full_mas < 0) g->since_full_mas = 0;   // charged above the last "full"
  }
  uint16_t cap = gauge_capacity(g, s);
  if (g->st.charge_mas >= 0 && cap) {
    int32_t max = (int32_t)cap * 3600;
    int32_t c = g->st.charge_mas + d;
    g->st.charge_mas = c < 0 ? 0 : c > max ? max : c;
  }
  if (d < 0 && cap) {
    g->cycle_mas -= d;
    int32_t tenth = (int32_t)cap * 360;   // 0.1 of a full cycle
    while (g->cycle_mas >= tenth) {
      g->cycle_mas -= tenth;
      if (g->st.cycles_x10 < 0xFFFF) g->st.cycles_x10++;
    }
  }
}

void gauge_full(gauge_t *g, const settings_t *s) {
  uint16_t cap = gauge_capacity(g, s);
  g->st.charge_mas = cap ? (int32_t)cap * 3600 : -1;
  g->since_full_mas = 0;
  g->learning = true;
}

void gauge_empty(gauge_t *g) {
  if (g->learning) {
    int32_t mah = g->since_full_mas / 3600;
    if (mah >= 100) g->st.capacity_learnt_mah = (uint16_t)(mah > 65000 ? 65000 : mah);
  }
  g->learning = false;
  g->st.charge_mas = 0;
}

void gauge_reset(gauge_t *g) {
  g->st.charge_mas = -1;
  g->learning = false;
  g->since_full_mas = 0;
}

typedef struct { uint16_t mv; uint16_t pct; } ocv_t;
// open-circuit voltage per cell -> percent (rough: under load or charge it reads low/high)
static const ocv_t OCV_LIION[] = { {3000, 0}, {3300, 5}, {3600, 20}, {3700, 40}, {3800, 60}, {3950, 80}, {4100, 95}, {4200, 100} };
static const ocv_t OCV_LFP[]   = { {2700, 0}, {3000, 5}, {3200, 20}, {3250, 40}, {3300, 70}, {3350, 90}, {3400, 98}, {3600, 100} };
static const ocv_t OCV_LTO[]   = { {1800, 0}, {2000, 5}, {2200, 20}, {2300, 40}, {2400, 60}, {2500, 80}, {2650, 95}, {2800, 100} };

uint16_t soc_from_voltage(uint8_t chem, uint16_t vpack_mv) {
  const ocv_t *t = chem == CHEM_LFP ? OCV_LFP : chem == CHEM_LTO2S ? OCV_LTO : OCV_LIION;
  uint16_t v = (uint16_t)(vpack_mv / chem_cells(chem));
  const int n = 8;
  if (v <= t[0].mv) return 0;
  if (v >= t[n - 1].mv) return 10000;
  for (int i = 1; i < n; i++) {
    if (v < t[i].mv) {
      uint32_t span = t[i].mv - t[i - 1].mv;
      uint32_t p0 = t[i - 1].pct * 100u, p1 = t[i].pct * 100u;
      return (uint16_t)(p0 + (p1 - p0) * (v - t[i - 1].mv) / span);
    }
  }
  return 10000;
}

uint16_t gauge_soc(const gauge_t *g, const settings_t *s, uint8_t chem, uint16_t vpack_mv, bool *from_v) {
  uint16_t cap = gauge_capacity(g, s);
  if (cap && g->st.charge_mas >= 0) {
    *from_v = false;
    uint32_t soc = (uint32_t)((uint64_t)g->st.charge_mas * 100u / ((uint32_t)cap * 36u));
    return (uint16_t)(soc > 10000 ? 10000 : soc);
  }
  *from_v = true;
  return soc_from_voltage(chem, vpack_mv);
}

uint16_t gauge_remaining_mah(const gauge_t *g) {
  return g->st.charge_mas > 0 ? (uint16_t)(g->st.charge_mas / 3600) : 0;
}
