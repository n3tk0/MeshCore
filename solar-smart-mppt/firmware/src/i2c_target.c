// I2C0 target on J5: own address 0x41 (INA260), alternate address 0x42 (our map).
// The TX FIFO gets one byte at a time when it runs empty, so at most one byte is prefetched;
// i2cproto_end() is told about a byte left unsent so the 0x42 pointer stays right.
#include "hal.h"
#include "board.h"
#include "regs.h"

static bool active, dir_known;
static uint8_t addr;

void i2c_target_init(void) {
  DL_I2C_reset(I2C0);
  DL_I2C_enablePower(I2C0);
  delay_cycles(16);
  DL_GPIO_initPeripheralInputFunctionFeatures(PIN_SDA_E_IOMUX, PIN_SDA_E_FUNC,
      DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  DL_GPIO_initPeripheralInputFunctionFeatures(PIN_SCL_E_IOMUX, PIN_SCL_E_FUNC,
      DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
  DL_GPIO_enableHiZ(PIN_SDA_E_IOMUX);
  DL_GPIO_enableHiZ(PIN_SCL_E_IOMUX);

  static const DL_I2C_ClockConfig clk = { .clockSel = DL_I2C_CLOCK_BUSCLK, .divideRatio = DL_I2C_CLOCK_DIVIDE_1 };
  DL_I2C_setClockConfig(I2C0, (DL_I2C_ClockConfig *)&clk);
  DL_I2C_enableAnalogGlitchFilter(I2C0);
  DL_I2C_setTargetOwnAddress(I2C0, ADDR_INA260);
  DL_I2C_setTargetOwnAddressAlternate(I2C0, ADDR_SMPPT);
  DL_I2C_enableTargetOwnAddressAlternate(I2C0);
  DL_I2C_setTargetTXFIFOThreshold(I2C0, DL_I2C_TX_FIFO_LEVEL_EMPTY);
  DL_I2C_setTargetRXFIFOThreshold(I2C0, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
  DL_I2C_enableTargetTXEmptyOnTXRequest(I2C0);
  DL_I2C_enableTargetClockStretching(I2C0);
  DL_I2C_disableTargetWakeup(I2C0);   // errata I2C_ERR_04
  DL_I2C_enableInterrupt(I2C0, DL_I2C_INTERRUPT_TARGET_START | DL_I2C_INTERRUPT_TARGET_STOP |
                               DL_I2C_INTERRUPT_TARGET_RXFIFO_TRIGGER | DL_I2C_INTERRUPT_TARGET_TXFIFO_TRIGGER);
  DL_I2C_enableTarget(I2C0);
  NVIC_EnableIRQ(I2C0_INT_IRQn);
}

static void finish(void) {
  if (!active) return;
  uint8_t unsent = DL_I2C_isTargetTXFIFOEmpty(I2C0) ? 0 : 1;
  DL_I2C_flushTargetTXFIFO(I2C0);
  if (dir_known) i2cproto_end(unsent);
  active = false;
  dir_known = false;
}

static void begin(bool read) {
  if (dir_known) return;
  addr = (uint8_t)DL_I2C_getTargetAddressMatch(I2C0);
  if (addr != ADDR_SMPPT) addr = ADDR_INA260;
  i2cproto_start(addr, read);
  dir_known = true;
}

void I2C0_IRQHandler(void) {
  switch (DL_I2C_getPendingInterrupt(I2C0)) {
    case DL_I2C_IIDX_TARGET_START:
      // write <reg>, repeated START, read: the register byte may still sit in the RX FIFO
      while (!DL_I2C_isTargetRXFIFOEmpty(I2C0)) { begin(false); i2cproto_rx(DL_I2C_receiveTargetData(I2C0)); }
      finish();                     // a repeated START ends the previous part
      active = true;
      dir_known = false;
      break;
    case DL_I2C_IIDX_TARGET_RXFIFO_TRIGGER:
      begin(false);
      while (!DL_I2C_isTargetRXFIFOEmpty(I2C0)) i2cproto_rx(DL_I2C_receiveTargetData(I2C0));
      break;
    case DL_I2C_IIDX_TARGET_TXFIFO_TRIGGER:
      begin(true);
      if (DL_I2C_isTargetTXFIFOEmpty(I2C0)) DL_I2C_transmitTargetData(I2C0, i2cproto_tx());
      break;
    case DL_I2C_IIDX_TARGET_STOP:
      while (!DL_I2C_isTargetRXFIFOEmpty(I2C0)) { begin(false); i2cproto_rx(DL_I2C_receiveTargetData(I2C0)); }
      finish();
      break;
    default:
      break;
  }
}
