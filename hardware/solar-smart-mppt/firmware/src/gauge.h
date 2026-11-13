// Charge counter: counts mA*s through the shunt, sets 100 % when the BQ25798 says "charge done",
// learns the capacity from a full charge down to the under-voltage cut-off.
#pragma once
#include "smppt.h"

typedef struct {
  gauge_state_t st;          // saved part
  int32_t  frac_mams;        // mA*ms not yet counted
  int32_t  since_full_mas;   // drawn since the last full charge (net), for learning
  bool     learning;         // a full charge was seen and no under-voltage since
  int32_t  cycle_mas;        // discharge counted towards the next 0.1 cycle
} gauge_t;

void gauge_init(gauge_t *g, const gauge_state_t *saved);   // saved may be 0
uint16_t gauge_capacity(const gauge_t *g, const settings_t *s);   // mAh, 0 = unknown
void gauge_step(gauge_t *g, const settings_t *s, int16_t ibat_ma, uint32_t dt_ms);
void gauge_full(gauge_t *g, const settings_t *s);           // charge done / SET_FULL
void gauge_empty(gauge_t *g);                               // under-voltage cut-off reached
void gauge_reset(gauge_t *g);                               // COUNTER_RESET
// percent * 100; *from_v = true when it comes from the voltage table
uint16_t gauge_soc(const gauge_t *g, const settings_t *s, uint8_t chem, uint16_t vpack_mv, bool *from_v);
uint16_t gauge_remaining_mah(const gauge_t *g);
uint16_t soc_from_voltage(uint8_t chem, uint16_t vpack_mv);
