#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <SHA256.h>

extern "C" {
#include <detools.h>
}

// Firmware update over LoRa: an admin sends a delta (detools sequential patch, heatshrink 8/7) made
// from the firmware this node runs to the new one. Only requests heard with no hop between are taken
// (the caller checks that). The patch is applied while it arrives; the new image is checked against
// its SHA-256 before the node boots into it.
//
// Request (after the 4 byte REQ timestamp): [OTA_REQ_TYPE][op][args]
// Reply   (after the 4 byte tag):           [op][status][data]
//   INFO   -> [platform][state][flags][img_size u32][img_sha 32][max_new u32][ident...]
//             flags: OTA_FLAG_ENABLED (`set ota on`), OTA_FLAG_UNCONFIRMED (new image not confirmed yet)
//   BEGIN  [patch_size u32][new_size u32][new_sha 32][old_sha 32][chunk u8]
//          [freq_khz u32][bw_hz u32][sf][cr][timeout_s u16]          -> [chunk u8]
//          freq_khz 0 = stay on the current radio settings
//          the same BEGIN for a paused update goes on from where it stopped (STATUS says where)
//   DATA   [seq u16][chunk bytes]                                    -> no reply
//   STATUS                                                           -> [state][next_seq u16][received u32][written u32]
//   END    (all data sent)                                           -> boots into the new image when status is OK
//   ABORT                                                            -> back to idle and the normal radio settings
//   PAUSE  (the admin waits for its time on air)                     -> back to the normal radio settings, progress
//          kept for OTA_PAUSE_KEEP_MS (any request of the admin starts that again), BEGIN goes on

#define OTA_REQ_TYPE           0x40

#define OTA_OP_INFO            1
#define OTA_OP_BEGIN           2
#define OTA_OP_DATA            3
#define OTA_OP_STATUS          4
#define OTA_OP_END             5
#define OTA_OP_ABORT           6
#define OTA_OP_PAUSE           7

#define OTA_OK                 0
#define OTA_ERR_STATE          1
#define OTA_ERR_TOO_BIG        2
#define OTA_ERR_PATCH          3
#define OTA_ERR_HASH           4
#define OTA_ERR_UNSUPPORTED    5
#define OTA_ERR_FLASH          6
#define OTA_ERR_BUSY           7
#define OTA_ERR_NOT_DIRECT     8
#define OTA_ERR_BAD_ARG        9
#define OTA_ERR_OLD_IMAGE      10   // the delta was made from other firmware than this node runs
#define OTA_ERR_BAD_IMAGE      11   // the new image is not an app for this board
#define OTA_ERR_DISABLED       12   // `set ota on` first

#define OTA_STATE_IDLE         0
#define OTA_STATE_RECEIVING    1
#define OTA_STATE_READY        2    // new image written and checked, about to boot into it
#define OTA_STATE_FAILED       3
#define OTA_STATE_PAUSED       4    // the admin went quiet: back on the normal radio, progress kept a while

#define OTA_FLAG_ENABLED       0x01
#define OTA_FLAG_UNCONFIRMED   0x02

#define OTA_PLATFORM_NONE      0
#define OTA_PLATFORM_NRF52     1
#define OTA_PLATFORM_ESP32     2

#define OTA_MIN_CHUNK          16
#define OTA_MAX_CHUNK          160
#define OTA_DEFAULT_TIMEOUT    60    // seconds with nothing from the admin before the session is dropped
#define OTA_MAX_TIMEOUT        600
#define OTA_PAUSE_KEEP_MS      (30 * 60 * 1000UL)   // a paused update can go on for this long

class OtaTarget {
public:
  virtual uint8_t platform() const = 0;
  virtual uint32_t imageSize() = 0;                                   // the running firmware, as in its .bin
  virtual bool readImage(uint32_t offset, uint8_t* dest, size_t len) = 0;
  virtual uint32_t maxNewSize() = 0;
  virtual bool beginNew(uint32_t size) = 0;
  virtual bool writeNew(const uint8_t* src, size_t len) = 0;         // sequential, from offset 0
  virtual bool finishNew() = 0;                                       // everything written: flush
  virtual bool readNew(uint32_t offset, uint8_t* dest, size_t len) = 0;
  virtual void abortNew() = 0;
  virtual bool newLooksBootable() { return true; }                    // e.g. an image for another chip
  virtual bool isUnconfirmed() { return false; }                      // booted into a new image, not yet confirmed
  virtual void confirm() { }                                          // the new image works: keep it
  virtual void commit() = 0;                                          // boot into the new image; no return when it works
};

