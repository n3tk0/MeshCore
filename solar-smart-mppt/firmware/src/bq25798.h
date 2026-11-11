// BQ25798 on I2C1 (0x6B). Register numbers and fields from the TI datasheet (SLUSE22).
// The driver only talks through bq_bus_t so the host tests run it against a fake chip.
#pragma once
#include "smppt.h"

#define BQ_ADDR 0x6B
#define BQ_NREGS 0x49

#define BQ_REG_VSYSMIN   0x00
#define BQ_REG_VREG      0x01   // 16 bit, 10 mV
#define BQ_REG_ICHG      0x03   // 16 bit, 10 mA
#define BQ_REG_VINDPM    0x05   // 100 mV
#define BQ_REG_IINDPM    0x06   // 16 bit, 10 mA
#define BQ_REG_CTRL0     0x0F   // EN_CHG bit 5, EN_HIZ bit 2, EN_TERM bit 1
#define BQ_REG_CTRL1     0x10   // WD_RST bit 3, WATCHDOG 2:0
#define BQ_REG_CTRL2     0x11   // SDRV_CTRL 2:1, SDRV_DLY bit 0
#define BQ_REG_MPPT      0x15   // VOC_PCT 7:5, VOC_DLY 4:3, VOC_RATE 2:1, EN_MPPT 0
#define BQ_REG_NTC1      0x18   // TS_IGNORE bit 0
#define BQ_REG_STAT1     0x1C   // CHG_STAT 7:5
#define BQ_REG_FAULT0    0x20
#define BQ_REG_FAULT1    0x21
#define BQ_REG_ADC_CTRL  0x2E   // ADC_EN bit 7
#define BQ_REG_IBUS      0x31
#define BQ_REG_IBAT      0x33
#define BQ_REG_VBUS      0x35
#define BQ_REG_VBAT      0x3B
#define BQ_REG_TS        0x3F   // 0.0976563 % of REGN
#define BQ_REG_TDIE      0x41   // 0.5 degC
#define BQ_REG_PART      0x48

#define BQ_CHG_DONE 7
#define BQ_TS_OPEN_X10 840      // TS above 84.0 % of REGN = no NTC (open: R2/R3 give ~85.5 %)

typedef struct {
  // read/write n consecutive registers from reg; return false on a bus error
  bool (*read)(uint8_t reg, uint8_t *buf, uint8_t n);
  bool (*write)(uint8_t reg, const uint8_t *buf, uint8_t n);
} bq_bus_t;

typedef struct {
  const bq_bus_t *bus;
  uint8_t r[BQ_NREGS];        // last read of every register
  bool ok;                    // last refresh worked
  bool configured;            // our settings are in the chip
  bool ntc_present;
  uint8_t errors;             // bus errors in a row
} bq_t;

void bq_init(bq_t *b, const bq_bus_t *bus);
// reads all registers into b->r; false on a bus error
bool bq_refresh(bq_t *b);
// writes our settings where the chip differs (after a chip reset, a watchdog reset or a change);
// returns true when every field matches afterwards
bool bq_apply(bq_t *b, const settings_t *s, uint8_t chem, bool charge_enable);
bool bq_kick_watchdog(bq_t *b);
bool bq_ship(bq_t *b);

uint16_t bq_u16(const bq_t *b, uint8_t reg);
int16_t bq_i16(const bq_t *b, uint8_t reg);
uint16_t bq_ts_x10(const bq_t *b);    // TS in 0.1 % of REGN
uint16_t bq_vsysmin_mv(uint8_t chem);
