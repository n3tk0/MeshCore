#include <Arduino.h>
#include <Wire.h>

#include "FaketecBoard.h"
#include "target.h"

// Wake from hibernate by holding the encoder push (RST is hidden in the case). Not BTN: the
// promicro bootloader treats P1.00 held at reset as its DFU button and would start BLE OTA.
#if defined(UI_HAS_ROTARY_INPUT) && defined(PIN_ENCODER_BTN)
  #define WAKE_BTN_PIN PIN_ENCODER_BTN
  #ifndef WAKE_HOLD_MILLIS
    #define WAKE_HOLD_MILLIS 1000   // hold this long to wake, shorter goes back to sleep
  #endif

// RESETREAS is sticky across resets, so capture it before anything else runs
static uint32_t faketec_reset_reason = 0;
static void __attribute__((constructor(101))) faketec_capture_reset_reason() {
  faketec_reset_reason = NRF_POWER->RESETREAS;
}

static void armButtonWake() {
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[WAKE_BTN_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
}

// Woken from hibernate by the encoder push: boot only after a long hold, so a bump in the pocket goes back to sleep.
static void confirmButtonWake() {
  NRF_POWER->RESETREAS = 0xFFFFFFFF;   // write 1s to clear, a later reboot must not look like a wake
  if (!(faketec_reset_reason & POWER_RESETREAS_OFF_Msk)) return;   // RST, power-up, watchdog...

  pinMode(WAKE_BTN_PIN, INPUT_PULLUP);
  delay(5);
  unsigned long start = millis();
  while (digitalRead(WAKE_BTN_PIN) == LOW && millis() - start < WAKE_HOLD_MILLIS) delay(10);
  if (millis() - start < WAKE_HOLD_MILLIS) {   // released too early: back to system off
    while (digitalRead(WAKE_BTN_PIN) == LOW) delay(10);
    armButtonWake();
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;   // SoftDevice is not running yet
    NVIC_SystemReset();
  }
#ifdef PIN_VIBRATION
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, HIGH);   // short buzz: accepted, let go
  delay(80);
  digitalWrite(PIN_VIBRATION, LOW);
#endif
  start = millis();
  while (digitalRead(WAKE_BTN_PIN) == LOW && millis() - start < 5000) delay(10);   // don't let the UI see this hold
}
#endif

void FaketecBoard::begin() {    
    NRF52Board::begin();
#ifdef WAKE_BTN_PIN
    confirmButtonWake();
#endif
    btn_prev_state = HIGH;
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();

    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up
}

void FaketecBoard::powerOff() {
#if defined(UI_HAS_ROTARY_INPUT)
  rotary_input.end();   // no pull-up current through a closed encoder contact
#endif
#ifdef PIN_VIBRATION
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, LOW);   // motor off through system off
#endif
#ifdef WAKE_BTN_PIN
  armButtonWake();
#endif
  NRF52Board::powerOff();
}
