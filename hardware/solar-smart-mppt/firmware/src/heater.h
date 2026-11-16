// Battery heater, pure logic (host tested). Called every second (and on every INT event) with fresh readings.
//
// The heater (MOSFET on PA11, fed from SYS) only runs while the panel gives power. A weak panel makes the
// BQ25798 top SYS up from the battery: when the battery discharges by more than HEAT_DSG_MA the heater goes
// off and stays off for HEAT_BLOCK_MS. The battery temperature is the NTC on J3 read by the BQ25798 TS ADC. Fail safe:
// everything unknown (no NTC, NTC off, no I2C, BQ fault, temperature outside the table) = heater off.
//   temp < heat_on  -> on;   temp > heat_off -> off;   in between: as before (hysteresis)
//   duty (heat_duty %, 0 = heater disabled) is applied as PWM while it is on.
#pragma once
#include "smppt.h"

#define HEAT_TEMP_NONE ((int16_t)0x8000)   // register value when there is no reading

#define HEAT_DSG_MA   100        // battery discharge that stops the heater
#define HEAT_BLOCK_MS 60000u     // ... for this long

typedef struct {
  bool on;
  bool blocked;
  uint32_t blocked_at_ms;
} heater_t;

typedef struct {
  bool sun;            // VBUS present
  bool bq_ok;          // last BQ25798 read worked
  bool bq_fault;       // any fault flag set
  bool ntc_present;    // TS below the "open" level
  uint8_t ntc_mode;    // NTC_*
  uint16_t ts_x10;     // TS in 0.1 % of REGN
  int16_t ibat_ma;     // battery current, + = charging
  uint32_t now_ms;
} heater_in_t;

typedef struct {
  bool temp_valid;
  int16_t temp_x10;    // 0.1 degC, HEAT_TEMP_NONE when not valid
  uint8_t duty;        // PWM percent for the gate, 0 = off
} heater_out_t;

void heater_init(heater_t *h);
void heater_step(heater_t *h, const settings_t *s, const heater_in_t *in, heater_out_t *out);

// TS (0.1 % of REGN) -> battery temperature (0.1 degC) for the 10k B3435 NTC with R2/R3 (5.1k / 30k).
// false outside -35 .. +60 degC (open, short, or too far off the table).
bool heater_ts_to_temp(uint16_t ts_x10, int16_t *temp_x10);