struct OtaRadioParams {
  float freq, bw;
  uint8_t sf, cr;
};

typedef bool (*OtaRadioCheck)(void* ctx, const OtaRadioParams& p);   // can this node's radio do it?

class LoraOta {
public:
  enum Action { ACT_NONE, ACT_SIDE_RADIO, ACT_RESTORE_RADIO, ACT_COMMIT };

private:
  OtaTarget* _target;
  const char* _ident;
  uint8_t _state, _err, _chunk;
  uint8_t _owner[8];
  uint32_t _patch_size, _new_size, _received, _written, _from_pos;
  uint16_t _next_seq;
  uint8_t _new_sha[32];
  uint32_t _img_size;
  uint8_t _img_sha[32];
  bool _img_sha_ok;
  unsigned long _last_activity;
  uint32_t _timeout_ms;
  Action _action;
  OtaRadioParams _side;
  bool _on_side;
  bool _enabled;
  unsigned long _paused_at;
  OtaRadioCheck _radio_check;
  void* _radio_ctx;
  detools_apply_patch_t _patch;

  static uint32_t getU32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
  static uint16_t getU16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
  static void putU32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
  static void putU16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }

  static int fromRead(void* arg, uint8_t* buf, size_t size) {
    LoraOta* self = (LoraOta*) arg;
    if (self->_from_pos + size > self->_img_size) return -1;
    if (!self->_target->readImage(self->_from_pos, buf, size)) return -1;
    self->_from_pos += size;
    return 0;
  }
  static int fromSeek(void* arg, int offset) {
    LoraOta* self = (LoraOta*) arg;
    int64_t pos = (int64_t)self->_from_pos + offset;
    if (pos < 0 || pos > (int64_t)self->_img_size) return -1;
    self->_from_pos = (uint32_t) pos;
    return 0;
  }
  static int toWrite(void* arg, const uint8_t* buf, size_t size) {
    LoraOta* self = (LoraOta*) arg;
    if (self->_written + size > self->_new_size) return -1;
    if (!self->_target->writeNew(buf, size)) return -1;
    self->_written += size;
    return 0;
  }

  bool computeImageSha() {
    if (_img_sha_ok) return true;
    _img_size = _target->imageSize();
    if (_img_size == 0) return false;
    SHA256 sha;
    uint8_t buf[256];
    for (uint32_t off = 0; off < _img_size; off += sizeof(buf)) {
      size_t n = _img_size - off < sizeof(buf) ? _img_size - off : sizeof(buf);
      if (!_target->readImage(off, buf, n)) return false;
      sha.update(buf, n);
    }
    sha.finalize(_img_sha, 32);
    _img_sha_ok = true;
    return true;
  }

  bool newImageMatches() {
    SHA256 sha;
    uint8_t buf[256];
    for (uint32_t off = 0; off < _new_size; off += sizeof(buf)) {
      size_t n = _new_size - off < sizeof(buf) ? _new_size - off : sizeof(buf);
      if (!_target->readNew(off, buf, n)) return false;
      sha.update(buf, n);
    }
    uint8_t got[32];
    sha.finalize(got, 32);
    return memcmp(got, _new_sha, 32) == 0;
  }

  void restoreRadio() {
    if (_on_side) _action = ACT_RESTORE_RADIO;
    _on_side = false;
  }

  bool held() const { return isActive() || _state == OTA_STATE_PAUSED; }

  void fail(uint8_t err) {
    if (_state == OTA_STATE_RECEIVING || _state == OTA_STATE_PAUSED) _target->abortNew();
    _state = OTA_STATE_FAILED;
    _err = err;
    restoreRadio();
  }

  int reply(uint8_t* out, uint8_t op, uint8_t status) {
    out[0] = op;
    out[1] = status;
    return 2;
  }

  void setTimeout(uint16_t timeout_s) {
    if (timeout_s == 0) timeout_s = OTA_DEFAULT_TIMEOUT;
    if (timeout_s > OTA_MAX_TIMEOUT) timeout_s = OTA_MAX_TIMEOUT;
    _timeout_ms = (uint32_t)timeout_s * 1000;
  }

  void startSide(bool on, const OtaRadioParams& side) {
    if (!on) return;
    _side = side;
    _on_side = true;
    _action = ACT_SIDE_RADIO;
  }

  int handleBegin(const uint8_t* sender_key, const uint8_t* args, size_t len, uint8_t* out, unsigned long now_ms) {
    if (len < 4 + 4 + 32 + 32 + 1 + 4 + 4 + 1 + 1 + 2) return reply(out, OTA_OP_BEGIN, OTA_ERR_BAD_ARG);
    uint32_t patch_size = getU32(&args[0]);
    uint32_t new_size = getU32(&args[4]);
    const uint8_t* new_sha = &args[8];
    const uint8_t* old_sha = &args[40];
    uint8_t chunk = args[72];
    uint32_t freq_khz = getU32(&args[73]);
    uint32_t bw_hz = getU32(&args[77]);
    uint8_t sf = args[81], cr = args[82];
    uint16_t timeout_s = getU16(&args[83]);

    if (_state == OTA_STATE_RECEIVING || _state == OTA_STATE_READY) {
      // the same BEGIN again (its reply was lost) is answered again; anything else waits for this one
      bool same = memcmp(_owner, sender_key, sizeof(_owner)) == 0 && _patch_size == patch_size && _new_size == new_size
                  && memcmp(_new_sha, new_sha, 32) == 0;
      if (!same) return reply(out, OTA_OP_BEGIN, OTA_ERR_BUSY);
      _last_activity = now_ms;
      int n = reply(out, OTA_OP_BEGIN, OTA_OK);
      out[n++] = _chunk;
      return n;
    }
    if (!_enabled) return reply(out, OTA_OP_BEGIN, OTA_ERR_DISABLED);
    bool resume = false;
    if (_state == OTA_STATE_PAUSED) {
      bool own = memcmp(_owner, sender_key, sizeof(_owner)) == 0;
      if (!own) return reply(out, OTA_OP_BEGIN, OTA_ERR_BUSY);
      resume = _patch_size == patch_size && _new_size == new_size && memcmp(_new_sha, new_sha, 32) == 0
               && memcmp(old_sha, _img_sha, 32) == 0;
      if (!resume) {   // the owner starts another update: the paused one is dropped
        _target->abortNew();
        _state = OTA_STATE_IDLE;
      }
    }
    if (patch_size == 0 || chunk < OTA_MIN_CHUNK || chunk > OTA_MAX_CHUNK || new_size == 0) {
      return reply(out, OTA_OP_BEGIN, OTA_ERR_BAD_ARG);
    }
    if (freq_khz != 0 && !(freq_khz >= 150000 && freq_khz <= 2500000 && bw_hz >= 7000 && bw_hz <= 500000
                           && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8)) {
      return reply(out, OTA_OP_BEGIN, OTA_ERR_BAD_ARG);
    }
    OtaRadioParams side;
    side.freq = (float)freq_khz / 1000.0f;
    side.bw = (float)bw_hz / 1000.0f;
    side.sf = sf;
    side.cr = cr;
    if (freq_khz != 0 && _radio_check && !_radio_check(_radio_ctx, side)) return reply(out, OTA_OP_BEGIN, OTA_ERR_BAD_ARG);
    if (resume) {
      setTimeout(timeout_s);
      _last_activity = now_ms;
      _state = OTA_STATE_RECEIVING;
      startSide(freq_khz != 0, side);
      int n = reply(out, OTA_OP_BEGIN, OTA_OK);
      out[n++] = _chunk;
      return n;
    }
    if (!computeImageSha()) return reply(out, OTA_OP_BEGIN, OTA_ERR_FLASH);
    if (memcmp(old_sha, _img_sha, 32) != 0) return reply(out, OTA_OP_BEGIN, OTA_ERR_OLD_IMAGE);
    if (new_size > _target->maxNewSize()) return reply(out, OTA_OP_BEGIN, OTA_ERR_TOO_BIG);
    if (!_target->beginNew(new_size)) return reply(out, OTA_OP_BEGIN, OTA_ERR_FLASH);

    memcpy(_owner, sender_key, sizeof(_owner));
    _patch_size = patch_size;
    _new_size = new_size;
    memcpy(_new_sha, new_sha, 32);
    _chunk = chunk;
    _received = _written = _from_pos = 0;
    _next_seq = 0;
    _err = OTA_OK;
    setTimeout(timeout_s);
    _last_activity = now_ms;
    detools_apply_patch_init(&_patch, fromRead, fromSeek, patch_size, toWrite, this);
    _state = OTA_STATE_RECEIVING;
    startSide(freq_khz != 0, side);
    int n = reply(out, OTA_OP_BEGIN, OTA_OK);
    out[n++] = _chunk;
    return n;
  }

  void handleData(const uint8_t* args, size_t len) {
    if (_state != OTA_STATE_RECEIVING || len < 2) return;
    uint16_t seq = getU16(args);
    const uint8_t* data = &args[2];
    size_t n = len - 2;
    if (seq != _next_seq) return;   // out of order: the admin learns where to go on from STATUS
    uint32_t left = _patch_size - _received;
    if (left == 0) return;
    size_t want = left < _chunk ? left : _chunk;
    if (n < want) return;
    n = want;                       // the rest is zero padding from the encryption
    int res = detools_apply_patch_process(&_patch, data, n);
    if (res < 0) {
      fail(OTA_ERR_PATCH);
      return;
    }
    _received += n;
    _next_seq++;
  }

  int handleEnd(uint8_t* out) {
    if (_state == OTA_STATE_READY) return reply(out, OTA_OP_END, OTA_OK);   // reply was lost, say it again
    if (_state != OTA_STATE_RECEIVING) return reply(out, OTA_OP_END, _state == OTA_STATE_FAILED ? _err : OTA_ERR_STATE);
    if (_received != _patch_size) return reply(out, OTA_OP_END, OTA_ERR_STATE);   // still missing data

    int res = detools_apply_patch_finalize(&_patch);
    if (res < 0 || (uint32_t)res != _new_size || _written != _new_size) {
      fail(OTA_ERR_PATCH);
      return reply(out, OTA_OP_END, _err);
    }
    if (!_target->finishNew()) {
      fail(OTA_ERR_FLASH);
      return reply(out, OTA_OP_END, _err);
    }
    if (!newImageMatches()) {
      fail(OTA_ERR_HASH);
      return reply(out, OTA_OP_END, _err);
    }
    if (!_target->newLooksBootable()) {
      fail(OTA_ERR_BAD_IMAGE);
      return reply(out, OTA_OP_END, _err);
    }
    _state = OTA_STATE_READY;
    _action = ACT_COMMIT;
    return reply(out, OTA_OP_END, OTA_OK);
  }

