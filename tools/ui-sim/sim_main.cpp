// Desktop screen simulator for the encoder companion UI (examples/companion_radio/ui-encoder).
// Runs the real UITask against a scripted mesh and a 128x64 framebuffer, and saves every step of
// the scenario as a PNG (4x scale), in Bulgarian and English. See README.md.

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <new>
#include "target.h"
#include "UISim.h"
#include "UITask.h"
#include "glcdfont.h"
#include <helpers/ui/CyrillicFont5x8.h>
#include <helpers/ui/TextFont.h>

// ---------------------------------------------------------------- hardware stand-ins

unsigned long sim_millis = 0;
SimBoard board;
SimRadio radio_driver;
SimClock rtc_clock;
SensorManager sensors;
SimButton user_btn;
SimButton encoder_btn;
SimRotary sim_rotary;
RotaryInput& rotary_input = sim_rotary;
SimMesh the_mesh;

static UITask* ui_task = NULL;

// NodePrefs' radio part: the CLI side is not used by the screens
bool CommonRadioPrefs::handleCommand(const char*, uint32_t, char*) { return false; }
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }

void StrHelper::strncpy(char* dest, const char* src, size_t buf_sz) {   // as in TxtDataHelpers.cpp
  while (buf_sz > 1 && *src) { *dest++ = *src++; buf_sz--; }
  *dest = 0;
}
void delay(unsigned long ms) { sim_millis += ms; }

ColorVal UIColor::window_bkg = 0, UIColor::title_bkg = 0, UIColor::title_txt = 1, UIColor::primary_txt = 1,
         UIColor::secondary_txt = 1, UIColor::warning_txt = 1, UIColor::popup_bkg = 0, UIColor::popup_txt = 1,
         UIColor::corp_blue = 1;

// ---------------------------------------------------------------- display: SSD1306 + Adafruit GFX text

