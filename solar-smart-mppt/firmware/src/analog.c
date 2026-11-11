// Shunt amplifiers, ADC and the short-circuit comparator.
//
//  SH_P (PA25) -> OPA0 x16 -> ADC ch12 and COMP0 IN-   (output pin PA22 stays off)
//  SH_N (PA18) -> OPA1 x16 -> ADC ch13                 (output pin PA16 is I2C1 SDA: stays off)
//  VBAT PA24 A3, VBM PA21 A5; ADC reference: internal VREF 2.5 V
//
// Discharge pulls SH_P below SH_N. COMP0 trips when OPA0 falls below its DAC, which is set from the
// latest SH_N reading minus Isc * R * 16. The shunt bias is only ~33 mV, so with the v0.3 parts
// SH_P reaches 0 V at about 1.6 A of discharge: the trip point and the discharge reading end there.
#include "hal.h"
#include "board.h"

#define COMP_IMSEL_OPA0_OUT DL_COMP_IMSEL_CHANNEL_6   // datasheet: COMP0 IN- channel 6 = OPA0 output

static volatile bool short_flag;

static const DL_OPA_Config opa_cfg = {
  .pselChannel = DL_OPA_PSEL_IN0_POS,
  .nselChannel = DL_OPA_NSEL_RTAP,
  .mselChannel = DL_OPA_MSEL_GND,
  .gain = DL_OPA_GAIN_N15_P16,
  .outputPinState = DL_OPA_OUTPUT_PIN_DISABLED,
  .choppingMode = DL_OPA_CHOPPING_MODE_DISABLE,   // COMP0 sees the output: no chopping ripple
};

static const DL_VREF_ClockConfig vref_clk = { .clockSel = DL_VREF_CLOCK_BUSCLK, .divideRatio = DL_VREF_CLOCK_DIVIDE_1 };
static const DL_VREF_Config vref_cfg = {
  .vrefEnable = DL_VREF_ENABLE_ENABLE,
  .bufConfig = DL_VREF_BUFCONFIG_OUTPUT_2_5V,
  .shModeEnable = DL_VREF_SHMODE_DISABLE,
  .holdCycleCount = DL_VREF_HOLD_MIN,
  .shCycleCount = DL_VREF_SH_MIN,
};

static const DL_ADC12_ClockConfig adc_clk = {
  .clockSel = DL_ADC12_CLOCK_ULPCLK,
  .divideRatio = DL_ADC12_CLOCK_DIVIDE_1,
  .freqRange = DL_ADC12_CLOCK_FREQ_RANGE_1_TO_4,
};

static const DL_COMP_Config comp_cfg = {
  .channelEnable = DL_COMP_ENABLE_CHANNEL_NEG,
  .mode = DL_COMP_MODE_FAST,
  .negChannel = COMP_IMSEL_OPA0_OUT,
  .posChannel = DL_COMP_IPSEL_CHANNEL_0,
  .hysteresis = DL_COMP_HYSTERESIS_10,
  .polarity = DL_COMP_POLARITY_NON_INV,          // high when DAC (+) > OPA0 (-) = short
};
static const DL_COMP_RefVoltageConfig comp_ref = {
  .mode = DL_COMP_REF_MODE_STATIC,
  .source = DL_COMP_REF_SOURCE_INT_VREF_DAC,
  .terminalSelect = DL_COMP_REF_TERMINAL_SELECT_POS,
  .controlSelect = DL_COMP_DAC_CONTROL_SW,
  .inputSelect = DL_COMP_DAC_INPUT_DACCODE0,
};

static void mem(uint32_t idx, uint32_t ch) {
  DL_ADC12_configConversionMem(ADC0, idx, ch, DL_ADC12_REFERENCE_VOLTAGE_INTREF,
      DL_ADC12_SAMPLE_TIMER_SOURCE_SCOMP0, DL_ADC12_AVERAGING_MODE_ENABLED,
      DL_ADC12_BURN_OUT_SOURCE_DISABLED, DL_ADC12_TRIGGER_MODE_AUTO_NEXT, DL_ADC12_WINDOWS_COMP_MODE_DISABLED);
}

