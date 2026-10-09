/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 *
 * Faketec v5 (Nice!Nano v2 / ProMicro nRF52840 + SX1262 module) with
 * SSD1306 I2C OLED and a quadrature rotary encoder on the Nice!Nano
 * inner pads P1.01 / P1.02 / P1.07.
 */

 #pragma once

 #include "WVariant.h"

 ////////////////////////////////////////////////////////////////////////////////
 // Low frequency clock source

#define VARIANT_MCK       (64000000ul)

//#define USE_LFXO      // 32.768 kHz crystal oscillator
#define USE_LFRC    // 32.768 kHz RC oscillator

////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_EXT_VCC          (21)
#define EXT_VCC              (PIN_EXT_VCC)

#define BATTERY_PIN          (17)
#define ADC_RESOLUTION       12

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT           (24)
#define NUM_DIGITAL_PINS     (24)
#define NUM_ANALOG_INPUTS    (3)
#define NUM_ANALOG_OUTPUTS   (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_TX       (3)   // P0.20, GPS header (P0.06/P0.08 drive the T3/T2 MOSFET gates)
#define PIN_SERIAL1_RX       (4)   // P0.22, GPS header

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA         (6)
#define PIN_WIRE_SCL         (7)
#define PIN_WIRE1_SDA        (13)
#define PIN_WIRE1_SCL        (14)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_SCK          (2)
#define PIN_SPI_MISO         (3)
#define PIN_SPI_MOSI         (4)

#define PIN_SPI_NSS          (5)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED              (22)
#define LED_PIN              PIN_LED
#define LED_BLUE             PIN_LED
#define LED_BUILTIN          PIN_LED
#define LED_STATE_ON         1

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1          (6)
#define BUTTON_PIN           PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// Rotary encoder (A/B to GND, push switch to GND, internal pull-ups)

#define PIN_ENCODER_A        (18)  // P1.01
#define PIN_ENCODER_B        (19)  // P1.02
#define PIN_ENCODER_BTN      (23)  // P1.07

// Vibration motor: PIN_VIBRATION is set from platformio.ini (default 0 = P0.08 = T2G,
// gate of the on-board T2 MOSFET footprint). Other Faketec MOSFET gates: T1G = P0.24
// (shared with PIN_GPS_EN), T3G = P0.06.

//////////////////////////////////////////////////////////////////////////////
// LoRa

#define P_LORA_NSS                (13)
#define P_LORA_DIO_1              (11)
#define P_LORA_RESET              (10)
#define P_LORA_BUSY               (16)
#define P_LORA_MISO               (15)
#define P_LORA_SCLK               (12)
#define P_LORA_MOSI               (14)
#define SX126X_POWER_EN           (21)
#define SX126X_RXEN               (2)
#define SX126X_TXEN               (-1)
#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE  (1.8f)
