#pragma once
// CLI `smppt` for the solar-smart-mppt board (MSPM0 firmware in solar-smart-mppt/firmware). The board shows itself as an
// INA260 on 0x41 (read by the sensor code) and has its own register map on 0x42, used here:
//   smppt                       status: voltage, current, %, sun, charging, BMS flags
//   smppt get <name|0xNN>       one register (names as in `smppt names`)
//   smppt set <name|0xNN> <v>   change a setting in the board's RAM (works at once; `smppt save` keeps it)
//   smppt save                  store the settings in the board's flash (survives a reset)
//   smppt cmd <load|defaults|reset|full|zero|ship|bsl|reboot>
//   smppt bq <0xNN>             a BQ25798 register as the board read it last
//   smppt names                 the setting names
#include <Arduino.h>
#include <Wire.h>

void smpptCommand(TwoWire& wire, const char* args, char* reply, size_t max);

// true when a solar-smart-mppt (or xiao-solar v0.5) MSPM0 answers on 0x42
bool smpptPresent(TwoWire& wire);
// `charger <sub> <val>` for a board whose BQ25798 sits behind the MSPM0: the same words as for a
// BQ25798 on the node's own bus, mapped to the 0x42 settings (each change saved in the board's flash).
void smpptCharger(TwoWire& wire, const char* sub, const char* val, char* reply, size_t max);