class SimDisplay : public DisplayDriver {
  uint8_t _fb[64][128];
  uint8_t _shown[64][128];
  bool _on = false;
  int _cx = 0, _cy = 0, _size = 1;
  uint8_t _color = 1;
  uint8_t _lead = 0, _skip = 0;
  const TextFont* _font = NULL;
public:
  int contrast = 0xFF;
  SimDisplay() : DisplayDriver(128, 64) { memset(_fb, 0, sizeof(_fb)); memset(_shown, 0, sizeof(_shown)); }
  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override { _on = false; }
  void setContrast(uint8_t level) override { contrast = level; }
  void clear() override { memset(_fb, 0, sizeof(_fb)); }
  void startFrame(ColorVal) override { memset(_fb, 0, sizeof(_fb)); _color = 1; _size = 1; _font = NULL; }
  void setTextSize(int sz) override { _size = sz; _font = NULL; }
  void setTextFont(const TextFont* f) override { _size = 1; _font = f; }
  void setColor(ColorVal c) override { _color = c ? 1 : 0; }
  void setCursor(int x, int y) override { _cx = x; _cy = y; }
  void pixel(int x, int y, uint8_t c) { if (x >= 0 && x < 128 && y >= 0 && y < 64) _fb[y][x] = c; }
  void fillRect(int x, int y, int w, int h) override {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) pixel(i, j, _color);
  }
  void drawRect(int x, int y, int w, int h) override {
    for (int i = x; i < x + w; i++) { pixel(i, y, _color); pixel(i, y + h - 1, _color); }
    for (int j = y; j < y + h; j++) { pixel(x, j, _color); pixel(x + w - 1, j, _color); }
  }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {   // Adafruit drawBitmap: MSB first
    int bw = (w + 7) / 8;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
      if (bits[j * bw + i / 8] & (0x80 >> (i & 7))) pixel(x + i, y + j, _color);
    }
  }
  void fontGlyph(uint16_t cp) {   // SSD1306Utf8::drawCodepoint / drawBlock
    if (_cx + _font->w > 128) { _cx = 0; _cy += _font->h; }
    int idx = textFontIndex(cp);
    if (idx < 0) fillRect(_cx, _cy + 1, _font->w - 1, _font->h - 2);
    else drawXbm(_cx, _cy, textFontGlyph(_font, idx), _font->w, _font->h);
    _cx += _font->w;
  }
  void glyph(const uint8_t* cols) {
    if (_font) { fontGlyph(0xFFFF); return; }   // with a custom font glyph() only draws blocks
    if (_cx + _size * 6 > 128) { _cx = 0; _cy += _size * 8; }   // Adafruit wraps at the right edge
    for (int i = 0; i < 5; i++) {
      uint8_t line = cols[i];
      for (int j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) for (int a = 0; a < _size; a++) for (int b = 0; b < _size; b++) pixel(_cx + i * _size + a, _cy + j * _size + b, _color);
      }
    }
    _cx += _size * 6;
  }
  void writeByte(uint8_t c) {   // SSD1306Utf8::write
    if (_skip) { if ((c & 0xC0) == 0x80) { _skip--; return; } _skip = 0; }
    if (_lead) {
      if ((c & 0xC0) == 0x80) {
        uint16_t cp = ((uint16_t)(_lead & 0x1F) << 6) | (c & 0x3F);
        _lead = 0;
        if (_font) { fontGlyph(cp); return; }
        int idx = cyrillicGlyphIndex(cp);
        if (idx >= 0) glyph(cyrillic_font_5x8[idx]); else glyph(&font[0xDB * 5]);
        return;
      }
      _lead = 0;
      glyph(&font[0xDB * 5]);
    }
    if (c == '\n') { _cx = 0; _cy += _font ? _font->h : _size * 8; return; }
    if (c == '\r') return;
    if (c < 0x80) { if (_font) fontGlyph(c); else glyph(&font[c * 5]); return; }
    if ((c & 0xE0) == 0xC0) { _lead = c; return; }
    if ((c & 0xF0) == 0xE0) { _skip = 2; glyph(&font[0xDB * 5]); return; }
    if ((c & 0xF8) == 0xF0) { _skip = 3; glyph(&font[0xDB * 5]); return; }
    glyph(&font[c * 5]);
  }
  void print(const char* s) override {
    for (const uint8_t* p = (const uint8_t*)s; *p; p++) writeByte(*p);
    if (_lead) { _lead = 0; glyph(&font[0xDB * 5]); }
    _skip = 0;
  }
  uint16_t getTextWidth(const char* str) override {
    uint16_t n = 0;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) if ((*p & 0xC0) != 0x80) n++;
    return n * (_font ? _font->w : 6 * _size);
  }
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    size_t j = 0;
    for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];
      if (c >= 32 && c <= 126) {
        dest[j++] = c;
      } else if ((c & 0xE0) == 0xC0 && ((unsigned char)src[i+1] & 0xC0) == 0x80) {
        uint16_t cp = ((uint16_t)(c & 0x1F) << 6) | ((unsigned char)src[i+1] & 0x3F);
        if (cyrillicGlyphIndex(cp) >= 0 && j + 2 < dest_size) { dest[j++] = c; dest[j++] = src[i+1]; }
        else dest[j++] = '\xDB';
        i++;
      } else if (c >= 0x80) {
        dest[j++] = '\xDB';
        while (src[i+1] && ((unsigned char)src[i+1] & 0xC0) == 0x80) i++;
      }
    }
    dest[j] = 0;
  }
  void endFrame() override { memcpy(_shown, _fb, sizeof(_fb)); }
  uint8_t shown(int x, int y) const { return _shown[y][x]; }
};

static SimDisplay display;
void SimBoard::powerOff() { display.turnOff(); }

// ---------------------------------------------------------------- PNG (uncompressed deflate, no libraries)

static uint32_t crc_table[256];
static void crcInit() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }
}
static uint32_t crc(const uint8_t* b, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void put32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> v;
  put32(v, data.size());
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  v.insert(v.end(), td.begin(), td.end());
  put32(v, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
  fwrite(v.data(), 1, v.size(), f);
}

#define SCALE 4
#define BORDER 8

static void savePng(const char* path) {
  const int W = 128 * SCALE + 2 * BORDER, H = 64 * SCALE + 2 * BORDER;
  std::vector<uint8_t> raw;
  // OLED look: light blue-white pixels, black panel, dark grey bezel; brightness follows contrast
  int lit = 120 + display.contrast * 135 / 255;
  for (int y = 0; y < H; y++) {
    raw.push_back(0);   // filter: none
    for (int x = 0; x < W; x++) {
      int px = (x - BORDER) / SCALE, py = (y - BORDER) / SCALE;
      bool inside = x >= BORDER && y >= BORDER && px < 128 && py < 64;
      uint8_t r = 40, g = 40, b = 44;
      if (inside) {
        bool gap = ((x - BORDER) % SCALE == SCALE - 1) || ((y - BORDER) % SCALE == SCALE - 1);
        if (display.isOn() && display.shown(px, py) && !gap) { r = lit * 200 / 255; g = lit * 235 / 255; b = lit; }
        else { r = 6; g = 8; b = 12; }
      }
      raw.push_back(r); raw.push_back(g); raw.push_back(b);
    }
  }
  std::vector<uint8_t> z = { 0x78, 0x01 };
  size_t pos = 0;
  uint32_t a = 1, bsum = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; bsum = (bsum + a) % 65521; }
  while (pos < raw.size()) {
    size_t n = raw.size() - pos; if (n > 65535) n = 65535;
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  put32(z, (bsum << 16) | a);
  FILE* f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, W); put32(ihdr, H);
  ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", std::vector<uint8_t>());
  fclose(f);
}

