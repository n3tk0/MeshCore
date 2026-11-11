#include "SmartMppt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// register numbers: keep in step with hardware/solar-smart-mppt/firmware/src/regs.h
namespace {
enum : uint8_t {
  R_ID = 0x00, R_VERSION = 0x01, R_STATUS = 0x02, R_SOC = 0x03, R_REMAIN = 0x04, R_VPACK = 0x05,
  R_VCELL1 = 0x06, R_VCELL2 = 0x07, R_IBAT = 0x08, R_VBUS = 0x09, R_IBUS = 0x0A, R_TEMP = 0x0B,
  R_CHEM = 0x0C, R_BQ_STAT = 0x0D, R_BQ_FAULT = 0x0E, R_CAP_LEARNT = 0x0F, R_CYCLES = 0x10,
  R_UNLOCK = 0x60, R_CMD = 0x61, R_RESULT = 0x62, R_BQ_FIRST = 0x80,
};
const uint16_t UNLOCK_KEY = 0xA55A;

struct Name { const char* name; uint8_t reg; bool is_signed; };
const Name NAMES[] = {
  {"chem", 0x20, false}, {"capacity", 0x21, false}, {"vchg", 0x22, false}, {"ichg", 0x23, false},
  {"iin", 0x24, false}, {"mppt", 0x25, false}, {"vindpm", 0x26, false}, {"ntc", 0x27, false},
  {"od_off", 0x28, false}, {"od_on", 0x29, false}, {"ship", 0x2A, false}, {"j7_off", 0x2B, false},
  {"j7_on", 0x2C, false}, {"idsg_max", 0x2D, false}, {"isc", 0x2E, false}, {"bal_delta", 0x2F, false},
  {"bal_start", 0x30, false}, {"zero_uv", 0x31, true}, {"shunt_uohm", 0x32, false}, {"ina_sign", 0x33, false},
  {"expert_n", 0x34, false}, {"expert0", 0x35, false}, {"expert1", 0x36, false}, {"expert2", 0x37, false},
  {"expert3", 0x38, false}, {"expert4", 0x39, false}, {"expert5", 0x3A, false}, {"expert6", 0x3B, false},
  {"expert7", 0x3C, false},
  // read only
  {"status", R_STATUS, false}, {"soc", R_SOC, false}, {"remain", R_REMAIN, false}, {"vpack", R_VPACK, false},
  {"vcell1", R_VCELL1, false}, {"vcell2", R_VCELL2, false}, {"ibat", R_IBAT, true}, {"vbus", R_VBUS, false},
  {"ibus", R_IBUS, true}, {"temp", R_TEMP, true}, {"cap_learnt", R_CAP_LEARNT, false}, {"cycles", R_CYCLES, false},
  {"result", R_RESULT, false},
};

struct Cmd { const char* name; uint8_t code; bool locked; };
const Cmd CMDS[] = {
  {"save", 1, true}, {"load", 2, false}, {"defaults", 3, true}, {"reset", 4, false}, {"full", 5, false},
  {"zero", 6, true}, {"ship", 7, true}, {"bsl", 8, true}, {"reboot", 9, true},
};

const char* const ERRS[] = { "OK", "locked", "out of range", "flash error", "bad register", "busy",
                             "unknown command", "not now" };

bool readReg(TwoWire& w, uint8_t reg, uint16_t& v) {
  w.beginTransmission(SMPPT_ADDR);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom((uint8_t)SMPPT_ADDR, (uint8_t)2) != 2) return false;
  v = (uint16_t)(w.read() << 8);
  v |= (uint16_t)w.read();
  return true;
}

bool writeReg(TwoWire& w, uint8_t reg, uint16_t v) {
  w.beginTransmission(SMPPT_ADDR);
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
      if (c.code >= 7) { snprintf(reply, max, "OK - %s", c.name); return true; }
      snprintf(reply, max, "Err - no answer");
      return false;
    }
  }
  snprintf(reply, max, r == 0 ? "OK" : "Err - %s", errText(r));
  return r == 0;
}

void status(TwoWire& w, char* reply, size_t max) {
  uint16_t id, ver, st, soc, vpack, ibat, vbus, rem, chem;
  if (!readReg(w, R_ID, id) || id != 0x534D) { snprintf(reply, max, "Err - no solar-smart-mppt on 0x42"); return; }
  readReg(w, R_VERSION, ver);
  readReg(w, R_STATUS, st);
  readReg(w, R_SOC, soc);
  readReg(w, R_VPACK, vpack);
  readReg(w, R_IBAT, ibat);
  readReg(w, R_VBUS, vbus);
  readReg(w, R_REMAIN, rem);
  readReg(w, R_CHEM, chem);
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
