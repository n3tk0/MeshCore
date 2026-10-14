#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

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

static void feedWatchdog() {
  if (NRF_WDT->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUS_Msk) NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

void FaketecBoard::loop() {
  feedWatchdog();
}

static bool softDeviceEnabled() {
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  return sd_enabled != 0;
}

static void enterSystemOff() {
  if (softDeviceEnabled()) sd_power_system_off();   // does not return when it works
  else NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
  NVIC_SystemReset();   // only reached under a debugger (it emulates system off)
}

// ---- power off / wake for the encoder UI: Back (SW2, P1.00) + encoder push held together
//
// System off has SW2 as its only wake pin: it has the external 10k pull-up R7, so arming it costs
// nothing. The encoder has only the internal ~13k pull-ups, and the AN-P1S can stop with A or B
// closed to GND (~250 uA per contact), so all three encoder pins go into system off with no pull.
// A wake on battery starts the firmware only if Back and the push are then held together for
// WAKE_HOLD_MILLIS; anything shorter (a bump in a pocket) goes straight back to system off without
// starting the screen or the radio. With USB power it starts at once.
//
// The switched 3.3 V rail (SX126X_POWER_EN, P0.13) feeds the radio, the OLED and, on the Faketec,
// R7. The rail is kept powered (HIGH) through system off, while the SX1262 is put into cold
// sleep (~0.6 uA) and the OLED is off (0xAE, ~1 uA). Keeping the rail powered guarantees R7
// pulls SW2 (P1.00) solidly to 3.3 V with zero leakage (no voltage divider into an unpowered rail).
// BUSY (P0.29) is an output of the SX1262 (high while it sleeps), so it stays an input: driving it
// low would short it against the radio for the whole time the board is off.
//
// Some ProMicro bootloaders read P1.00 held at reset as their DFU button. GPREGRET = DFU_MAGIC_SKIP
// makes the Adafruit bootloader (0.6.0 and later) start the app without looking at the buttons; it
// clears the value, so it is written again before every system off.
#if defined(UI_HAS_ROTARY_INPUT) && defined(PIN_ENCODER_BTN) && defined(PIN_USER_BTN)
  #define FAKETEC_COMBO_WAKE 1
  #ifndef WAKE_SYNC_MILLIS
    #define WAKE_SYNC_MILLIS    300    // after the wake, the second button must be down within this
  #endif
  #ifndef WAKE_HOLD_MILLIS
    #define WAKE_HOLD_MILLIS    1500   // ... and both held together this long
  #endif
  #define WAKE_LET_GO_MILLIS    30     // a contact bounce shorter than this is not a release
  #define RELEASE_SETTLE_MILLIS 50     // both up this long before system off (else it wakes at once)
  #define RELEASE_WAIT_MILLIS   10000  // power off: how long to wait for the buttons to be let go
  #define REJECT_WAIT_MILLIS    2000   // rejected wake: the same, shorter (the CPU is running)
  #define WAKE_BUZZ_MILLIS      120
  #define DFU_MAGIC_SKIP        0x6D   // Adafruit nRF52 bootloader, src/main.c

static bool backDown() { return digitalRead(PIN_USER_BTN) == LOW; }
static bool pushDown() { return digitalRead(PIN_ENCODER_BTN) == LOW; }
static bool bothDown() { return backDown() && pushDown(); }

static bool vbusPresent() {
  uint32_t usb_status = NRF_POWER->USBREGSTATUS;
  if (softDeviceEnabled()) sd_power_usbregstatus_get(&usb_status);
  return (usb_status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

static void buzz(uint16_t ms) {
#ifdef PIN_VIBRATION
  pinMode(PIN_VIBRATION, OUTPUT);
  #ifdef VIBRATION_PWM
    analogWrite(PIN_VIBRATION, VIBRATION_PWM);
    delay(ms);
    analogWrite(PIN_VIBRATION, 0);
  #else
    digitalWrite(PIN_VIBRATION, HIGH);
    delay(ms);
    digitalWrite(PIN_VIBRATION, LOW);
  #endif
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, LOW);
#else
  (void)ms;
#endif
}

// true once both buttons have been up for RELEASE_SETTLE_MILLIS, false if still held after timeout
static bool waitButtonsReleased(unsigned long timeout) {
  pinMode(PIN_USER_BTN, INPUT_PULLUP);       // internal pull-up guarantees valid logic level
  pinMode(PIN_ENCODER_BTN, INPUT_PULLUP);    // for reading only: off again before system off
  delay(2);
  unsigned long start = millis(), up_at = 0;
  bool up = false;
  while (millis() - start < timeout) {
    feedWatchdog();
    if (backDown() || pushDown()) up = false;
    else if (!up) { up = true; up_at = millis(); }
    else if (millis() - up_at >= RELEASE_SETTLE_MILLIS) return true;
    delay(5);
  }
  return false;
}

// Rejected wake: the reset left P0.13 floating for a moment, so the rail may have dipped and the
// SX1262 restarted in standby (~0.6 mA). SetSleep again over SPI (NSS low also wakes it if it
// was still asleep), then leave NSS high and the bus pins without input buffers.
static void radioSleepRaw() {
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA_BUSY, INPUT);
  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_LORA_NSS, LOW);
  unsigned long t = millis();
  while (digitalRead(P_LORA_BUSY) == HIGH && millis() - t < 10) { }   // wakes from sleep in < 4 ms
  SPI.transfer(0x84);   // SetSleep
  SPI.transfer(0x00);   // cold start, no RTC wake-up
  digitalWrite(P_LORA_NSS, HIGH);
  SPI.endTransaction();
  SPI.end();
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_SCLK]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_MOSI]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[P_LORA_MISO]);
}

