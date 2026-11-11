#include "settings.h"
#include <stddef.h>
#include <string.h>

int chem_cells(uint8_t chem) { return chem == CHEM_LTO2S ? 2 : 1; }

typedef struct {
  uint16_t vchg, vchg_min, vchg_max;
  uint16_t od_off, od_on, ship, j7_off, j7_on;
  uint16_t pack_min, pack_max;   // limits for the voltage thresholds
} chem_table_t;

static const chem_table_t CHEM[CHEM_COUNT] = {
  //  vchg  min   max   od_off od_on ship  j7_off j7_on  pack_min pack_max
  { 4200, 4000, 4250, 3000, 3300, 2900, 3300, 3500, 2500, 4250 },   // Li-ion 1S
  { 3600, 3400, 3650, 2700, 3000, 2600, 3000, 3150, 2500, 3650 },   // LiFePO4 1S
  { 5600, 4800, 5600, 3600, 4000, 3400, 4400, 4700, 3000, 5600 },   // LTO 2S (2.8 V/cell max)
};

void settings_defaults(settings_t *s, uint8_t chem) {
  if (chem >= CHEM_COUNT) chem = CHEM_LIION;
  const chem_table_t *c = &CHEM[chem];
  memset(s, 0, sizeof(*s));
  s->chem = CHEM_BY_JUMPER;
  s->chem_jumper = chem;
  s->capacity_mah = 0;
  s->vchg_mv = c->vchg;
  s->ichg_ma = 1000;
  s->iin_ma = 3000;
  s->mppt_code = 4;          // 81.25 % of Voc, close to Vmp of most panels
  s->ntc_mode = NTC_AUTO;
  s->vindpm_mv = 0;
  s->od_off_mv = c->od_off;
  s->od_on_mv = c->od_on;
  s->ship_mv = c->ship;
  s->j7_off_mv = c->j7_off;
  s->j7_on_mv = c->j7_on;
  s->idsg_max_ma = 0;
  s->isc_ma = 1500;          // v0.3 shunt bias (~33 mV) reads discharge only up to ~1.6 A
  s->bal_delta_mv = 30;
  s->bal_start_mv = 2500;    // per cell, LTO
  s->zero_uv = 0;
  s->shunt_uohm = 20000;
  s->ina_sign = INA_SIGN_DISCHARGE_POSITIVE;
  s->expert_n = 0;
}

static bool in(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi; }

int settings_check(const settings_t *s, uint8_t chem) {
  if (chem >= CHEM_COUNT) return ERR_RANGE;
  const chem_table_t *c = &CHEM[chem];
  if (s->chem != CHEM_BY_JUMPER && s->chem >= CHEM_COUNT) return ERR_RANGE;
  if (!in(s->vchg_mv, c->vchg_min, c->vchg_max)) return ERR_RANGE;
  if (!in(s->ichg_ma, 50, 3000)) return ERR_RANGE;          // F1 is 3 A, the shunt reads to 3.2 A
  if (!in(s->iin_ma, 100, 3300)) return ERR_RANGE;
  if (s->mppt_code > 7) return ERR_RANGE;
  if (s->ntc_mode > NTC_ON) return ERR_RANGE;
  if (s->vindpm_mv != 0 && !in(s->vindpm_mv, 3600, 22000)) return ERR_RANGE;
  if (!in(s->ship_mv, c->pack_min, c->pack_max)) return ERR_RANGE;
  if (!in(s->od_off_mv, c->pack_min, c->pack_max)) return ERR_RANGE;
  if (s->ship_mv > s->od_off_mv) return ERR_RANGE;
  if (s->od_on_mv < s->od_off_mv + 100 || s->od_on_mv >= s->vchg_mv) return ERR_RANGE;
  if (s->j7_on_mv < s->j7_off_mv + 50 || s->j7_off_mv < s->od_off_mv || s->j7_on_mv > c->pack_max)
    return ERR_RANGE;
  if (s->idsg_max_ma != 0 && !in(s->idsg_max_ma, 100, 3200)) return ERR_RANGE;
  if (!in(s->isc_ma, 200, 3200)) return ERR_RANGE;
  if (!in(s->bal_delta_mv, 10, 500) || !in(s->bal_start_mv, 1500, 2800)) return ERR_RANGE;
  if (!in(s->shunt_uohm, 15000, 25000)) return ERR_RANGE;
  if (s->ina_sign > 1) return ERR_RANGE;
  if (s->expert_n > EXPERT_MAX) return ERR_RANGE;
  for (int i = 0; i < s->expert_n; i++) {
    uint8_t reg = s->expert[i] >> 8;
    // VSYSMIN, the charge voltage, the cell count (REG0A 7:6) and ship mode stay ours: a wrong byte there is unsafe
    if (reg > 0x18 || reg == 0x00 || reg == 0x01 || reg == 0x02 || reg == 0x0A || reg == 0x11) return ERR_RANGE;
  }
  return ERR_OK;
}