void analog_init(void) {
  DL_VREF_reset(VREF); DL_OPA_reset(OPA0); DL_OPA_reset(OPA1); DL_ADC12_reset(ADC0); DL_COMP_reset(COMP0);
  DL_VREF_enablePower(VREF); DL_OPA_enablePower(OPA0); DL_OPA_enablePower(OPA1);
  DL_ADC12_enablePower(ADC0); DL_COMP_enablePower(COMP0);
  delay_cycles(16);

  DL_VREF_setClockConfig(VREF, (DL_VREF_ClockConfig *)&vref_clk);
  DL_VREF_configReference(VREF, (DL_VREF_Config *)&vref_cfg);

  DL_OPA_init(OPA0, (DL_OPA_Config *)&opa_cfg);
  DL_OPA_init(OPA1, (DL_OPA_Config *)&opa_cfg);
  DL_OPA_setGainBandwidth(OPA0, DL_OPA_GBW_LOW);
  DL_OPA_setGainBandwidth(OPA1, DL_OPA_GBW_LOW);
  DL_OPA_enable(OPA0);
  DL_OPA_enable(OPA1);

  DL_ADC12_setClockConfig(ADC0, (DL_ADC12_ClockConfig *)&adc_clk);
  DL_ADC12_initSeqSample(ADC0, DL_ADC12_REPEAT_MODE_DISABLED, DL_ADC12_SAMPLING_SOURCE_AUTO,
      DL_ADC12_TRIG_SRC_SOFTWARE, DL_ADC12_SEQ_START_ADDR_00, DL_ADC12_SEQ_END_ADDR_03,
      DL_ADC12_SAMP_CONV_RES_12_BIT, DL_ADC12_SAMP_CONV_DATA_FORMAT_UNSIGNED);
  mem(DL_ADC12_MEM_IDX_0, ADC_CH_OPA0);
  mem(DL_ADC12_MEM_IDX_1, ADC_CH_OPA1);
  mem(DL_ADC12_MEM_IDX_2, ADC_CH_VBAT);
  mem(DL_ADC12_MEM_IDX_3, ADC_CH_VBM);
  DL_ADC12_configHwAverage(ADC0, DL_ADC12_HW_AVG_NUM_ACC_16, DL_ADC12_HW_AVG_DEN_DIV_BY_16);
  DL_ADC12_setPowerDownMode(ADC0, DL_ADC12_POWER_DOWN_MODE_AUTO);
  DL_ADC12_setSampleTime0(ADC0, 40);   // 10 us at 4 MHz; the dividers have 100 nF on the pin

  DL_COMP_init(COMP0, (DL_COMP_Config *)&comp_cfg);
  DL_COMP_refVoltageInit(COMP0, (DL_COMP_RefVoltageConfig *)&comp_ref);
  DL_COMP_setDACCode0(COMP0, 0);       // 0 V: never trips until the first reading sets it
  DL_COMP_clearInterruptStatus(COMP0, DL_COMP_INTERRUPT_OUTPUT_EDGE);
  DL_COMP_enableInterrupt(COMP0, DL_COMP_INTERRUPT_OUTPUT_EDGE);
  DL_COMP_enable(COMP0);
  NVIC_EnableIRQ(COMP0_INT_IRQn);
}

// GPIOA and COMP0 share this vector
void GROUP1_IRQHandler(void) {
  if (DL_COMP_getPendingInterrupt(COMP0) == DL_COMP_IIDX_OUTPUT_EDGE) {
    DL_GPIO_setPins(GPIOA, PIN_OD);    // discharge FET off first, then tell the main loop
    short_flag = true;
  }
}

bool analog_short_tripped(void) {
  bool f = short_flag;
  short_flag = false;
  return f;
}

static uint16_t to_mv(uint16_t raw) { return (uint16_t)((uint32_t)raw * VREF_MV / 4096u); }

void analog_read(analog_t *a) {
  DL_ADC12_clearInterruptStatus(ADC0, DL_ADC12_INTERRUPT_MEM3_RESULT_LOADED);
  DL_ADC12_enableConversions(ADC0);
  DL_ADC12_startConversion(ADC0);
  for (uint32_t t = 0; t < 20000u; t++)
    if (DL_ADC12_getRawInterruptStatus(ADC0, DL_ADC12_INTERRUPT_MEM3_RESULT_LOADED)) break;
  uint16_t p = to_mv(DL_ADC12_getMemResult(ADC0, DL_ADC12_MEM_IDX_0));
  uint16_t n = to_mv(DL_ADC12_getMemResult(ADC0, DL_ADC12_MEM_IDX_1));
  uint16_t vb = to_mv(DL_ADC12_getMemResult(ADC0, DL_ADC12_MEM_IDX_2));
  uint16_t vm = to_mv(DL_ADC12_getMemResult(ADC0, DL_ADC12_MEM_IDX_3));
  a->shunt_uv = ((int32_t)p - (int32_t)n) * 1000 / OPA_GAIN;
  a->sh_n_mv = (uint16_t)(n / OPA_GAIN);
  a->vbat_mv = (uint16_t)((uint32_t)vb * VBAT_DIV_NUM / VBAT_DIV_DEN);
  a->vbm_mv = (uint16_t)(vm * VBM_DIV);
}

void analog_set_short(uint16_t isc_ma, uint16_t shunt_uohm, uint16_t sh_n_mv) {
  // trip when SH_P < SH_N - Isc*R, seen through the x16 OPA0
  int32_t drop_uv = (int32_t)isc_ma * shunt_uohm / 1000;          // mA * uOhm / 1000 = uV
  int32_t thr_mv = ((int32_t)sh_n_mv * 1000 - drop_uv) * OPA_GAIN / 1000;
  if (thr_mv < 0) thr_mv = 0;                                      // beyond the bias: can't see it
  uint32_t code = (uint32_t)thr_mv * 256u / VREF_MV;
  DL_COMP_setDACCode0(COMP0, (uint8_t)(code > 255 ? 255 : code));
}
