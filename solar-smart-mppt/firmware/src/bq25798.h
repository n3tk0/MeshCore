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
#define BQ_REG_ITERM     0x09   // REG09: ITERM 4:0, 40 mA steps (bit 6 REG_RST, bit 5 STOP_WD_CHG)
#define BQ_REG_NTC0      0x17   // JEITA_ISETC 2:1 (01 = 20 % in TS_COOL, 11 = unchanged)
#define BQ_ISETC_FULL_MAX_MA 500
#define BQ_REG_CTRL0     0x0F   // EN_CHG bit 5, EN_HIZ bit 2, EN_TERM bit 1
#define BQ_REG_CTRL1     0x10   // WD_RST bit 3, WATCHDOG 2:0
#define BQ_REG_CTRL2     0x11   // AUTO_INDET_EN bit 6, SDRV_CTRL 2:1, SDRV_DLY bit 0
#define BQ_REG_CTRL5     0x14   // SFET_PRESENT bit 7 (SDRV_CTRL is locked at 0 without it)
#define BQ_REG_MPPT      0x15   // VOC_PCT 7:5, VOC_DLY 4:3, VOC_RATE 2:1, EN_MPPT 0
#define BQ_REG_NTC1      0x18   // TS_IGNORE bit 0
#define BQ_REG_STAT0     0x1B   // VBUS_PRESENT_STAT bit 0
#define BQ_REG_STAT1     0x1C   // CHG_STAT 7:5
#define BQ_REG_STAT3     0x1E   // VSYS_STAT bit 4 (VBAT < VSYSMIN: the chip takes no EN_MPPT)
#define BQ_REG_FAULT0    0x20
#define BQ_REG_FAULT1    0x21
#define BQ_REG_FLAG0     0x22   // POORSRC_FLAG bit 4 (read to clear: bq_refresh counts it)
#define BQ_REG_ADC_CTRL  0x2E   // ADC_EN bit 7
#define BQ_REG_ADC_DIS0  0x2F   // TS_ADC_DIS bit 2
#define BQ_REG_IBUS      0x31
#define BQ_REG_IBAT      0x33
#define BQ_REG_VBUS      0x35
#define BQ_REG_VBAT      0x3B
#define BQ_REG_TS        0x3F   // 0.0976563 % of REGN
#define BQ_REG_TDIE      0x41   // 0.5 degC
#define BQ_REG_PART      0x48

#define BQ_VOC_PCT        80       // auto VINDPM: % of the panel's open-circuit voltage
#define BQ_VOC_MIN_MV     3000     // a VBUS result under this is not an open-circuit reading
#define BQ_VINDPM_MIN_MV  3600
#define BQ_VINDPM_MAX_MV  22000
#define BQ_HIZ_HOLD_POLLS 2        // EN_HIZ is kept this many applies (1 s each) to get a no-load VBUS result ...
#define BQ_HIZ_HOLD_PERIOD 6       // ... in every this many applies of a poor-source loop
#define BQ_POORSRC_KEEP_MIN 30     // ST2_POOR_SRC for this long after the last one

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
  bool ntc_present;
  bool ship_sent;             // ship mode written: bq_apply leaves SDRV_CTRL alone until the sun is back
  uint8_t errors;             // bus errors in a row
  // poor source / input-voltage diagnostics and the auto VINDPM (MPPT mode, see bq_apply)
  uint16_t poorsrc_count;     // REG22 POORSRC_FLAG seen since start (the read clears it, so counted in bq_refresh)
  uint32_t secs;              // bq_refresh() calls, one a second
  uint32_t poorsrc_secs;      // secs at the last one
  bool unl;                   // at the last refresh: VBUS there, converter not loading it (EN_HIZ / no PG / poor source)
  bool hiz_seen;              // EN_HIZ as read at the last refresh (before bq_apply clears it in r[])
  bool unl_prev;              // ... and at the refresh before
  bool hiz_cleared;           // we cleared EN_HIZ and the chip has not set it again yet: the converter ran
  bool voc_ok;                // VBUS (voc_mv) was read with the converter off for a whole second: open-circuit voltage
  uint16_t voc_mv;
  uint8_t hiz_cnt;            // applies of this drop-out so far (EN_HIZ is held in the first ones)
  uint16_t auto_vin_mv;       // last VINDPM set from Voc, 0 = none
  uint16_t auto_vin_count;
} bq_t;

void bq_init(bq_t *b, const bq_bus_t *bus);
// reads all registers into b->r; false on a bus error
bool bq_refresh(bq_t *b);
// writes our settings where the chip differs (after a chip reset, a watchdog reset or a change);
// returns true when every field matches afterwards
bool bq_apply(bq_t *b, const settings_t *s, uint8_t chem, bool charge_enable);
bool bq_kick_watchdog(bq_t *b);
bool bq_ship(bq_t *b);
// VINDPM for a panel open-circuit voltage: 80 %, rounded down to 100 mV, 3600..22000
uint16_t bq_vindpm_from_voc(uint16_t voc_mv);
// ST2_HIZ / ST2_NO_PG / ST2_POOR_SRC from the last refresh (0 without VBUS)
uint16_t bq_why(const bq_t *b);
// minutes since the last poor source, 0xFFFF = none
uint16_t bq_poorsrc_age_min(const bq_t *b);

uint16_t bq_u16(const bq_t *b, uint8_t reg);
int16_t bq_i16(const bq_t *b, uint8_t reg);
uint16_t bq_ts_x10(const bq_t *b);    // TS in 0.1 % of REGN
bool bq_vbus_present(const bq_t *b);  // VBUS_PRESENT_STAT as last read (not the ADC, which stops at night)
bool bq_sfet(uint8_t chem);           // SFET_PRESENT (ship mode) for this chemistry
uint16_t bq_vsysmin_mv(uint8_t chem);