// last step of every power off and of a rejected wake; does not return
static void systemOffWithComboWake(unsigned long release_timeout) {
  waitButtonsReleased(release_timeout);

  // Keep switched power rail powered: SX1262 is sleeping (~0.6 uA) and OLED is off (~1 uA).
  // Keeping 3.3V on allows R7 (10k pull-up) to hold SW2 (P1.00) HIGH with zero leakage.
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);

  #ifdef PIN_VIBRATION
    pinMode(PIN_VIBRATION, OUTPUT);
    digitalWrite(PIN_VIBRATION, LOW);
  #endif

  // no pull-ups and no input buffers on the encoder pins: nothing flows through a closed contact
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_ENCODER_A]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_ENCODER_B]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_ENCODER_BTN]);

  // SW2 is the only wake pin. Since R7 holds it at 3.3V, NRF_GPIO_PIN_NOPULL has 0 uA leakage.
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_NOPULL,
                           backDown() ? NRF_GPIO_PIN_SENSE_HIGH : NRF_GPIO_PIN_SENSE_LOW);
  if (softDeviceEnabled()) {
    sd_power_gpregret_clr(0, 0xFF);
    sd_power_gpregret_set(0, DFU_MAGIC_SKIP);
  } else {
    NRF_POWER->GPREGRET = DFU_MAGIC_SKIP;
  }
  enterSystemOff();
}

// Woken from system off by SW2: start only after Back + push held together, else back to sleep.
// Runs first in begin(), before the screen and the radio are started.
static void confirmComboWake() {
  if (!(faketec_reset_reason & POWER_RESETREAS_OFF_Msk)) return;   // RST, power-up, watchdog, VBUS...
  pinMode(SX126X_POWER_EN, OUTPUT);   // the rail feeds R7 (Back's pull-up), the radio and the OLED
  digitalWrite(SX126X_POWER_EN, HIGH);
  if (vbusPresent()) return;   // on the charger: start without the buttons

  pinMode(PIN_USER_BTN, INPUT_PULLUP);       // internal pull-up guarantees read even if rail was off
  pinMode(PIN_ENCODER_BTN, INPUT_PULLUP);    // the encoder has no pull-up of its own
  delay(2);

  bool held = false;
  unsigned long start = millis();
  while (millis() - start < WAKE_SYNC_MILLIS) {
    if (bothDown()) { held = true; break; }
    delay(5);
  }
  if (held) {
    unsigned long since = millis(), apart_at = 0;
    bool apart = false;
    while (millis() - since < WAKE_HOLD_MILLIS) {
      if (bothDown()) apart = false;
      else if (!apart) { apart = true; apart_at = millis(); }
      else if (millis() - apart_at >= WAKE_LET_GO_MILLIS) { held = false; break; }
      delay(5);
    }
  }
  if (!held) {
    radioSleepRaw();
    systemOffWithComboWake(REJECT_WAIT_MILLIS); // rejected wake: straight back to system off!
  }

  buzz(WAKE_BUZZ_MILLIS);                       // accepted: let go buzz (120ms)
  waitButtonsReleased(RELEASE_WAIT_MILLIS);     // the UI must not see this hold as a click
}
#endif

void FaketecBoard::begin() {    
    NRF52Board::begin();
#ifdef FAKETEC_COMBO_WAKE
    confirmComboWake();    // may go back to system off: the register then keeps the OFF bit
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
#ifdef FAKETEC_COMBO_WAKE
  #ifdef PIN_VIBRATION
    // Double buzz confirmation before shutdown: 80ms ON, 80ms OFF, 120ms ON
    buzz(80);
    delay(80);
    buzz(120);
  #endif

  // End button and rotary listeners
  #if defined(UI_HAS_ROTARY_INPUT)
    rotary_input.end();
    #if defined(PIN_ENCODER_BTN)
      encoder_btn.end();
    #endif
  #endif
  #ifdef DISPLAY_CLASS
    user_btn.end();
  #endif

  // Shutdown display, LoRa radio to sleep
  shutdownPeripherals();

  #ifdef PIN_VIBRATION
    pinMode(PIN_VIBRATION, OUTPUT);
    digitalWrite(PIN_VIBRATION, LOW);
  #endif

  systemOffWithComboWake(RELEASE_WAIT_MILLIS);
#else
  #if defined(UI_HAS_ROTARY_INPUT)
    rotary_input.end();
    #if defined(PIN_ENCODER_BTN)
      encoder_btn.end();
    #endif
  #endif
  #ifdef DISPLAY_CLASS
    user_btn.end();
  #endif
  #ifdef PIN_VIBRATION
    pinMode(PIN_VIBRATION, OUTPUT);
    digitalWrite(PIN_VIBRATION, LOW);
  #endif
  NRF52Board::powerOff();
#endif
}
