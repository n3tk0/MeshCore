// BMS decisions, pure logic (host tested). Called once a second with fresh readings.
//
// The MCU is fed from the pack side behind the discharge FET, so "discharge FET off" without sun
// would power the MCU down, R19 would turn the FET back on and it would start over. So:
//  - under-voltage with sun: discharge FET off, charging goes on; back on above od_on;
//  - under-voltage without sun: BQ25798 ship mode (everything off until the panel gives power);
//  - short (COMP0 interrupt) or software over-current: FET off, retry after a pause with sun,
//    ship mode without sun.
#pragma once
#include "smppt.h"

typedef struct {
  uint16_t vpack_mv, vcell1_mv, vcell2_mv;
  int16_t  ibat_ma;          // + = charging
  bool     sun;              // VBUS present
  bool     short_tripped;    // COMP0 fired since the last call
  bool     bq_fault, bq_i2c_err, ntc_missing;
} bms_in_t;

typedef struct {
  bool od_off;               // discharge FET off
  bool oc_off;               // charge FET off
  bool charge_ok;            // ~CE may go low
  bool j7_on;
  bool bal1, bal2;
  bool ship;                 // go to ship mode now (save the gauge first)
  bool empty;                // under-voltage reached (gauge learning stops here)
  uint16_t status;           // ST_* bits owned by the BMS
} bms_out_t;

typedef struct {
  uint8_t uv_count, ov_count, oc_count, ship_count;
  bool od_off, oc_off, j7_on, uv, ov, overcurrent, shorted, bal1, bal2;
  uint16_t retry_s;          // seconds until the FET is tried again after a short / over-current
} bms_t;

#define BMS_DEBOUNCE   3     // seconds a limit must hold
#define BMS_RETRY_S    60    // after a short or over-current (with sun)

void bms_init(bms_t *b);
void bms_step(bms_t *b, const settings_t *s, uint8_t chem, const bms_in_t *in, bms_out_t *out);

// LED on PA9: 0 off, 1 red, 2 yellow. A short flash every 2 s so it costs almost nothing.
enum { LED_OFF = 0, LED_RED = 1, LED_YELLOW = 2 };
uint8_t bms_led(uint16_t status, uint32_t now_ms);