// ---------------------------------------------------------------- the scripted mesh

ContactInfo* SimMesh::addContact(const char* name, uint8_t type, uint8_t key0, uint32_t lastmod, uint8_t path_len) {
  ContactInfo& c = contacts[MAX_ANON_CONTACTS + num_contacts++];
  memset(&c, 0, sizeof(c));
  for (int i = 0; i < PUB_KEY_SIZE; i++) c.id.pub_key[i] = key0 + i;
  StrHelper::strncpy(c.name, name, sizeof(c.name));
  c.type = type;
  c.lastmod = lastmod;
  c.out_path_len = path_len;
  // the path goes through Repeater Lozen (0x80), then repeaters without a contact
  if (path_len != OUT_PATH_UNKNOWN) for (int i = 0; i < (path_len & 63); i++) c.out_path[i] = i == 0 ? 0x80 : 0xC0 + i;
  return &c;
}
void SimMesh::addChannel(int idx, const char* name) {
  StrHelper::strncpy(channels[idx].name, name, 32);
  for (int i = 0; i < 16; i++) channels[idx].channel.secret[i] = 0x31 * (idx + 1) + i;   // a key, so it can be muted
}
void SimMesh::addRecent(const char* name, uint32_t ts) {
  AdvertPath& a = recent[num_recent++];
  memset(&a, 0, sizeof(a));
  StrHelper::strncpy(a.name, name, sizeof(a.name));
  a.recv_timestamp = ts;
}
bool SimMesh::getChannel(int idx, ChannelDetails& dest) {
  if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
  dest = channels[idx];
  return true;
}
ContactInfo* SimMesh::lookupContactByPubKey(const uint8_t* key, int prefix_len) {
  for (int i = 0; i < num_contacts; i++) {
    ContactInfo* c = &contacts[MAX_ANON_CONTACTS + i];
    if (memcmp(c->id.pub_key, key, prefix_len) == 0) return c;
  }
  return NULL;
}
bool SimMesh::getContactByIdx(uint32_t idx, ContactInfo& c) {
  if (idx >= (uint32_t)(MAX_ANON_CONTACTS + num_contacts)) return false;
  c = contacts[idx];
  return true;
}
bool SimMesh::sendGroupMessage(uint32_t, mesh::GroupChannel&, const char*, const char* text, int) {
  snprintf(last_sent, sizeof(last_sent), "%s", text);
  return tx_allowed;
}
int SimMesh::sendMessage(const ContactInfo&, uint32_t, uint8_t, const char* text, uint32_t& ack, uint32_t& timeout) {
  snprintf(last_sent, sizeof(last_sent), "%s", text);
  ack = last_ack = ++next_ack;
  timeout = 8000;
  return MSG_SEND_SENT_FLOOD;
}
int SimMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  int n = max_num < num_recent ? max_num : num_recent;
  for (int i = 0; i < n; i++) dest[i] = recent[i];
  return n;
}

// ---------------------------------------------------------------- scenario helpers

static std::string out_dir = "out";
static std::string lang_tag;
static int shot_no = 0;
static FILE* manifest = NULL;

static void run(unsigned long ms) {   // let the UI run for a while, 10 ms per loop
  for (unsigned long t = 0; t < ms; t += 10) { ui_task->loop(); sim_millis += 10; }
  ui_task->loop();
}
static void press()    { encoder_btn.push(BUTTON_EVENT_CLICK); run(50); }
static void hold()     { encoder_btn.push(BUTTON_EVENT_LONG_PRESS); run(50); }
static void back()     { user_btn.push(BUTTON_EVENT_CLICK); run(50); }
static void holdBack() {   // to standby; on standby it locks the keys, so unlock again
  user_btn.push(BUTTON_EVENT_LONG_PRESS); run(50);
  if (ui_task->isLocked()) { user_btn.push(BUTTON_EVENT_LONG_PRESS); run(900); }
}
static void turn(int n) {   // slow turn: one detent at a time (no acceleration)
  for (int i = 0; i < (n < 0 ? -n : n); i++) { sim_rotary.turn(n < 0 ? -1 : 1); run(150); }
}
static void spin(int n) { sim_rotary.turn(n); run(50); }   // fast spin: all detents at once

