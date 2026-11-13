#include "SmartMppt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// the board's own register map (C header, shared with its firmware so the two cannot drift apart)
extern "C" {
#include "../../hardware/solar-smart-mppt/firmware/src/regs.h"
}

namespace {

struct Name { const char* name; uint8_t reg; bool is_signed; };
const Name NAMES[] = {
  {"chem", R_CHEM_SET, false}, {"capacity", R_CAPACITY, false}, {"vchg", R_VCHG, false},
  {"ichg", R_ICHG, false}, {"iin", R_IIN, false}, {"mppt", R_MPPT, false}, {"vindpm", R_VINDPM, false},
  {"ntc", R_NTC, false}, {"od_off", R_OD_OFF, false}, {"od_on", R_OD_ON, false}, {"ship", R_SHIP, false},
  {"j7_off", R_J7_OFF, false}, {"j7_on", R_J7_ON, false}, {"idsg_max", R_IDSG_MAX, false}, {"isc", R_ISC, false},
  {"bal_delta", R_BAL_DELTA, false}, {"bal_start", R_BAL_START, false}, {"zero_uv", R_ZERO_UV, true},
  {"shunt_uohm", R_SHUNT_UOHM, false}, {"ina_sign", R_INA_SIGN, false}, {"expert_n", R_EXPERT_N, false},
  {"expert0", R_EXPERT0, false}, {"expert1", R_EXPERT0 + 1, false}, {"expert2", R_EXPERT0 + 2, false},
  {"expert3", R_EXPERT0 + 3, false}, {"expert4", R_EXPERT0 + 4, false}, {"expert5", R_EXPERT0 + 5, false},
  {"expert6", R_EXPERT0 + 6, false}, {"expert7", R_EXPERT0 + 7, false},
  // read only
  {"status", R_STATUS, false}, {"soc", R_SOC, false}, {"remain", R_REMAIN, false}, {"vpack", R_VPACK, false},
  {"vcell1", R_VCELL1, false}, {"vcell2", R_VCELL2, false}, {"ibat", R_IBAT, true}, {"vbus", R_VBUS, false},
  {"ibus", R_IBUS, true}, {"temp", R_TEMP, true}, {"cap_learnt", R_CAP_LEARNT, false}, {"cycles", R_CYCLES, false},
  {"result", R_RESULT, false},
};

struct Cmd { const char* name; uint8_t code; bool locked; };
const Cmd CMDS[] = {
  {"save", CMD_SAVE, true}, {"load", CMD_LOAD, false}, {"defaults", CMD_DEFAULTS, true},
  {"reset", CMD_COUNTER_RESET, false}, {"full", CMD_SET_FULL, false}, {"zero", CMD_ZERO, true},
  {"ship", CMD_SHIP, true}, {"bsl", CMD_BSL, true}, {"reboot", CMD_REBOOT, true},
};

const char* const ERRS[] = { "OK", "locked", "out of range", "flash error", "bad register", "busy",
                             "unknown command", "not now" };

bool readReg(TwoWire& w, uint8_t reg, uint16_t& v) {
  w.beginTransmission(ADDR_SMPPT);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)ADDR_SMPPT, (uint8_t)2) != 2) return false;
  v = (uint16_t)(w.read() << 8);
  v |= (uint16_t)w.read();
  return true;
}

bool writeReg(TwoWire& w, uint8_t reg, uint16_t v) {
  w.beginTransmission(ADDR_SMPPT);
  w.write(reg);
  w.write((uint8_t)(v >> 8));
  w.write((uint8_t)v);
  return w.endTransmission() == 0;
}

const char* errText(uint16_t r) {
  if (r == 0xFFFF) return "still running";
  return r < sizeof(ERRS) / sizeof(ERRS[0]) ? ERRS[r] : "error";
}

const Name* findName(const char* s, size_t len) {
  for (const Name& n : NAMES)
    if (strlen(n.name) == len && memcmp(n.name, s, len) == 0) return &n;
  return nullptr;
}

// "<name>" or "0xNN" -> register; returns false if unknown
bool parseReg(const char*& p, uint8_t& reg, bool& is_signed) {
  while (*p == ' ') p++;
  const char* start = p;
  while (*p && *p != ' ') p++;
  size_t len = (size_t)(p - start);
  if (len == 0) return false;
  if (const Name* n = findName(start, len)) { reg = n->reg; is_signed = n->is_signed; return true; }
  char* end;
  unsigned long v = strtoul(start, &end, 0);
  if (end != p || v > 0xFF) return false;
  reg = (uint8_t)v;
  is_signed = false;
  return true;
}

