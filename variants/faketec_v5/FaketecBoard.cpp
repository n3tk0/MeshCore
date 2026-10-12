#include <Arduino.h>
#include <Wire.h>

#include "FaketecBoard.h"
#include "target.h"

// RESETREAS is sticky across resets: read it before anything else runs, and clear it only in
// begin(), after every early reader (NRF52_POWER_MANAGEMENT has one too, and the order of
// constructors across files is not defined), so the next start reports only its own cause. The UI
// asks board.getResetReason() (0 = power-on, i.e. a battery was put in).
static uint32_t faketec_reset_reason = 0;
static void __attribute__((constructor(101))) faketec_capture_reset_reason() {
  faketec_reset_reason = NRF_POWER->RESETREAS;
}

static void clearResetReason() {
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) sd_power_reset_reason_clr(0xFFFFFFFF);
  else NRF_POWER->RESETREAS = 0xFFFFFFFF;   // write 1s to clear
}

uint32_t FaketecBoard::getResetReason() const { return faketec_reset_reason; }

// Hardware watchdog: a hang (e.g. the I2C driver waiting forever on a stuck bus) would also stop
// the low-battery shutdown, and with no BMS the LiPo would then run flat. The watchdog restarts
// the board instead. It keeps counting while the CPU sleeps between events and stops in system
// off. Once started it cannot be stopped; the bootloader feeds it during a firmware update.
#ifndef WATCHDOG_SECONDS
  #define WATCHDOG_SECONDS 30
#endif

void FaketecBoard::onBootComplete() {
#if WATCHDOG_SECONDS > 0
  if (!(NRF_WDT->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUS_Msk)) {
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) | (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = WATCHDOG_SECONDS * 32768UL - 1;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk;
    NRF_WDT->TASKS_START = 1;
  }
#endif
  NRF52BoardDCDC::onBootComplete();
}

void FaketecBoard::loop() {
  if (NRF_WDT->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUS_Msk) NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

// Wake from hibernate by holding the encoder push (RST is hidden in the case). Not BTN: the
// promicro bootloader treats P1.00 held at reset as its DFU button and would start BLE OTA.
#if defined(UI_HAS_ROTARY_INPUT) && defined(PIN_ENCODER_BTN)
  #define WAKE_BTN_PIN PIN_ENCODER_BTN
  #ifndef WAKE_HOLD_MILLIS
    #define WAKE_HOLD_MILLIS 1000   // hold this long to wake, shorter goes back to sleep
  #endif

static void armButtonWake() {
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[WAKE_BTN_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
}

// Woken from hibernate by the encoder push: boot only after a long hold, so a bump in the pocket goes back to sleep.
static void confirmButtonWake() {
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
    confirmButtonWake();   // may go back to system off: the register then keeps the OFF bit
#endif
    clearResetReason();
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
  #if defined(PIN_ENCODER_BTN)
    encoder_btn.end();  // the push becomes the system-off wake pin below
  #endif
#endif
#ifdef DISPLAY_CLASS
  user_btn.end();
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
