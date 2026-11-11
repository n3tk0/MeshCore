// solar-smart-mppt v0.3 pins (MSPM0L1306SRGER, VQFN-24). IOMUX index = PINCM(n) = PA(n-1).
#pragma once
#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>

// I2C0 target on J5 (INA260 0x41 + our map 0x42), 3.3 V bus, pull-ups on the repeater
#define PIN_SDA_E_IOMUX  IOMUX_PINCM11     // PA10
#define PIN_SDA_E_FUNC   IOMUX_PINCM11_PF_I2C0_SDA
#define PIN_SCL_E_IOMUX  IOMUX_PINCM12     // PA11
#define PIN_SCL_E_FUNC   IOMUX_PINCM12_PF_I2C0_SCL

// I2C1 controller to the BQ25798 (R13/R14 4.7k)
#define PIN_SCL_I_IOMUX  IOMUX_PINCM16     // PA15
#define PIN_SCL_I_FUNC   IOMUX_PINCM16_PF_I2C1_SCL
#define PIN_SDA_I_IOMUX  IOMUX_PINCM17     // PA16 (also the OPA1 output pin: keep that off)
#define PIN_SDA_I_FUNC   IOMUX_PINCM17_PF_I2C1_SDA

// digital
#define PIN_OC_G      DL_GPIO_PIN_0        // open drain, R20 pull-up: low = charge FET off
#define PIN_OC_G_IOMUX IOMUX_PINCM1
#define PIN_BAL1      DL_GPIO_PIN_2        // cell 1 bleed (Q7)
#define PIN_BAL1_IOMUX IOMUX_PINCM3
#define PIN_BAL2      DL_GPIO_PIN_3        // cell 2 bleed (Q10 -> Q8)
#define PIN_BAL2_IOMUX IOMUX_PINCM4
#define PIN_J7        DL_GPIO_PIN_4        // REG_CTL, J7 EN through JP5 2-3
#define PIN_J7_IOMUX  IOMUX_PINCM5
#define PIN_LED       DL_GPIO_PIN_9        // high = red D6, low = yellow D7, Hi-Z = off
#define PIN_LED_IOMUX IOMUX_PINCM10
#define PIN_CE        DL_GPIO_PIN_17       // ~CE: high = no charging (R4 pull-up)
#define PIN_CE_IOMUX  IOMUX_PINCM18
#define PIN_CHEM      DL_GPIO_PIN_23       // JP4: open = follows the pull, 1-2 = 0, 2-3 = 1
#define PIN_CHEM_IOMUX IOMUX_PINCM24
#define PIN_OD        DL_GPIO_PIN_26       // OD_CTL high = discharge FET off (Q6)
#define PIN_OD_IOMUX  IOMUX_PINCM27

// analog: SH_P PA25 -> OPA0 IN0+, SH_N PA18 -> OPA1 IN0+, VBAT PA24 = A3 (1M/470k), VBM PA21 = A5 (1M/1M)
#define ADC_CH_OPA0   DL_ADC12_INPUT_CHAN_12
#define ADC_CH_OPA1   DL_ADC12_INPUT_CHAN_13
#define ADC_CH_VBAT   DL_ADC12_INPUT_CHAN_3
#define ADC_CH_VBM    DL_ADC12_INPUT_CHAN_5

#define VBAT_DIV_NUM  1470u                // (1M + 470k) / 470k
#define VBAT_DIV_DEN  470u
#define VBM_DIV       2u                   // 1M / 1M
#define OPA_GAIN      16
#define VREF_MV       2500u

// settings store: two 1 KB sectors at the end of the 64 KB flash (the linker stops before them)
#define STORE_BASE    0x0000F000u

#define MCLK_HZ       4000000u             // SYSOSC at 4 MHz: enough for 100/400 kHz I2C, low current
