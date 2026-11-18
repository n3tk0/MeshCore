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
  {"heat_on", R_HEAT_ON, true}, {"heat_off", R_HEAT_OFF, true}, {"heat_duty", R_HEAT_DUTY, false},
  {"expert0", R_EXPERT0, false}, {"expert1", R_EXPERT0 + 1, false}, {"expert2", R_EXPERT0 + 2, false},
  {"expert3", R_EXPERT0 + 3, false}, {"expert4", R_EXPERT0 + 4, false}, {"expert5", R_EXPERT0 + 5, false},
  {"expert6", R_EXPERT0 + 6, false}, {"expert7", R_EXPERT0 + 7, false},
  // read only
  {"status", R_STATUS, false}, {"soc", R_SOC, false}, {"remain", R_REMAIN, false}, {"vpack", R_VPACK, false},
  {"vcell1", R_VCELL1, false}, {"vcell2", R_VCELL2, false}, {"ibat", R_IBAT, true}, {"vbus", R_VBUS, false},
  {"ibus", R_IBUS, true}, {"temp", R_TEMP, true}, {"bat_temp", R_BAT_TEMP, true}, {"heat_pct", R_HEAT_PCT, false},
  {"status2", R_STATUS2, false}, {"poorsrc", R_POORSRC, false}, {"poorsrc_age", R_POORSRC_AGE, false},
  {"cap_learnt", R_CAP_LEARNT, false}, {"cycles", R_CYCLES, false},
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
  uint16_t bt = 0x8000, st2 = 0;   // older firmware has no such registers (they read 0)
  if (ver >= 0x0002) {   // firmware 0.2 and newer
    readReg(w, R_BAT_TEMP, bt);
    readReg(w, R_STATUS2, st2);
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
  if (bt != 0x8000 && n > 0 && (size_t)n < max) {   // battery temperature (NTC), 0.1 degC
    int t = (int16_t)bt;
    n += snprintf(reply + n, max - n, " bat:%s%d.%dC", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
  }
  if ((st2 & ST2_HEATING) && n > 0 && (size_t)n < max) n += snprintf(reply + n, max - n, " heat");
  // with the panel there: why it may not charge, as the direct BQ25798 `charger` line says it
  // (vin = REG05 while MPPT (REG15 bit 0) is off; poor src xN Tm; hiz; no PG). Firmware 0.3 and newer.
  if (ver >= 0x0003 && ((st & 0x0004) || (st2 & (ST2_HIZ | ST2_NO_PG | ST2_POOR_SRC))) && n > 0 && (size_t)n < max) {
    uint16_t cnt = 0, age = 0xFFFF, r05 = 0, r15 = 1;
    readReg(w, R_POORSRC, cnt);
    readReg(w, R_POORSRC_AGE, age);
    readReg(w, (uint8_t)(R_BQ_FIRST + 0x05), r05);
    readReg(w, (uint8_t)(R_BQ_FIRST + 0x15), r15);
    if ((!(r15 & 1) || (st2 & (ST2_HIZ | ST2_NO_PG))) && r05) n += snprintf(reply + n, max - n, " vin %u.%uV", (unsigned)(r05 & 0xFF) / 10, (unsigned)(r05 & 0xFF) % 10);
    if ((st2 & ST2_POOR_SRC) && cnt && age != 0xFFFF && (size_t)n < max) {
      n += snprintf(reply + n, max - n, " poor-src x%u %um", cnt > 999 ? 999u : cnt, age);
    }
    if ((st2 & ST2_HIZ) && (size_t)n < max) n += snprintf(reply + n, max - n, " hiz");
    if ((st2 & ST2_NO_PG) && (size_t)n < max) n += snprintf(reply + n, max - n, " no-PG");
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

  if (strcmp(args, "names") == 0 || memcmp(args, "names ", 6) == 0) {
    // settings only (expert1..7 work like expert0); a long list goes on over pages: "names 2"
    int page = args[5] ? atoi(args + 6) : 1, cur = 1, n = 0;
    if (page < 1) page = 1;
    reply[0] = 0;
    for (const Name& nm : NAMES) {
      if (nm.reg < 0x20 || nm.reg > R_SET_LAST || (nm.reg > R_EXPERT0 && nm.reg < R_HEAT_ON)) continue;
      size_t need = (n > 0 ? 1 : 0) + strlen(nm.name);
      if ((size_t)n + need + 10 >= max) {   // keep room for " +names N"
        if (cur == page) { snprintf(reply + n, max - n, " +names %d", page + 1); return; }
        cur++; n = 0; need = strlen(nm.name);
      }
      if (cur == page) n += snprintf(reply + n, max - n, "%s%s", n > 0 ? " " : "", nm.name);
      else n += (int)need;   // count only, for the page break
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

bool smpptPresent(TwoWire& w) {
  uint16_t id;
  return readReg(w, R_ID, id) && id == 0x534D;
}

namespace {
// a whole number, nothing else after it (-1 if not)
long wholeNumber(const char* s) {
  if (*s < '0' || *s > '9') return -1;
  char* end;
  long v = strtol(s, &end, 0);
  return *end == 0 ? v : -1;
}

// one setting, then SAVE so it survives a reset of the board
void setAndSave(TwoWire& w, uint8_t reg, uint16_t v, char* reply, size_t max) {
  uint16_t r;
  if (!writeReg(w, reg, v) || !readReg(w, R_RESULT, r)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
  if (r != 0) { snprintf(reply, max, "Err - %s", errText(r)); return; }
  runCmd(w, CMDS[0], reply, max);   // "save"
}

// BQ register n as an expert entry: added, changed, or (val "auto") taken out
void expertReg(TwoWire& w, uint8_t bq, const char* val, char* reply, size_t max) {
  uint16_t n, e[EXPERT_MAX];
  if (!readReg(w, R_EXPERT_N, n) || n > EXPERT_MAX) { snprintf(reply, max, "Err - no board on 0x42"); return; }
  for (uint16_t i = 0; i < n; i++) if (!readReg(w, R_EXPERT0 + i, e[i])) { snprintf(reply, max, "Err - read"); return; }
  int at = -1;
  for (uint16_t i = 0; i < n; i++) if ((e[i] >> 8) == bq) at = i;
  uint16_t r = 0;
  if (strcmp(val, "auto") == 0) {
    if (at < 0) { snprintf(reply, max, "OK - REG%02X was not set", bq); return; }
    for (uint16_t i = at; i + 1 < n; i++) e[i] = e[i + 1];
    n--;
    for (uint16_t i = at; i < n; i++) if (!writeReg(w, R_EXPERT0 + i, e[i])) { snprintf(reply, max, "Err - write"); return; }
    if (!writeReg(w, R_EXPERT_N, n) || !readReg(w, R_RESULT, r)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
    if (r) { snprintf(reply, max, "Err - %s", errText(r)); return; }
    writeReg(w, R_EXPERT0 + n, 0);   // clear the freed slot so RAM matches flash
    runCmd(w, CMDS[0], reply, max);
    return;
  }
  long v = wholeNumber(val);
  if (v < 0 || v > 0xFF) { snprintf(reply, max, "Err - charger reg 0xNN [0..255|auto]"); return; }
  uint16_t entry = (uint16_t)(bq << 8 | v);
  if (at >= 0) { setAndSave(w, R_EXPERT0 + at, entry, reply, max); return; }
  if (n >= EXPERT_MAX) { snprintf(reply, max, "Err - %u registers set already", EXPERT_MAX); return; }
  // the board's rule, checked here too: older MSPM0 firmware checks the slot only with the count and
  // would keep a refused entry in RAM (settings 'unsaved')
  if (bq < 0x03 || bq > 0x18 || bq == 0x0A || bq == 0x11) {
    snprintf(reply, max, "Err - REG%02X is not allowed (0x03..0x18 without 0x0A, 0x11)", bq);
    return;
  }
  // write the entry first (checked immediately for non-zero slots), then the count (also checks reg 0x00)
  if (!writeReg(w, R_EXPERT0 + n, entry) || !readReg(w, R_RESULT, r)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
  if (r == 0) {
    setAndSave(w, R_EXPERT_N, n + 1, reply, max);
  } else {
    snprintf(reply, max, "Err - %s", errText(r));
  }
  if (strncmp(reply, "Err - out of range", 18) == 0) {
    snprintf(reply, max, "Err - REG%02X is not allowed (0x03..0x18 without 0x0A, 0x11)", bq);
  }
}
}  // namespace

void smpptCharger(TwoWire& w, const char* sub, const char* val, char* reply, size_t max) {
  if (*sub == 0) { status(w, reply, max); return; }
  if (strcmp(sub, "chem") == 0) {
    int c = strcmp(val, "liion") == 0 ? CHEM_LIION : strcmp(val, "lifepo4") == 0 ? CHEM_LFP
          : strcmp(val, "lto") == 0 ? CHEM_LTO2S : strcmp(val, "auto") == 0 ? CHEM_BY_JUMPER : -1;
    if (c < 0) { snprintf(reply, max, "Err - charger chem liion|lifepo4|lto|auto (auto = jumper)"); return; }
    setAndSave(w, R_CHEM_SET, (uint16_t)c, reply, max);
  } else if (strcmp(sub, "ichg") == 0) {
    long ma = wholeNumber(val);
    if (ma < 50 || ma > 3000) { snprintf(reply, max, "Err - charger ichg 50..3000 (mA)"); return; }
    setAndSave(w, R_ICHG, (uint16_t)ma, reply, max);
  } else if (strcmp(sub, "vin") == 0) {
    long mv = strcmp(val, "mppt") == 0 ? 0 : wholeNumber(val);
    if (mv != 0 && (mv < 3600 || mv > 22000)) { snprintf(reply, max, "Err - charger vin mppt|3600..22000 (mV)"); return; }
    setAndSave(w, R_VINDPM, (uint16_t)mv, reply, max);
  } else if (strcmp(sub, "ntc") == 0) {
    int m = strcmp(val, "on") == 0 ? NTC_ON : strcmp(val, "off") == 0 ? NTC_OFF : strcmp(val, "auto") == 0 ? NTC_AUTO : -1;
    if (m < 0) { snprintf(reply, max, "Err - charger ntc on|off|auto"); return; }
    setAndSave(w, R_NTC, (uint16_t)m, reply, max);
  } else if (strcmp(sub, "cutoff") == 0) {
    long mv = wholeNumber(val);
    if (mv <= 0) { snprintf(reply, max, "Err - charger cutoff <mV> (the board always cuts off)"); return; }
    setAndSave(w, R_OD_OFF, (uint16_t)mv, reply, max);   // discharge FET off under this
  } else if (strcmp(sub, "shipfet") == 0) {
    snprintf(reply, max, "Err - no ship FET setting on this board");
  } else if (strcmp(sub, "ship") == 0) {
    runCmd(w, CMDS[6], reply, max);   // "ship"
  } else if (strcmp(sub, "reg") == 0) {
    const char* sp = strchr(val, ' ');
    char regtxt[8];
    size_t len = sp ? (size_t)(sp - val) : strlen(val);
    if (len == 0 || len >= sizeof(regtxt)) { snprintf(reply, max, "Err - charger reg 0xNN [value|auto]"); return; }
    memcpy(regtxt, val, len);
    regtxt[len] = 0;
    long bq = wholeNumber(regtxt);
    if (bq < 0 || bq > 0x48) { snprintf(reply, max, "Err - charger reg 0x00..0x48 [value|auto]"); return; }
    if (sp) while (*sp == ' ') sp++;
    if (!sp || !*sp) {
      uint16_t v;
      if (!readReg(w, (uint8_t)(R_BQ_FIRST + bq), v)) { snprintf(reply, max, "Err - no board on 0x42"); return; }
      snprintf(reply, max, "> REG%02lX = 0x%02X (as the board read it, every 1 s)", bq, v & 0xFF);
      return;
    }
    expertReg(w, (uint8_t)bq, sp, reply, max);
  } else {
    snprintf(reply, max, "Err - charger [chem|ichg|vin|ntc|cutoff|ship|reg]");
  }
}
