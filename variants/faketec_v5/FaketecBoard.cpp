#include <Arduino.h>
#include <Wire.h>

#include "FaketecBoard.h"
#include "target.h"

void FaketecBoard::begin() {    
    NRF52Board::begin();
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
  ((QuadratureRotaryInput&)rotary_input).end();   // no pull-up current through a closed encoder contact
#endif
  NRF52Board::powerOff();
}
