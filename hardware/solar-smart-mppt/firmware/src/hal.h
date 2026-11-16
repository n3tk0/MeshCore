// Hardware glue for the MSPM0L1306 (not built for the host tests)
#pragma once
#include "smppt.h"
#include "bq25798.h"
#include "settings.h"

void hal_init(void);
uint32_t hal_ms(void);          // 10 ms resolution, from TIMG0 (runs in STOP)
void hal_idle(void);            // sleep until the next interrupt
void hal_delay_ms(uint32_t ms);

void hal_od_off(bool off);      // discharge FET
void hal_oc_off(bool off);      // charge FET
void hal_charge(bool on);       // ~CE
void hal_j7(bool on);
void hal_bal(bool b1, bool b2);
void hal_led(uint8_t led);      // LED_*
uint8_t hal_read_chem_jumper(void);
#if !SMPPT_J5_PA10
void hal_heater(uint8_t pct);   // heater gate: 0 off, 100 on, between = 1 kHz PWM (TIMG4)
bool hal_int_taken(void);       // BQ25798 INT went low since the last call (and clears that)
void hal_int_isr(void);         // from the GPIOA interrupt
#endif

extern const bq_bus_t hal_bq_bus;
extern const flash_ops_t hal_flash;

void hal_reboot(void);
void hal_enter_bsl(void);

// analog (analog.c)
typedef struct {
  int32_t shunt_uv;             // SH_P - SH_N, offset not removed
  uint16_t vbat_mv, vbm_mv;
  uint16_t sh_n_mv;             // OPA1 output / 16, for the COMP threshold
} analog_t;

void analog_init(void);
bool analog_read(analog_t *a);   // false: ADC did not finish, *a unchanged
// short circuit: discharge above isc_ma trips COMP0 (the ISR turns OD off at once)
void analog_set_short(uint16_t isc_ma, uint16_t shunt_uohm, uint16_t sh_n_mv);
bool analog_short_tripped(void);  // and clears the flag
bool analog_short_pending(void);  // without clearing it

// I2C target (i2c_target.c)
void i2c_target_init(void);
