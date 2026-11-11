#include "hal.h"
#include "board.h"
#include "bms.h"

static volatile uint32_t ms_now;

void TIMG0_IRQHandler(void) {
  if (DL_TimerG_getPendingInterrupt(TIMG0) == DL_TIMER_IIDX_ZERO) ms_now += 10;
}

uint32_t hal_ms(void) { return ms_now; }

void hal_idle(void) { __WFI(); }

void hal_delay_ms(uint32_t ms) {
  delay_cycles(ms * (MCLK_HZ / 1000u));
}

static const DL_TimerG_ClockConfig tim_clk = {
  .clockSel = DL_TIMER_CLOCK_MFCLK,          // 4 MHz, runs in STOP
  .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
  .prescale = 39,                            // 100 kHz
};
static const DL_TimerG_TimerConfig tim_cfg = {
  .period = 999,                             // 10 ms
  .timerMode = DL_TIMER_TIMER_MODE_PERIODIC,
  .startTimer = DL_TIMER_START,
};

static const DL_I2C_ClockConfig i2c_clk = {
  .clockSel = DL_I2C_CLOCK_BUSCLK,
  .divideRatio = DL_I2C_CLOCK_DIVIDE_1,
};

static void out_pin(uint32_t iomux, uint32_t pin, bool high) {
  DL_GPIO_initDigitalOutput(iomux);
  if (high) DL_GPIO_setPins(GPIOA, pin); else DL_GPIO_clearPins(GPIOA, pin);
  DL_GPIO_enableOutput(GPIOA, pin);
}

void hal_init(void) {
  DL_GPIO_reset(GPIOA);
  DL_TimerG_reset(TIMG0);
  DL_I2C_reset(I2C1);
  DL_GPIO_enablePower(GPIOA);
  DL_TimerG_enablePower(TIMG0);
  DL_I2C_enablePower(I2C1);
  delay_cycles(16);

  DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);
  DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_4M);
  DL_SYSCTL_setMCLKDivider(DL_SYSCTL_MCLK_DIVIDER_DISABLE);
  DL_SYSCTL_enableMFCLK();
#ifdef SMPPT_SLEEP_ONLY
  DL_SYSCTL_setPowerPolicyRUN0SLEEP0();
#else
  DL_SYSCTL_setPowerPolicySTOP0();   // SYSOSC keeps running at 4 MHz: I2C target, TIMG0, COMP stay on