static void shotFrame(const char* name, const char* caption) {   // the frame as it is now
  char path[256];
  snprintf(path, sizeof(path), "%s/%02d_%s_%s.png", out_dir.c_str(), ++shot_no, name, lang_tag.c_str());
  savePng(path);
  if (manifest) fprintf(manifest, "%s\t%s\t%s\n", lang_tag.c_str(), path + out_dir.size() + 1, caption);
}

static void shot(const char* name, const char* caption) {
  ui_task->loop();
  shotFrame(name, caption);
}

static void receive(UITask& ui, bool channel, int ch_idx, const ContactInfo* from, const char*, const char* text) {
  if (!channel) {   // as the mesh delivers it: with the signal of the packet
    mesh::Packet pkt;
    pkt._snr = 27;   // +6.75 dB
    pkt.path_len = 2;
    ui.onMessageRecv(&pkt, *from, TXT_TYPE_PLAIN, 0, text);
    radio_driver.recv++;
    return;
  }
  mesh::Packet pkt;   // a channel message, also as the mesh delivers it (mute, mentions)
  pkt._snr = 18;
  pkt.path_len = 1;
  ChannelDetails ch;
  the_mesh.getChannel(ch_idx, ch);
  ui.onChannelMessageRecv(&pkt, ch, text);
  radio_driver.recv++;
}

