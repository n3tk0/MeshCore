#pragma once
// CLI `smppt` for the solar-smart-mppt board (hardware/solar-smart-mppt). The board shows itself as an
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

#define SMPPT_ADDR 0x42

void smpptCommand(TwoWire& wire, const char* args, char* reply, size_t max);