bool runCmd(TwoWire& w, const Cmd& c, char* reply, size_t max) {
  if (c.locked && !writeReg(w, R_UNLOCK, UNLOCK_KEY)) { snprintf(reply, max, "Err - no board on 0x42"); return false; }
  if (!writeReg(w, R_CMD, c.code)) { snprintf(reply, max, "Err - no board on 0x42"); return false; }
  uint16_t r = 0xFFFF;
  for (int i = 0; i < 30 && r == 0xFFFF; i++) {   // the board runs it in its main loop (flash: ~ms)
    delay(20);
    if (!readReg(w, R_RESULT, r)) {
      // ship / bsl / reboot: the board is gone, as asked
      if (c.code >= CMD_SHIP) { snprintf(reply, max, "OK - %s", c.name); return true; }
      snprintf(reply, max, "Err - no answer");
      return false;
    }
  }
  snprintf(reply, max, r == 0 ? "OK" : "Err - %s", errText(r));
  return r == 0;
}

void status(TwoWire& w, char* reply, size_t max) {
  uint16_t id = 0, ver, st, soc, vpack, ibat, vbus, rem, chem;
  if (!readReg(w, R_ID, id) || id != 0x534D) { snprintf(reply, max, "Err - no solar-smart-mppt on 0x42"); return; }
  if (!readReg(w, R_VERSION, ver) || !readReg(w, R_STATUS, st) || !readReg(w, R_SOC, soc) ||
      !readReg(w, R_VPACK, vpack) || !readReg(w, R_IBAT, ibat) || !readReg(w, R_VBUS, vbus) ||
      !readReg(w, R_REMAIN, rem) || !readReg(w, R_CHEM, chem)) {
    snprintf(reply, max, "Err - read failed, try again");
    return;
  }
  static const char* const CHEMS[] = { "Li-ion", "LiFePO4", "LTO2S" };
  int n = snprintf(reply, max, "v%u.%u %s %umV %dmA %u.%u%%%s %umAh sun:%umV",
                   ver >> 8, ver & 0xFF, (chem >> 8) < 3 ? CHEMS[chem >> 8] : "?", vpack, (int16_t)ibat,
                   soc / 100, (soc % 100) / 10, (st & 0x4000) ? "(V)" : "", rem, vbus);
  static const char* const FLAGS[16] = { "chg", "full", "sun", "no-ntc", "od-off", "oc-off", "SHORT", "UV",
                                         "OV", "OVERCURRENT", "bal", "j7", "BQ-FAULT", "BQ-I2C", "", "unsaved" };
  for (int b = 0; b < 16 && n > 0 && (size_t)n < max; b++) {
    if (b == 2 || b == 14) continue;   // sun shown above, SOC_FROM_V as (V)
    if (st & (1u << b)) n += snprintf(reply + n, max - n, " %s", FLAGS[b]);
  }
}
}  // namespace

void smpptCommand(TwoWire& w, const char* args, char* reply, size_t max) {
  while (*args == ' ') args++;
  if (*args == 0 || strcmp(args, "status") == 0) { status(w, reply, max); return; }

  if (memcmp(args, "get ", 4) == 0) {
    const char* p = args + 4;
    uint8_t reg; bool sg; uint16_t v;
    if (!parseReg(p, reg, sg)) { snprintf(reply, max, "Err - unknown name (see: smppt names)"); return; }
    if (!readReg(w, reg, v)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
    if (sg) snprintf(reply, max, "> %d", (int16_t)v);
    else snprintf(reply, max, "> %u (0x%04X)", v, v);
    return;
  }

  if (memcmp(args, "set ", 4) == 0) {
    const char* p = args + 4;
    uint8_t reg; bool sg;
    if (!parseReg(p, reg, sg)) { snprintf(reply, max, "Err - unknown name (see: smppt names)"); return; }
    char* end;
    long v = strtol(p, &end, 0);
    if (end == p || v < -32768 || v > 65535) { snprintf(reply, max, "Err - value"); return; }
    uint16_t r;
    if (!writeReg(w, reg, (uint16_t)v) || !readReg(w, R_RESULT, r)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
    snprintf(reply, max, r == 0 ? "OK (smppt save keeps it)" : "Err - %s", errText(r));
    return;
  }

  if (memcmp(args, "bq ", 3) == 0) {
    unsigned long reg = strtoul(args + 3, nullptr, 0);
    uint16_t v;
    if (reg > 0x48 || !readReg(w, (uint8_t)(R_BQ_FIRST + reg), v)) { snprintf(reply, max, "Err"); return; }
    snprintf(reply, max, "> REG%02lX = 0x%02X", reg, v & 0xFF);
    return;
  }

  if (strcmp(args, "names") == 0) {
    int n = 0;
    for (const Name& nm : NAMES) {
      if (nm.reg < 0x20 || nm.reg > 0x35) continue;   // settings only; expert1..7 work like expert0
      if (n > 0 && (size_t)n < max) n += snprintf(reply + n, max - n, " ");
      if ((size_t)n < max) n += snprintf(reply + n, max - n, "%s", nm.name);
      if ((size_t)n >= max) break;
    }
    return;
  }

  const char* c = args;
  if (memcmp(c, "cmd ", 4) == 0) c += 4;
  for (const Cmd& cmd : CMDS) {
    if (strcmp(c, cmd.name) == 0) { runCmd(w, cmd, reply, max); return; }
  }
  snprintf(reply, max, "Err - smppt [status|get|set|save|cmd|bq|names]");
}