static void scenario(uint8_t lang) {
  static NodePrefs prefs;
  prefs.~NodePrefs(); new (&prefs) NodePrefs();   // defaults, also for the fields added later
  StrHelper::strncpy(prefs.node_name, "Petko-Z7", sizeof(prefs.node_name));
  prefs.freq = 869.525f; prefs.sf = 11; prefs.cr = 5; prefs.bw = 250.0f; prefs.tx_power_dbm = 22;
  prefs.ui_lang = lang; prefs.tz_offset = 2; prefs.ui_dst = 1;
  prefs.ui_off = 3;   // 5 min, so nothing times out during the script

  // millis() keeps running into the second language: the tools keep their state in statics
  shot_no = 0;
  lang_tag = lang == 0 ? "bg" : "en";
  the_mesh = SimMesh();
  the_mesh.prefs = &prefs;
  for (int i = 0; i < PUB_KEY_SIZE; i++) the_mesh.self_id.pub_key[i] = 0xA0 + i;
  rtc_clock.base = 0;
  board.mv = 3920; board.external = false;
  radio_driver.recv = 0;

  static MultiSerialInterface serial;
  UITask* ui = new UITask(&board, &serial);
  ui_task = ui;
  uint32_t now0 = 1791710400UL;   // 2026-10-11 06:40 UTC
  bool bg = lang == 0;
  ContactInfo* ivan  = the_mesh.addContact(bg ? "Иван" : "Ivan", ADV_TYPE_CHAT, 0x10, now0 - 300, 2);
  ContactInfo* maria = the_mesh.addContact(bg ? "Мария" : "Maria", ADV_TYPE_CHAT, 0x20, now0 - 4000, 0);
  the_mesh.addContact(bg ? "Георги Петров" : "George Peterson", ADV_TYPE_CHAT, 0x30, now0 - 90000, OUT_PATH_UNKNOWN);
  the_mesh.addContact("Alice", ADV_TYPE_CHAT, 0x40, now0 - 600, 1);
  the_mesh.addContact("Bob", ADV_TYPE_CHAT, 0x50, now0 - 7200, 3);
  the_mesh.addContact(bg ? "Клуб Витоша" : "Vitosha club", ADV_TYPE_ROOM, 0x60, now0 - 1800, 2);
  the_mesh.addContact(bg ? "Добри" : "Dobri", ADV_TYPE_CHAT, 0x70, now0 - 120, 1);
  the_mesh.addContact("Repeater Lozen", ADV_TYPE_REPEATER, 0x80, now0 - 60, 1);
  the_mesh.addChannel(0, "Public");
  the_mesh.addChannel(1, "bg-mesh");

  ui->begin(&display, &sensors, &prefs);
  run(200);
  shot("splash", bg ? "Начален екран" : "Splash");
  run(2600);
  shot("antenna", bg ? "Въпрос за антената: нищо не е избрано, първото движение избира" : "Antenna question: nothing preselected, the first move picks");
  press();
  shot("antenna_nudge", bg ? "Enter преди избор: подсказка" : "Enter before a choice: a hint");
  turn(-1);
  shot("antenna_yes", bg ? "Антена: избрано Да (Back = не, само приемане)" : "Antenna: Yes picked (Back = no, receive only)");
  press();
  run(1300);

  // standby before any clock or message
  shot("standby_empty", bg ? "Standby без час и съобщения: BT PIN и последно чут" : "Standby without clock or messages: BT PIN, last heard");
  rtc_clock.setCurrentTime(now0 + 1);
  the_mesh.addRecent("Repeater Lozen", now0 - 30);
  the_mesh.addRecent(bg ? "Добри" : "Dobri", now0 - 90);
  the_mesh.addRecent("Alice", now0 - 600);
  radio_driver.recv = 3;
  run(100);
  turn(1); shot("card_recent", bg ? "Карта: чути наскоро" : "Card: recently heard");
  turn(1); shot("card_radio", bg ? "Карта: радио" : "Card: radio");
  turn(1); shot("card_battery", bg ? "Карта: батерия" : "Card: battery");
  board.external = true; ui->refresh(); run(100);
  shot("card_battery_usb", bg ? "Карта: батерия при USB (мълния в лентата)" : "Card: battery on USB (bolt in the bar)");
  board.external = false; ui->refresh();
  turn(1);

  // messages arrive
  receive(*ui, false, 0, ivan, NULL, bg ? "Къде си? Чакам те пред входа от 10 минути." : "Where are you? Waiting at the entrance for 10 minutes.");
  run(500);
  shot("standby_new", bg ? "Ново съобщение: инверсна лента за 3 s" : "New message: inverted for 3 s");
  run(3000);
  receive(*ui, true, 1, NULL, "bg-mesh", bg ? "Добри: Някой чува ли ме от Лозен?" : "Dobri: Can anyone hear me from Lozen?");
  receive(*ui, false, 0, maria, NULL, bg ? "Ок, до после" : "OK, see you later");
  run(3200);
  shot("standby_unread", bg ? "Standby: 3 непрочетени" : "Standby: 3 unread");

  // press on standby opens the conversation of the newest unread
  press();
  shot("thread", bg ? "Натискане от standby: разговорът с най-новото непрочетено, като чат" : "Press on standby: the conversation of the newest unread, as a chat");
  back();
  shot("conversations", bg ? "Разговори: икона, брой непрочетени или възраст" : "Conversations: icon, unread count or age");
  hold();
  shot("conv_actions", bg ? "Задържане: действия за разговор" : "Hold: conversation actions");
  turn(1); press();
  shot("confirm_delete_all", bg ? "Изтрий всички иска потвърждение" : "Delete all asks first");
  back();

  // reply: conversation -> thread, press = what to send
  turn(2); press();
  shot("thread_ivan", bg ? "Разговорът с Иван: съобщението се вижда цялото" : "The conversation with Ivan: the whole message shows");
  hold(); turn(1); press();
  shot("msg_info_in", bg ? "Задържане > Инфо: час, маршрут, сигнал" : "Hold > Details: time, route, signal");
  back();
  press();
  shot("reply_pick", bg ? "Натискане = отговор: Напиши... и готовите" : "Press = reply: Write... and quick replies");
  turn(2); press();
  run(1200);
  shot("send_undo", bg ? "3 s до изпращане, Back отказва" : "3 s before sending, Back cancels");
  run(2000);
  shot("sent_alert", bg ? "Изпратено, обратно в разговора" : "Sent, back to the conversation");
  run(1200);
  shot("thread_pending", bg ? "В разговора: изпратеното чака ACK (часовник)" : "In the thread: the sent one waits for an ACK (clock)");
  uint8_t ackb[4]; memcpy(ackb, &the_mesh.last_ack, 4);
  ui->ackReceived(ackb);
  run(100);
  shot("thread_acked", bg ? "ACK получен: отметка и кратка вибрация" : "ACK received: check mark and a short buzz");

  // a message that is not delivered: hold on Ivan's message > Reply > a quick reply
  hold(); press(); turn(1); press();
  run(3100);
  run(31000);   // three attempts (the last by flood), each waits for its ACK
  shot("thread_failed", bg ? "Без ACK след 3 опита: кръстче, известие и вибрация" : "No ACK after 3 attempts: cross, alert and a buzz");
  run(2000);
  turn(12); hold();
  shot("msg_actions_retry", bg ? "Задържане на недоставено: Изпрати пак" : "Hold on an undelivered one: Send again");
  turn(2); press();
  shot("msg_info_out", bg ? "Инфо за изпратено: опити и състояние" : "Details of a sent one: attempts and state");
  back();
  hold(); turn(3); press();
  shot("confirm_delete_one", bg ? "Изтриване на едно съобщение (потвърждението се изключва от настройките)" : "Deleting one message (confirmation can be turned off)");
  back();
  holdBack();

  // contacts and the keyboard
  ui->markAllRead();     // otherwise a press on standby opens the unread message
  press();               // main menu
  turn(1); press();      // contacts
  shot("contacts", bg ? "Контакти по име, възраст вдясно" : "Contacts by name, age on the right");
  turn(-1);
  shot("contacts_letters", bg ? "Наляво от първия: ред с букви" : "Left from the first: letter row");
  turn(1);
  hold();
  shot("contact_actions", bg ? "Задържане на контакт" : "Hold on a contact");
  turn(2); press();
  shot("contact_info", bg ? "Информация за контакт" : "Contact details");
  back();
  hold(); turn(1); press();
  shot("trace_wait", bg ? "Трасиране: чака отговор" : "Trace: waiting for the answer");
  {
    mesh::Packet pkt;
    pkt._snr = 22;
    uint8_t snrs[1] = { (uint8_t)(int8_t)34 };
    uint8_t hashes[1] = { 0x80 };
    run(1400);
    ui->onTraceRecv(&pkt, the_mesh.last_tag, 0, 0, snrs, hashes, 1);
    run(100);
  }
  shot("trace_done", bg ? "Трасиране: SNR на всеки скок и времето" : "Trace: SNR at each hop and the round trip");
  back();
  press();               // contacts: press = write
  press();               // Write...
  shot("keyboard", bg ? "Клавиатура" : "Keyboard");
  turn(7); press();      // "з" / "h"
  shot("keyboard_suggest", bg ? "Предложена дума: курсорът е на нея, натискане я приема" : "Suggested word: the cursor is on it, a press takes it");
  turn(1);
  shot("keyboard_jump", bg ? "Завъртане: обратно на клавишите, на следващата вероятна буква" : "Turn: back on the keys, at the likely next letter");
  press();
  shot("keyboard_suggest2", bg ? "Втора буква: нова подсказка" : "Second letter: a new suggestion");
  press();
  shot("keyboard_accepted", bg ? "Натискане: думата е приета, с интервал" : "Press: the word is taken, with a space");
  hold();
  shot("compose_menu", bg ? "Задържане: меню на клавиатурата" : "Hold: keyboard menu");
  back();
  holdBack();

  // menus
  press();
  shot("main_menu", bg ? "Главно меню" : "Main menu");
  turn(2); press();
  shot("channels", bg ? "Канали: възраст на последното съобщение" : "Channels: age of the last message");
  turn(1); press();
  shot("channel_thread", bg ? "Натискане на канал: неговият разговор" : "Press on a channel: its conversation");
  back();
  hold();
  shot("channel_actions", bg ? "Задържане на канал: пиши, заглуши, изтрий" : "Hold on a channel: write, mute, delete");
  turn(1); press(); run(900);
  shot("channels_muted", bg ? "Заглушен канал: без вибрация и светване" : "Muted channel: no buzz, screen stays off");
  turn(1); press();
  shot("channel_new", bg ? "+ Нов канал: име на #канал с клавиатурата" : "+ New channel: a #channel name on the keyboard");
  back();
  back();

  // network tools
  turn(1); press();
  shot("network_menu", bg ? "Мрежа" : "Network");
  press();
  shot("nearby_wait", bg ? "Наблизо: търси ретранслатори" : "Nearby: looking for repeaters");
  {
    static const struct { uint8_t key0; int8_t in4, out4; } R[] = { { 0x80, 38, 30 }, { 0xC1, -14, -6 }, { 0xD7, 9, 18 } };
    for (auto& r : R) {
      mesh::Packet pkt;
      pkt._snr = r.in4;
      pkt.payload[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;
      pkt.payload[1] = (uint8_t)r.out4;
      memcpy(&pkt.payload[2], &the_mesh.last_tag, 4);
      for (int i = 0; i < 8; i++) pkt.payload[6 + i] = r.key0 + i;
      pkt.payload_len = 14;
      run(700);
      ui->onControlDataRecv(&pkt);
    }
    run(200);
  }
  shot("nearby", bg ? "Наблизо: по сила на сигнала (SNR)" : "Nearby: by signal strength (SNR)");
  press();
  shot("nearby_node", bg ? "Ретранслатор: как ни чува и как го чуваме" : "A repeater: how it hears us and how we hear it");
  back(); back();
  the_mesh.air_total = 3100;
  turn(1); press();
  shot("airtime", bg ? "Ефирно време: последния час срещу лимита 10%" : "Airtime: the last hour against the 10% limit");
  back();
  turn(1); press();
  shot("range_empty", bg ? "Тест на обхвата" : "Range test");
  press();
  shot("range_pick", bg ? "Цел: ретранслатори, после контакти през тях" : "Target: repeaters, then contacts reached through them");
  press();
  turn(1); press();   // start
  for (int i = 0; i < 4; i++) {   // three answers, one miss
    run(1500);
    if (i != 2) {
      mesh::Packet pkt;
      pkt._snr = 20 - 6 * i;
      uint8_t snrs[1] = { (uint8_t)(int8_t)(28 - 5 * i) };
      uint8_t hashes[1] = { 0x80 };
      ui->onTraceRecv(&pkt, the_mesh.last_tag, 0, 0, snrs, hashes, 1);
    }
    run(28500);
  }
  turn(3);
  shot("range_running", bg ? "Тест на обхвата: SNR натам/обратно, -- = без отговор" : "Range test: SNR there/back, -- = no answer");
  holdBack();
  shot("standby_range", bg ? "Тестът на обхвата върви: икона в лентата" : "The range test runs: an icon in the status row");
  prefs.setRepeatEn(true); ui->refresh(); run(50);
  shot("standby_repeat", bg ? "Препредаване + тест на обхвата: две икони" : "Repeat + range test: two icons");
  prefs.setRepeatEn(false);
  press(); turn(3); press(); turn(2); press();   // Menu > Network > Range test
  turn(1); press();   // stop
  back(); back();

  turn(1); press();
  shot("settings", bg ? "Настройки: групи" : "Settings: groups");
  for (int g = 0; g < 5; g++) {
    press();
    char name[24]; snprintf(name, sizeof(name), "settings_%d", g);
    static const char* GB[] = { "Настройки: екран", "Настройки: управление", "Настройки: съобщения", "Настройки: радио", "Настройки: система" };
    static const char* GE[] = { "Settings: display", "Settings: controls", "Settings: messages", "Settings: radio", "Settings: system" };
    shot(name, bg ? GB[g] : GE[g]);
    if (g == 0) {
      press(); turn(2); shot("edit_menu_size", bg ? "Размер на менютата (стойността е в този размер)" : "Menu size (the value is shown in that size)"); back();
      turn(3); press(); turn(1); shot("edit_brightness", bg ? "Редакция на стойност (яркостта се вижда веднага)" : "Value editor (brightness previews live)"); back();
    }
    if (g == 1) { turn(4); press(); shot("edit_lock", bg ? "Заключване: ръчно или при изгасване" : "Key lock: by hand or when the screen goes off"); back(); }
    if (g == 2) {
      turn(1); press(); shot("edit_wake", bg ? "Кога светва екранът за съобщение" : "When a message turns the screen on"); back();
      turn(4); press(); shot("words", bg ? "Научени думи: изтриване (още няма)" : "Learned words: clear (none yet)"); run(2000);
    }
    if (g == 3) {
      turn(2); press(); turn(-1); press(); shot("antenna_never", bg ? "Антена: никога иска потвърждение" : "Antenna: never asks first"); back(); back();
      turn(1); press(); turn(1); shot("edit_preset", bg ? "Радио пресет" : "Radio preset"); back();
      turn(1); press(); turn(1); press();
      shot("repeat_q", bg ? "Препредаване: само при авария" : "Repeat: emergencies only"); back(); back();
    }
    if (g == 4) {
      turn(2); press(); shot("edit_tz", bg ? "Часова зона" : "Time zone"); back();
      turn(2); press(); turn(3); shot("edit_battery", bg ? "Калибрация: въведи волтажа от мултиметъра" : "Calibration: enter the multimeter reading"); back();
      turn(1); press(); shot("about", bg ? "За устройството" : "About"); back();
      turn(1); press(); shot("hardware", bg ? "Хардуер: тестове и измервания" : "Hardware: tests and readings");
      press(); turn(3); press(); hold(); turn(-1);
      shot("encoder_test", bg ? "Тест на енкодера: завъртания, натискания, задържания" : "Encoder test: turns, presses, holds");
      back(); turn(1); press();
      shot("edit_input", bg ? "Вход: Авто, Енкодер или Бутони" : "Input: Auto, Encoder or Buttons");
      back(); turn(2); press();
      shot("display_test", bg ? "Тест на дисплея: подсказка на първия образ" : "Display test: a hint on the first pattern");
      back();
      sim_rotary.setMode(RotaryInputMode::Buttons);   // two buttons on the encoder pins instead
      ui->refresh(); run(50);
      shot("hardware_buttons", bg ? "Хардуер с два бутона вместо енкодер" : "Hardware with two buttons instead of the encoder");
      press();
      shot("display_test_buttons", bg ? "Тест на дисплея с бутони: Натисни" : "Display test with buttons: Press");
      back();
      sim_rotary.setMode(RotaryInputMode::Encoder);
      back();
      turn(2); shot("settings_update", bg ? "Ъпдейт на фърмуера от менюто" : "Firmware update from the menu");
      press(); shot("update_usb_q", bg ? "Рестарт в bootloader-а (USB диск за UF2)" : "Restart into the bootloader (USB drive for a UF2)"); back();
    }
    back();
    turn(1);
  }
  holdBack();

  // key lock: hold Back on standby
  user_btn.push(BUTTON_EVENT_LONG_PRESS); run(50);
  turn(3);    // nothing happens: the screen stays off
  press();
  shot("locked", bg ? "Заключено: натискане показва как се отключва" : "Locked: a press shows how to unlock");
  user_btn.push(BUTTON_EVENT_LONG_PRESS); run(900);

  // quick menu, hibernate
  hold();
  shot("quick_menu", bg ? "Бързо меню: BT с PIN" : "Quick menu: BT with PIN");
  turn(-1); press();
  shot("hibernate_confirm", bg ? "Хибернация?" : "Hibernate?");
  encoder_btn.push(BUTTON_EVENT_CLICK);
  ui->loop();   // runs hibernate(): draws, waits, "powers off"
  display.turnOn();
  shotFrame("hibernate", bg ? "Изключване: екранът гасне веднага" : "Power off: the screen goes dark at once");
  ui->drawPowerOffCountdown(1500);
  shotFrame("poweroff_hold", bg ? "Back + енкодер задържани: пусни за отказ" : "Back + push held: let go to cancel");

  // low battery
  holdBack();
  uint8_t off = prefs.ui_off;
  prefs.ui_off = 3;   // 5 min: the screen stays on until the warning
  run(1000);   // the "unlocked" alert of holdBack() goes away
  board.mv = 3540;
  // the shown value is an average of readings 8 s apart: the warning comes after about a minute
  for (int t = 0; t < 1500 && !ui->alertShowing(); t++) run(100);
  run(100);
  shotFrame("low_battery", bg ? "Батерия под 10%: известие и дълга вибрация" : "Battery below 10%: alert and a long buzz");
  board.mv = 3920;
  run(3000);
  prefs.ui_off = off;

  // the other text sizes: menus, lists and messages
  for (int t = 1; t <= 3; t++) {
    prefs.ui_menu_sz = t;
    prefs.ui_text_sz = t;
    static const char* TAGS[] = { "", "medium", "small", "tiny" };
    static const char* TB[] = { "", "среден размер", "малък размер", "мини размер" };
    static const char* TE[] = { "", "medium size", "small size", "tiny size" };
    const char* tag = TAGS[t];
    const char* tb = TB[t];
    const char* te = TE[t];
    char name[32], cap[96];
    holdBack();
    press();
    snprintf(name, sizeof(name), "main_menu_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Главно меню" : "Main menu", bg ? tb : te); shot(name, cap);
    press();
    snprintf(name, sizeof(name), "conversations_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Разговори" : "Conversations", bg ? tb : te); shot(name, cap);
    turn(2); press();
    snprintf(name, sizeof(name), "thread_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Разговор" : "Thread", bg ? tb : te); shot(name, cap);
    turn(-9);
    snprintf(name, sizeof(name), "thread_top_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Разговор, по-нагоре" : "Thread, scrolled up", bg ? tb : te); shot(name, cap);
    hold();
    snprintf(name, sizeof(name), "msg_actions_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Действия" : "Actions", bg ? tb : te); shot(name, cap);
    holdBack();
    press(); turn(1); press();
    snprintf(name, sizeof(name), "contacts_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Контакти" : "Contacts", bg ? tb : te); shot(name, cap);
    turn(-1);
    snprintf(name, sizeof(name), "contacts_letters_%s", tag);
    snprintf(cap, sizeof(cap), "%s: %s", bg ? "Ред с букви" : "Letter row", bg ? tb : te); shot(name, cap);
  }
  // menus large, content small (the old "mixed" choice)
  prefs.ui_menu_sz = 0;
  prefs.ui_text_sz = 2;
  holdBack();
  press();
  shot("main_menu_mixed", bg ? "Меню голям, текст малък: главно меню" : "Menu large, text small: main menu");
  press();
  shot("conversations_mixed", bg ? "Меню голям, текст малък: разговори" : "Menu large, text small: conversations");
  prefs.ui_text_sz = 0;
  holdBack();

  // rescue screen
  ui->enterRescue();
  run(100);
  shot("rescue", bg ? "CLI rescue (задържан Back по време на началния екран)" : "CLI rescue (Back held during the splash)");
}

int main(int argc, char** argv) {
  if (argc > 1) out_dir = argv[1];
  crcInit();
  std::string m = out_dir + "/manifest.tsv";
  manifest = fopen(m.c_str(), "w");
  if (!manifest) { perror(m.c_str()); return 1; }
  scenario(0);
  scenario(1);
  fclose(manifest);
  printf("screens written to %s\n", out_dir.c_str());
  return 0;
}