public:
  LoraOta(OtaTarget* target) : _target(target), _ident(""), _state(OTA_STATE_IDLE), _err(OTA_OK), _chunk(0),
                               _patch_size(0), _new_size(0), _received(0), _written(0), _from_pos(0),
                               _next_seq(0), _img_size(0), _img_sha_ok(false), _last_activity(0),
                               _timeout_ms(OTA_DEFAULT_TIMEOUT * 1000), _action(ACT_NONE), _on_side(false),
                               _enabled(false), _paused_at(0),
                               _radio_check(NULL), _radio_ctx(NULL) {
    memset(_owner, 0, sizeof(_owner));
    memset(&_side, 0, sizeof(_side));
  }

  void setIdent(const char* ident) { _ident = ident; }
  void setEnabled(bool on) { _enabled = on; }
  bool isEnabled() const { return _enabled; }
  void setRadioCheck(OtaRadioCheck check, void* ctx) { _radio_check = check; _radio_ctx = ctx; }
  bool isOnSideRadio() const { return _on_side; }
  bool isActive() const { return _state == OTA_STATE_RECEIVING || _state == OTA_STATE_READY; }
  uint8_t getState() const { return _state; }
  const OtaRadioParams& sideRadio() const { return _side; }

  // what the node has to do after the last call: switch radio, restore it, boot the new image
  Action takeAction() {
    Action a = _action;
    _action = ACT_NONE;
    return a;
  }

  // boot the new image; only returns when that failed
  void commit() {
    _target->commit();
    _state = OTA_STATE_FAILED;
    _err = OTA_ERR_FLASH;
    restoreRadio();
  }

  // a session nobody talks to any more is paused, so the node gets back to its normal radio settings;
  // what was received is kept for OTA_PAUSE_KEEP_MS, for the same BEGIN to go on with
  bool checkTimeout(unsigned long now_ms) {
    if (_state == OTA_STATE_RECEIVING) {
      if ((unsigned long)(now_ms - _last_activity) < _timeout_ms) return false;
      _state = OTA_STATE_PAUSED;
      _paused_at = now_ms;
      restoreRadio();
      return true;
    }
    if (_state == OTA_STATE_PAUSED && (unsigned long)(now_ms - _paused_at) >= OTA_PAUSE_KEEP_MS) {
      fail(OTA_ERR_STATE);
      return true;
    }
    return false;
  }

  // req starts at the OTA_REQ_TYPE byte. Returns the reply length (written to out), 0 for none.
  int handleRequest(const uint8_t* sender_key, bool is_zero_hop, const uint8_t* req, size_t len, uint8_t* out,
                    unsigned long now_ms) {
    if (len < 2 || req[0] != OTA_REQ_TYPE) return 0;
    uint8_t op = req[1];
    const uint8_t* args = &req[2];
    size_t args_len = len - 2;

    if (_target == NULL || _target->platform() == OTA_PLATFORM_NONE) return reply(out, op, OTA_ERR_UNSUPPORTED);
    if (!is_zero_hop) return op == OTA_OP_DATA ? 0 : reply(out, op, OTA_ERR_NOT_DIRECT);

    bool owner = memcmp(_owner, sender_key, sizeof(_owner)) == 0;
    if (op != OTA_OP_INFO && op != OTA_OP_BEGIN && held() && !owner) {
      return op == OTA_OP_DATA ? 0 : reply(out, op, OTA_ERR_BUSY);
    }
    if (owner && isActive()) _last_activity = now_ms;
    if (owner && _state == OTA_STATE_PAUSED) _paused_at = now_ms;

    switch (op) {
    case OTA_OP_INFO: {
      if (!computeImageSha()) return reply(out, op, OTA_ERR_FLASH);
      int n = reply(out, op, OTA_OK);
      out[n++] = _target->platform();
      out[n++] = _state;
      out[n++] = (_enabled ? OTA_FLAG_ENABLED : 0) | (_target->isUnconfirmed() ? OTA_FLAG_UNCONFIRMED : 0);
      putU32(&out[n], _img_size); n += 4;
      memcpy(&out[n], _img_sha, 32); n += 32;
      putU32(&out[n], _target->maxNewSize()); n += 4;
      size_t il = strlen(_ident);
      if (il > 60) il = 60;
      memcpy(&out[n], _ident, il); n += il;
      return n;
    }
    case OTA_OP_BEGIN:
      return handleBegin(sender_key, args, args_len, out, now_ms);
    case OTA_OP_DATA:
      handleData(args, args_len);
      return 0;
    case OTA_OP_STATUS: {
      int n = reply(out, op, _state == OTA_STATE_FAILED ? _err : OTA_OK);
      out[n++] = _state;
      putU16(&out[n], _next_seq); n += 2;
      putU32(&out[n], _received); n += 4;
      putU32(&out[n], _written); n += 4;
      return n;
    }
    case OTA_OP_END:
      return handleEnd(out);
    case OTA_OP_PAUSE:
      if (_state != OTA_STATE_RECEIVING) return reply(out, op, _state == OTA_STATE_PAUSED ? OTA_OK : OTA_ERR_STATE);
      _state = OTA_STATE_PAUSED;
      _paused_at = now_ms;
      restoreRadio();
      return reply(out, op, OTA_OK);
    case OTA_OP_ABORT:   // also cancels the reboot into an image already checked
      if (_state == OTA_STATE_RECEIVING || _state == OTA_STATE_PAUSED) _target->abortNew();
      if (_state != OTA_STATE_IDLE) _action = ACT_RESTORE_RADIO;   // also cancels a reboot after END
      _on_side = false;
      _state = OTA_STATE_IDLE;
      _err = OTA_OK;
      memset(_owner, 0, sizeof(_owner));
      return reply(out, op, OTA_OK);
    default:
      return reply(out, op, OTA_ERR_BAD_ARG);
    }
  }
};
