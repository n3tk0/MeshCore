// solar-smart-mppt firmware: shared types (no hardware access here, so the host tests use them too)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 1

// Battery chemistry. JP4: open = Li-ion, 1-2 = LiFePO4, 2-3 = LTO 2S.
enum { CHEM_LIION = 0, CHEM_LFP = 1, CHEM_LTO2S = 2, CHEM_COUNT = 3, CHEM_BY_JUMPER = 0xFF };

// NTC on J3
enum { NTC_AUTO = 0, NTC_OFF = 1, NTC_ON = 2 };

// Sign of the INA260 current register (0x41). MeshCore's battery gauge expects discharge positive.
enum { INA_SIGN_DISCHARGE_POSITIVE = 0, INA_SIGN_CHARGE_POSITIVE = 1 };

#define EXPERT_MAX 8

// Settings kept in flash. Change SETTINGS_VERSION when the layout changes (old records are ignored).
#define SETTINGS_VERSION 1
typedef struct {
  uint8_t  chem;            // CHEM_* or CHEM_BY_JUMPER
  uint8_t  chem_jumper;     // what JP4 read when the record was saved (a changed jumper wins)
  uint16_t capacity_mah;    // 0 = unknown (percent from voltage until learnt)
  uint16_t vchg_mv;         // charge voltage of the pack
  uint16_t ichg_ma;         // charge current
  uint16_t iin_ma;          // input (panel) current limit
  uint8_t  mppt_code;       // BQ25798 VOC_PCT 0..7 (56.25 % .. 100 %); MPPT on when vindpm_mv == 0
  uint8_t  ntc_mode;        // NTC_*
  uint16_t vindpm_mv;       // fixed input voltage limit, 0 = MPPT
  uint16_t od_off_mv;       // BMS: discharge FET off below (pack)
  uint16_t od_on_mv;        // ... back on above
  uint16_t ship_mv;         // ship mode below this without sun
  uint16_t j7_off_mv;       // J7 off below (JP5 2-3)
  uint16_t j7_on_mv;        // ... back on above
  uint16_t idsg_max_ma;     // software over-current (discharge), 0 = off
  uint16_t isc_ma;          // short circuit trip (COMP0)
  uint16_t bal_delta_mv;    // LTO: balance when cells differ more than this
  uint16_t bal_start_mv;    // ... and the higher cell is above this
  int16_t  zero_uv;         // shunt offset (SH_P - SH_N at 0 A), from the ZERO command
  uint16_t shunt_uohm;      // 20000 = 20 mOhm (trim for gain)
  uint8_t  ina_sign;        // INA_SIGN_*
  uint8_t  expert_n;        // stored raw BQ25798 writes, applied after our own settings
  uint16_t expert[EXPERT_MAX];   // (reg << 8) | value
} settings_t;

// Charge counter, saved hourly and before ship mode
typedef struct {
  int32_t  charge_mas;      // mA*s in the battery (< 0 = unknown); mAh = charge_mas / 3600
  uint16_t capacity_learnt_mah;
  uint16_t cycles_x10;      // full-equivalent cycles * 10
} gauge_state_t;

// Status flags (register 0x02 on 0x42)
#define ST_CHARGING      0x0001
#define ST_FULL          0x0002
#define ST_SUN           0x0004   // VBUS present
#define ST_NTC_MISSING   0x0008
#define ST_OD_OFF        0x0010   // discharge FET off
#define ST_OC_OFF        0x0020   // charge FET off
#define ST_SHORT         0x0040   // short circuit trip latched
#define ST_UV            0x0080
#define ST_OV            0x0100
#define ST_OVERCURRENT   0x0200
#define ST_BALANCING     0x0400
#define ST_J7_ON         0x0800
#define ST_BQ_FAULT      0x1000
#define ST_BQ_I2C_ERR    0x2000
#define ST_SOC_FROM_V    0x4000   // percent comes from voltage (capacity or charge unknown)
#define ST_SETTINGS_DIRTY 0x8000  // RAM settings differ from flash (send SAVE)

// Live values, written by the main loop, read by the I2C target
typedef struct {
  uint16_t vpack_mv;
  uint16_t vcell1_mv, vcell2_mv;  // 2S only (cell1 = BATT- .. BM)
  int16_t  ibat_ma;               // + = charging
  uint16_t vbus_mv;
  int16_t  ibus_ma;
  int16_t  temp_x10;              // BQ25798 die temperature, 0.1 degC
  uint16_t soc_x100;              // 0..10000
  uint16_t remaining_mah;
  uint16_t status;                // ST_*
  uint8_t  chem;                  // active
  uint8_t  chem_jumper;
  uint8_t  bq_chg_stat;           // REG1C bits 7:5
  uint8_t  bq_fault0, bq_fault1;  // REG20, REG21
  uint16_t last_error;            // ERR_* of the last command
} live_t;

int chem_cells(uint8_t chem);