bool settings_resolve_chem(settings_t *s, uint8_t jumper, uint8_t *active) {
  if (jumper >= CHEM_COUNT) jumper = CHEM_LIION;
  if (s->chem_jumper != jumper) {
    // JP4 moved since the settings were saved: the jumper wins, voltages back to its defaults
    settings_t keep = *s;
    settings_defaults(s, jumper);
    s->capacity_mah = keep.capacity_mah;
    s->ichg_ma = keep.ichg_ma;
    s->iin_ma = keep.iin_ma;
    s->mppt_code = keep.mppt_code;
    s->vindpm_mv = keep.vindpm_mv;
    s->ntc_mode = keep.ntc_mode;
    s->zero_uv = keep.zero_uv;
    s->shunt_uohm = keep.shunt_uohm;
    s->ina_sign = keep.ina_sign;
    *active = jumper;
    return true;
  }
  *active = s->chem == CHEM_BY_JUMPER ? jumper : s->chem;
  return false;
}

// ---------------- flash store ----------------

uint16_t crc16_ccitt(const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

typedef struct {
  uint16_t magic;     // 0x5352 "SR"
  uint8_t  type;      // REC_*
  uint8_t  version;   // SETTINGS_VERSION
  uint32_t seq;
  uint16_t len;       // payload bytes
  uint16_t crc;       // over the header (crc = 0) + payload
} rec_hdr_t;

#define REC_MAGIC 0x5352
#define REC_MAX_PAYLOAD 96
#define ALIGN8(x) (((x) + 7u) & ~7u)

static const flash_ops_t *F;
static uint32_t active_sector;   // address
static uint32_t write_off;       // next free offset in the active sector
static uint32_t last_seq;

static bool rec_valid(const uint8_t *p, uint32_t room, rec_hdr_t *out) {
  rec_hdr_t h;
  if (room < sizeof(h)) return false;
  memcpy(&h, p, sizeof(h));
  if (h.magic != REC_MAGIC || h.version != SETTINGS_VERSION || h.len > REC_MAX_PAYLOAD) return false;
  if (sizeof(h) + h.len > room) return false;
  uint8_t buf[sizeof(rec_hdr_t) + REC_MAX_PAYLOAD];
  memcpy(buf, p, sizeof(h) + h.len);
  memset(buf + offsetof(rec_hdr_t, crc), 0, sizeof(h.crc));   // no cast: buf may be unaligned
  if (crc16_ccitt(buf, sizeof(h) + h.len) != h.crc) return false;
  *out = h;
  return true;
}

static bool erased(const uint8_t *p, uint32_t n) {
  while (n--) if (*p++ != 0xFF) return false;
  return true;
}

// walks a sector; returns the offset after the last record and the highest seq seen
static uint32_t scan(uint32_t sector, uint32_t *max_seq) {
  const uint8_t *s = F->ptr(sector);
  uint32_t off = 0;
  *max_seq = 0;
  while (off + sizeof(rec_hdr_t) <= STORE_SECTOR_SIZE) {
    rec_hdr_t h;
    if (!rec_valid(s + off, STORE_SECTOR_SIZE - off, &h)) {
      if (erased(s + off, sizeof(rec_hdr_t))) break;   // end of records
      off += 8;                                        // half-written record: skip it
      continue;
    }
    if (h.seq > *max_seq) *max_seq = h.seq;
    off += ALIGN8(sizeof(h) + h.len);
  }
  return off;
}

void store_init(const flash_ops_t *ops) {
  F = ops;
  uint32_t seqA, seqB;
  uint32_t offA = scan(F->base, &seqA);
  uint32_t offB = scan(F->base + STORE_SECTOR_SIZE, &seqB);
  if (seqB > seqA) { active_sector = F->base + STORE_SECTOR_SIZE; write_off = offB; last_seq = seqB; }
  else             { active_sector = F->base;                     write_off = offA; last_seq = seqA; }
}

static bool find_latest(uint32_t sector, uint8_t type, const uint8_t **rec, rec_hdr_t *hdr) {
  const uint8_t *s = F->ptr(sector);
  uint32_t off = 0;
  bool found = false;
  while (off + sizeof(rec_hdr_t) <= STORE_SECTOR_SIZE) {
    rec_hdr_t h;
    if (!rec_valid(s + off, STORE_SECTOR_SIZE - off, &h)) {
      if (erased(s + off, sizeof(rec_hdr_t))) break;
      off += 8;
      continue;
    }
    if (h.type == type && (!found || h.seq > hdr->seq)) { *rec = s + off; *hdr = h; found = true; }
    off += ALIGN8(sizeof(h) + h.len);
  }
  return found;
}

// newest record of a type in either sector
static bool latest_any(uint8_t type, const uint8_t **rec, rec_hdr_t *hdr) {
  const uint8_t *ra = 0, *rb = 0;
  rec_hdr_t ha, hb;
  bool a = find_latest(F->base, type, &ra, &ha);
  bool b = find_latest(F->base + STORE_SECTOR_SIZE, type, &rb, &hb);
  if (!a && !b) return false;
  if (a && (!b || ha.seq > hb.seq)) { *rec = ra; *hdr = ha; }
  else                              { *rec = rb; *hdr = hb; }
  return true;
}

bool store_load(uint8_t type, void *data, uint16_t len) {
  const uint8_t *r; rec_hdr_t h;
  if (!latest_any(type, &r, &h)) return false;
  if (h.len != len) return false;   // other layout: ignore
  memcpy(data, r + sizeof(rec_hdr_t), len);
  return true;
}

static bool write_rec(uint32_t sector, uint32_t *off, uint8_t type, const void *data, uint16_t len) {
  uint8_t buf[ALIGN8(sizeof(rec_hdr_t) + REC_MAX_PAYLOAD)];
  uint32_t total = ALIGN8(sizeof(rec_hdr_t) + len);
  memset(buf, 0xFF, sizeof(buf));
  rec_hdr_t h = { REC_MAGIC, type, SETTINGS_VERSION, ++last_seq, len, 0 };
  memcpy(buf, &h, sizeof(h));
  memcpy(buf + sizeof(h), data, len);
  h.crc = crc16_ccitt(buf, sizeof(h) + len);
  memcpy(buf, &h, sizeof(h));
  if (*off + total > STORE_SECTOR_SIZE) return false;
  if (!F->program(sector + *off, buf, total)) return false;
  *off += total;
  return true;
}

bool store_save(uint8_t type, const void *data, uint16_t len) {
  if (len > REC_MAX_PAYLOAD) return false;
  uint32_t total = ALIGN8(sizeof(rec_hdr_t) + len);
  if (write_off + total <= STORE_SECTOR_SIZE) return write_rec(active_sector, &write_off, type, data, len);

  // active sector full: move to the other one, carrying the newest record of the other type
  uint32_t other = active_sector == F->base ? F->base + STORE_SECTOR_SIZE : F->base;
  uint8_t keep[REC_MAX_PAYLOAD];
  uint8_t keep_type = type == REC_SETTINGS ? REC_GAUGE : REC_SETTINGS;
  const uint8_t *r; rec_hdr_t h;
  bool have_keep = latest_any(keep_type, &r, &h);
  if (have_keep) memcpy(keep, r + sizeof(rec_hdr_t), h.len);
  if (!F->erase(other)) return false;
  uint32_t off = 0;
  if (!write_rec(other, &off, type, data, len)) return false;
  if (have_keep && !write_rec(other, &off, keep_type, keep, h.len)) return false;
  active_sector = other;
  write_off = off;
  return true;
}