#endif

  // safe state before anything else: FETs as the hardware pulls them, no charging, J7 off
  out_pin(PIN_OD_IOMUX, PIN_OD, false);
  out_pin(PIN_OC_G_IOMUX, PIN_OC_G, true);   // open drain released = charge FET on
  out_pin(PIN_CE_IOMUX, PIN_CE, true);       // ~CE high = no charging until the BQ is set up
  out_pin(PIN_J7_IOMUX, PIN_J7, false);
  out_pin(PIN_BAL1_IOMUX, PIN_BAL1, false);
  out_pin(PIN_BAL2_IOMUX, PIN_BAL2, false);
  DL_GPIO_initDigitalOutput(PIN_LED_IOMUX);
  DL_GPIO_disableOutput(GPIOA, PIN_LED);     // Hi-Z = both LEDs off

  // I2C1 controller, 100 kHz from 4 MHz: SCL = clk / (10 * (TPR + 1))
  DL_GPIO_initPeripheralInputFunctionFeatures(PIN_SDA_I_IOMUX, PIN_SDA_I_FUNC,
      DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  DL_GPIO_initPeripheralInputFunctionFeatures(PIN_SCL_I_IOMUX, PIN_SCL_I_FUNC,
      DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  DL_GPIO_enableHiZ(PIN_SDA_I_IOMUX);
  DL_GPIO_enableHiZ(PIN_SCL_I_IOMUX);
  DL_I2C_setClockConfig(I2C1, (DL_I2C_ClockConfig *)&i2c_clk);
  DL_I2C_disableAnalogGlitchFilter(I2C1);
  DL_I2C_resetControllerTransfer(I2C1);
  DL_I2C_setTimerPeriod(I2C1, 3);
  DL_I2C_setControllerTXFIFOThreshold(I2C1, DL_I2C_TX_FIFO_LEVEL_EMPTY);
  DL_I2C_setControllerRXFIFOThreshold(I2C1, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
  DL_I2C_enableControllerClockStretching(I2C1);
  DL_I2C_enableController(I2C1);

  DL_TimerG_setClockConfig(TIMG0, (DL_TimerG_ClockConfig *)&tim_clk);
  DL_TimerG_initTimerMode(TIMG0, (DL_TimerG_TimerConfig *)&tim_cfg);
  DL_TimerG_enableInterrupt(TIMG0, DL_TIMERG_INTERRUPT_ZERO_EVENT);
  DL_TimerG_enableClock(TIMG0);
  NVIC_EnableIRQ(TIMG0_INT_IRQn);
}

void hal_od_off(bool off) { if (off) DL_GPIO_setPins(GPIOA, PIN_OD); else DL_GPIO_clearPins(GPIOA, PIN_OD); }
void hal_oc_off(bool off) { if (off) DL_GPIO_clearPins(GPIOA, PIN_OC_G); else DL_GPIO_setPins(GPIOA, PIN_OC_G); }
void hal_charge(bool on) { if (on) DL_GPIO_clearPins(GPIOA, PIN_CE); else DL_GPIO_setPins(GPIOA, PIN_CE); }
void hal_j7(bool on) { if (on) DL_GPIO_setPins(GPIOA, PIN_J7); else DL_GPIO_clearPins(GPIOA, PIN_J7); }

void hal_bal(bool b1, bool b2) {
  if (b1 && b2) b1 = b2 = false;   // never both
  if (b1) DL_GPIO_setPins(GPIOA, PIN_BAL1); else DL_GPIO_clearPins(GPIOA, PIN_BAL1);
  if (b2) DL_GPIO_setPins(GPIOA, PIN_BAL2); else DL_GPIO_clearPins(GPIOA, PIN_BAL2);
}

void hal_led(uint8_t led) {
  if (led == LED_OFF) { DL_GPIO_disableOutput(GPIOA, PIN_LED); return; }
  if (led == LED_RED) DL_GPIO_setPins(GPIOA, PIN_LED); else DL_GPIO_clearPins(GPIOA, PIN_LED);
  DL_GPIO_enableOutput(GPIOA, PIN_LED);
}

uint8_t hal_read_chem_jumper(void) {
  DL_GPIO_initDigitalInputFeatures(PIN_CHEM_IOMUX, DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
      DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  hal_delay_ms(1);
  bool up = DL_GPIO_readPins(GPIOA, PIN_CHEM) != 0;
  DL_GPIO_initDigitalInputFeatures(PIN_CHEM_IOMUX, DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_DOWN,
      DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  hal_delay_ms(1);
  bool down = DL_GPIO_readPins(GPIOA, PIN_CHEM) != 0;
  // leave it pulled down: an open jumper then draws nothing
  if (up && !down) return CHEM_LIION;   // open: follows the pull
  if (!up && !down) return CHEM_LFP;    // 1-2 to GND
  if (up && down) return CHEM_LTO2S;    // 2-3 to 3V3
  return CHEM_LFP;                      // can't happen; LiFePO4 has the lowest charge voltage
}

// ---------------- I2C1 controller with timeouts ----------------

#define I2C_TIMEOUT 20000u   // loop passes, a few ms at 4 MHz

static bool wait_idle(void) {
  for (uint32_t i = 0; i < I2C_TIMEOUT; i++)
    if (DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_IDLE) return true;
  return false;
}

static bool wait_done(void) {
  for (uint32_t i = 0; i < I2C_TIMEOUT; i++)
    if (!(DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_BUSY)) {
      return !(DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_ERROR);
    }
  return false;
}

static void i2c_recover(void) {
  DL_I2C_resetControllerTransfer(I2C1);
  DL_I2C_flushControllerTXFIFO(I2C1);
  DL_I2C_flushControllerRXFIFO(I2C1);
  DL_I2C_disableController(I2C1);
  DL_I2C_enableController(I2C1);
}

static bool bq_write(uint8_t reg, const uint8_t *buf, uint8_t n) {
  uint8_t d[8];
  if (n > 7 || !wait_idle()) { i2c_recover(); return false; }
  d[0] = reg;
  for (uint8_t i = 0; i < n; i++) d[i + 1] = buf[i];
  DL_I2C_flushControllerTXFIFO(I2C1);
  DL_I2C_fillControllerTXFIFO(I2C1, d, (uint16_t)(n + 1));   // FIFO holds 8 bytes
  DL_I2C_startControllerTransfer(I2C1, BQ_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, (uint16_t)(n + 1));
  if (!wait_done()) { i2c_recover(); return false; }
  return true;
}

static bool bq_read(uint8_t reg, uint8_t *buf, uint8_t n) {
  if (!bq_write(reg, 0, 0)) return false;
  if (!wait_idle()) { i2c_recover(); return false; }
  DL_I2C_startControllerTransfer(I2C1, BQ_ADDR, DL_I2C_CONTROLLER_DIRECTION_RX, n);
  for (uint8_t i = 0; i < n; i++) {
    uint32_t t = 0;
    while (DL_I2C_isControllerRXFIFOEmpty(I2C1)) {
      if (++t > I2C_TIMEOUT || (DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_ERROR)) {
        i2c_recover();
        return false;
      }
    }
    buf[i] = DL_I2C_receiveControllerData(I2C1);
  }
  return wait_done();
}

const bq_bus_t hal_bq_bus = { bq_read, bq_write };

// ---------------- flash ----------------

// One flash bank: while it erases or programs, nothing may be fetched from it, and the vector table
// and the ISRs live there. So interrupts stay off for each command (the command loop runs from RAM).
// An erase holds them off for a few ms: the I2C target stretches the clock meanwhile.
static bool fl_erase(uint32_t addr) {
  uint32_t pm = __get_PRIMASK();
  __disable_irq();
  DL_FlashCTL_executeClearStatus(FLASHCTL);
  DL_FlashCTL_unprotectSector(FLASHCTL, addr, DL_FLASHCTL_REGION_SELECT_MAIN);
  bool ok = DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL, addr, DL_FLASHCTL_COMMAND_SIZE_SECTOR) ==
            DL_FLASHCTL_COMMAND_STATUS_PASSED;
  __set_PRIMASK(pm);
  return ok;
}

static bool fl_program(uint32_t addr, const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *)data;
  for (uint32_t off = 0; off < len; off += 8) {
    uint32_t w[2];
    __builtin_memcpy(w, p + off, 8);
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, addr + off, DL_FLASHCTL_REGION_SELECT_MAIN);
    bool ok = DL_FlashCTL_programMemoryFromRAM64(FLASHCTL, addr + off, w) == DL_FLASHCTL_COMMAND_STATUS_PASSED;
    __set_PRIMASK(pm);
    if (!ok) return false;
  }
  return true;
}

static const uint8_t *fl_ptr(uint32_t addr) { return (const uint8_t *)addr; }

const flash_ops_t hal_flash = { fl_erase, fl_program, fl_ptr, STORE_BASE };

void hal_reboot(void) { DL_SYSCTL_resetDevice(DL_SYSCTL_RESET_SYSRST); }
void hal_enter_bsl(void) { DL_SYSCTL_resetDevice(DL_SYSCTL_RESET_BOOTLOADER_ENTRY); }
