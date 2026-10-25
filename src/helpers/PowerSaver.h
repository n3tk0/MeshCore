#pragma once

#include <stdint.h>

// Saving the battery of a node on its own (a repeater on a solar panel) in steps, by voltage:
//   low      (< low_mv):  adverts half as often, the mailbox stays in RAM (no flash writes)
//   critical (< crit_mv): no LoRa OTA and no mailbox, the node only forwards packets
//   off      (< off_mv):  radio off, the node sleeps and only checks the battery now and then
// Going down a step takes 3 readings in a row below it (a transmit pulls the cell down for a moment);
// going back up takes a reading 100 mV above the step's limit. External power (USB / a charger the
// board can see) is always 'normal'. Pure logic: the caller reads the battery and acts on the stage.

#define PSAVE_NORMAL    0
#define PSAVE_LOW       1
#define PSAVE_CRITICAL  2
#define PSAVE_OFF       3

#define PSAVE_HYSTERESIS_MV   100
#define PSAVE_CONFIRM_READS     3

struct PowerSaverConfig {
  uint8_t  enabled;
  uint16_t low_mv, crit_mv, off_mv;   // 0 = that step is not used
};

class PowerSaver {
  PowerSaverConfig _cfg;
  uint8_t _stage;
  uint8_t _below;      // readings in a row that point to a lower step
  uint8_t _below_to;   // ... and to which

  uint8_t stageFor(uint16_t mv, int extra) const {
    if (_cfg.off_mv && mv < _cfg.off_mv + extra) return PSAVE_OFF;
    if (_cfg.crit_mv && mv < _cfg.crit_mv + extra) return PSAVE_CRITICAL;
    if (_cfg.low_mv && mv < _cfg.low_mv + extra) return PSAVE_LOW;
    return PSAVE_NORMAL;
  }

public:
  PowerSaver() { defaults(); }

  void defaults() {
    _cfg.enabled = 1;
    _cfg.low_mv = 3600;
    _cfg.crit_mv = 3450;
    _cfg.off_mv = 0;   // 'off' only by hand (`battsave 3600 3450 3300`): a wrongly calibrated ADC on a roof
                       // would take the radio away with no way to fix it from afar
    _stage = PSAVE_NORMAL;
    _below = 0;
    _below_to = PSAVE_NORMAL;
  }

  PowerSaverConfig& config() { return _cfg; }
  bool valid() const {   // the steps go down in order
    if (_cfg.low_mv && _cfg.crit_mv && _cfg.crit_mv >= _cfg.low_mv) return false;
    if (_cfg.crit_mv && _cfg.off_mv && _cfg.off_mv >= _cfg.crit_mv) return false;
    if (_cfg.low_mv && _cfg.off_mv && _cfg.off_mv >= _cfg.low_mv) return false;
    return true;
  }
  uint8_t stage() const { return _stage; }
  void setStage(uint8_t s) { _stage = s; _below = 0; }

  // a battery reading (mv 0 or out of range = the board cannot measure it); returns the stage
  uint8_t update(uint16_t mv, bool external) {
    if (!_cfg.enabled || external || mv < 2500 || mv > 4600) {
      _below = 0;
      if (!_cfg.enabled || external) _stage = PSAVE_NORMAL;
      return _stage;   // a bad reading changes nothing
    }
    uint8_t down = stageFor(mv, 0);
    if (down > _stage) {
      _below_to = _below == 0 || down < _below_to ? down : _below_to;   // the mildest step of the run
      if (++_below >= PSAVE_CONFIRM_READS) {
        _stage = _below_to;
        _below = 0;
      }
      return _stage;
    }
    _below = 0;
    uint8_t up = stageFor(mv, PSAVE_HYSTERESIS_MV);
    if (up < _stage) _stage = up;
    return _stage;
  }

  static const char* name(uint8_t s) {
    switch (s) {
      case PSAVE_LOW: return "low";
      case PSAVE_CRITICAL: return "critical";
      case PSAVE_OFF: return "off";
      default: return "normal";
    }
  }
};
