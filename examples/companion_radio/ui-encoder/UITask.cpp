#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"

#if !defined(UI_HAS_ROTARY_INPUT) || !defined(PIN_ENCODER_BTN) || !defined(PIN_USER_BTN)
  #error "ui-encoder needs UI_HAS_ROTARY_INPUT, PIN_ENCODER_BTN (Enter) and PIN_USER_BTN (Back)"
#endif

#ifndef ENCODER_STEPS_PER_DETENT
  #define ENCODER_STEPS_PER_DETENT 4
#endif
#ifndef ENCODER_REVERSE
  #define ENCODER_REVERSE false
#endif

#define BOOT_SCREEN_MILLIS   2500
#define RESCUE_WINDOW_MILLIS 8000     // hold Back this soon after boot -> CLI rescue
#define MARQUEE_PAUSE_MILLIS 1000
#define MARQUEE_STEP_MILLIS  300
#define NEW_MSG_HIGHLIGHT    3000
#define VALID_EPOCH          1704067200UL   // 2024-01-01, anything earlier means "no clock yet"

// haptic dictionary (ms): key press, confirmed action, channel msg, direct msg, low battery / power off
#ifndef UI_HAPTIC_CLICK_MS
  #define UI_HAPTIC_CLICK_MS  25
#endif
#ifndef UI_HAPTIC_TICK_MS
  #define UI_HAPTIC_TICK_MS   0     // per encoder detent, off: the encoder has mechanical clicks
#endif
#define UI_HAPTIC_ACK_MS      120
#define UI_HAPTIC_LONG_MS     600
static const uint16_t HAPTIC_CHANNEL[] = { 200 };
static const uint16_t HAPTIC_DIRECT[]  = { 150, 150, 150 };

#define COLOR_BLACK  0

#include "../ui-new/icons.h"

// 8x8 status icons, MSB first rows (Adafruit drawBitmap format)
static const uint8_t icon_mail[] = { 0xFF, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0xFF, 0x00 };
static const uint8_t icon_bt[]   = { 0x20, 0x30, 0xA8, 0x70, 0xA8, 0x30, 0x20, 0x00 };

static const unsigned long AUTO_OFF_TABLE[] = { 15000, 30000, 60000, 300000 };
#define DOTS_BOTTOM 0
#define DOTS_RIGHT  1
#define DOTS_NONE   2

// ---------------------------------------------------------------- UI language (0 = BG, 1 = EN)

#define LANG_COUNT 2
static const char* LANG_NAMES[LANG_COUNT] = { "BG", "EN" };
static NodePrefs* lang_prefs = NULL;

enum StrId {
  S_AGE_S,
  S_AGE_M,
  S_AGE_H,
  S_AGE_D,
  S_EMPTY,
  S_YES,
  S_NO,
  S_MSG,
  S_DEL,
  S_DEL_ALL,
  S_MARK_READ,
  S_MSGS,
  S_MSGS_N,
  S_NO_MSGS,
  S_DELETED,
  S_ALL_DELETED,
  S_READ,
  S_RECENT,
  S_RECENT_ADV,
  S_SETTINGS,
  S_SETTINGS_EDIT,
  S_LANG_FMT,
  S_DOTS_FMT,
  S_VIBE_FMT,
  S_TZ_FMT,
  S_TZ_EDIT_FMT,
  S_SCREEN_FMT,
  S_ON,
  S_OFF,
  S_DOTS_BOTTOM,
  S_DOTS_RIGHT,
  S_DOTS_NONE,
  S_OFF_15S,
  S_OFF_30S,
  S_OFF_1M,
  S_OFF_5M,
  S_MENU,
  S_QUICK,
  S_SEND_ADV,
  S_HIBERNATE,
  S_HIB_Q,
  S_ADV_SENT,
  S_ADV_FAIL,
  S_MORE_FMT,
  S_PHONE,
  S_HEARD_FMT,
  S_NOBODY,
  S_NOISE_FMT,
  S_NO_GPS,
  S_FIX,
  S_NOFIX,
  S_SAT_FMT,
  S_ALT_FMT,
  S_NEW_FMT,
  S_POWER_OFF,
  S_BATT_EMPTY,
  S_GPS_ON,
  S_GPS_OFF,
  S_ENC_FMT,
  S_ENC_NORMAL,
  S_ENC_REVERSED,
  S_STEPS_FMT,
  S_COUNT
};

static const char* const STRINGS[S_COUNT][2] = {
  { "%luс", "%lus" },
  { "%luм", "%lum" },
  { "%luч", "%luh" },
  { "%luд", "%lud" },
  { "Няма записи", "No entries" },
  { "Натисни = да", "Press = yes" },
  { "Back = не", "Back = no" },
  { "Съобщение", "Message" },
  { "Изтрий", "Delete" },
  { "Изтрий всички", "Delete all" },
  { "Маркирай прочетени", "Mark all read" },
  { "Съобщения", "Messages" },
  { "Съобщения (%d)", "Messages (%d)" },
  { "Няма съобщения", "No messages" },
  { "Изтрито", "Deleted" },
  { "Всички изтрити", "All deleted" },
  { "Прочетени", "Marked read" },
  { "Скорошни", "Recent" },
  { "Скорошни adverts", "Recent adverts" },
  { "Настройки", "Settings" },
  { "Настройки: промяна", "Settings: edit" },
  { "Език: %s", "Language: %s" },
  { "Точки: %s", "Dots: %s" },
  { "Вибрация: %s", "Vibration: %s" },
  { "Час: UTC%+d", "Time: UTC%+d" },
  { "Час: < UTC%+d >", "Time: < UTC%+d >" },
  { "Екран: %s", "Screen: %s" },
  { "вкл", "on" },
  { "изкл", "off" },
  { "отдолу", "bottom" },
  { "вдясно", "right" },
  { "няма", "hidden" },
  { "15с", "15s" },
  { "30с", "30s" },
  { "1мин", "1min" },
  { "5мин", "5min" },
  { "Меню", "Menu" },
  { "Бързо меню", "Quick menu" },
  { "Изпрати advert", "Send advert" },
  { "Хибернация", "Hibernate" },
  { "Хибернация?", "Hibernate?" },
  { "Advert изпратен", "Advert sent" },
  { "Advert неуспешен", "Advert failed" },
  { "+%d още", "+%d more" },
  { "Телефон: свързан", "Phone: connected" },
  { "Чут: %s %s", "Heard: %s %s" },
  { "Никой не е чут", "Nobody heard yet" },
  { "Шум: %d", "Noise: %d" },
  { "Няма GPS модул", "No GPS module" },
  { "fix", "fix" },
  { "без fix", "no fix" },
  { "%s %d сат", "%s %d sat" },
  { "%.0f м", "%.0f m" },
  { "Ново: %s", "New: %s" },
  { "Изключване...", "Powering off..." },
  { "Батерията е изтощена", "Battery empty" },
  { "GPS: вкл", "GPS: on" },
  { "GPS: изкл", "GPS: off" },
  { "Енкодер: %s", "Encoder: %s" },
  { "нормален", "normal" },
  { "обърнат", "reversed" },
  { "Щрак: %d стъпки", "Detent: %d steps" },
};

static const char* T(StrId id) {
  uint8_t lang = lang_prefs ? lang_prefs->ui_lang % LANG_COUNT : 0;
  return STRINGS[id][lang];
}

// ---------------------------------------------------------------- UTF-8 / text helpers

static int utf8Len(const char* s) {
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) if ((*p & 0xC0) != 0x80) n++;
  return n;
}

static const char* utf8Skip(const char* s, int chars) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p && chars > 0) {
    p++;
    while ((*p & 0xC0) == 0x80) p++;
    chars--;
  }
  return (const char*)p;
}

// copy up to max_chars characters starting at character 'from'
static void utf8Slice(char* dest, size_t dest_size, const char* src, int from, int max_chars) {
  const char* a = utf8Skip(src, from);
  const char* b = utf8Skip(a, max_chars);
  size_t n = b - a;
  if (n >= dest_size) n = dest_size - 1;
  memcpy(dest, a, n);
  dest[n] = 0;
}

static void formatAge(char* buf, size_t n, uint32_t secs) {
  if (secs < 60) snprintf(buf, n, T(S_AGE_S), (unsigned long)secs);
  else if (secs < 3600) snprintf(buf, n, T(S_AGE_M), (unsigned long)(secs / 60));
  else if (secs < 86400) snprintf(buf, n, T(S_AGE_H), (unsigned long)(secs / 3600));
  else snprintf(buf, n, T(S_AGE_D), (unsigned long)(secs / 86400));
}

// Draws text clipped to max_chars; if it is longer and 'scroll' is set, it scrolls (marquee)
// instead of being cut with "...". Returns true while scrolling, so the caller re-renders soon.
static bool drawMarquee(DisplayDriver& d, int x, int y, const char* text, int max_chars, unsigned long since, bool scroll) {
  char filtered[200];
  d.translateUTF8ToBlocks(filtered, text, sizeof(filtered));
  int len = utf8Len(filtered);
  int offset = 0;
  bool active = false;
  if (len > max_chars && scroll) {
    int extra = len - max_chars;
    unsigned long cycle = 2 * MARQUEE_PAUSE_MILLIS + (unsigned long)extra * MARQUEE_STEP_MILLIS;
    unsigned long t = (millis() - since) % cycle;
    if (t >= MARQUEE_PAUSE_MILLIS) {
      offset = (t - MARQUEE_PAUSE_MILLIS) / MARQUEE_STEP_MILLIS;
      if (offset > extra) offset = extra;
    }
    active = true;
  }
  char part[200];
  utf8Slice(part, sizeof(part), filtered, offset, max_chars);
  d.setCursor(x, y);
  d.print(part);
  return active;
}

static void drawBattery(DisplayDriver& d, int x, int y, uint16_t mv) {
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
  int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  d.drawRect(x, y, 12, 7);
  d.fillRect(x + 12, y + 2, 2, 3);
  d.fillRect(x + 2, y + 2, (pct * 8) / 100, 3);
}

// ---------------------------------------------------------------- screens

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];
public:
  SplashScreen(UITask* task) : _task(task) {
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');
    int len = dash ? dash - ver : strlen(ver);
    if (len >= (int)sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;
    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }
  int render(DisplayDriver& display) override {
    display.setColor(UIColor::corp_blue);
    display.drawXbm((display.width() - 128) / 2, 3, meshcore_logo, 128, 13);
    display.setColor(UIColor::primary_txt);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 24, _version_info);
    display.drawTextCentered(display.width() / 2, 42, FIRMWARE_BUILD_DATE);
    return 500;
  }
  void poll() override {
    if (millis() >= dismiss_after) _task->home();
  }
};

// Generic scrolling list: title row, up to 5 rows, inverted selection, scrollbar, marquee on the
// selected row. Short lists wrap around, long ones stop at the ends.
class ListScreen : public UIScreen {
protected:
  UITask* _task;
  int _sel = 0, _top = 0;
  unsigned long _sel_since = 0;
  static const int ROWS = 5, ROW_H = 10, TOP = 12;

  virtual void title(char* buf, size_t n) = 0;
  virtual int count() = 0;
  virtual void label(int i, char* buf, size_t n) = 0;
  virtual bool onEnter(int i) { return false; }
  virtual bool onContext(int i) { return false; }
  virtual const char* emptyText() { return T(S_EMPTY); }

  void select(int i) {
    int n = count();
    if (n <= 0) { _sel = _top = 0; return; }
    if (i < 0) i = (n <= 8) ? n - 1 : 0;
    if (i >= n) i = (n <= 8) ? 0 : n - 1;
    if (i != _sel) _sel_since = millis();
    _sel = i;
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + ROWS) _top = _sel - ROWS + 1;
  }

public:
  ListScreen(UITask* task) : _task(task) { }
  void reset() { _sel = 0; _top = 0; _sel_since = millis(); }

  int render(DisplayDriver& d) override {
    char buf[200];
    int n = count();
    if (_sel >= n) select(n - 1);
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    title(buf, sizeof(buf));
    bool scrolling = drawMarquee(d, 0, 0, buf, n > ROWS ? 15 : 21, _sel_since, true);
    if (n > ROWS) {
      char pos[12];
      snprintf(pos, sizeof(pos), "%d/%d", _sel + 1, n);
      d.drawTextRightAlign(d.width() - 1, 0, pos);
    }
    d.fillRect(0, 9, d.width(), 1);

    if (n == 0) {
      d.drawTextCentered(d.width() / 2, 32, emptyText());
      return scrolling ? MARQUEE_STEP_MILLIS : 1000;
    }
    int text_w = (n > ROWS) ? d.width() - 4 : d.width();
    int max_chars = (text_w - 2) / 6;
    for (int r = 0; r < ROWS && _top + r < n; r++) {
      int i = _top + r;
      int y = TOP + r * ROW_H;
      label(i, buf, sizeof(buf));
      if (i == _sel) {
        d.setColor(UIColor::primary_txt);
        d.fillRect(0, y - 1, text_w, ROW_H);
        d.setColor(COLOR_BLACK);
        if (drawMarquee(d, 1, y, buf, max_chars, _sel_since, true)) scrolling = true;
        d.setColor(UIColor::primary_txt);
      } else {
        drawMarquee(d, 1, y, buf, max_chars, 0, false);
      }
    }
    if (n > ROWS) {   // scrollbar
      int track = ROWS * ROW_H;
      int h = track * ROWS / n; if (h < 4) h = 4;
      int y = TOP - 1 + (track - h) * _top / (n - ROWS);
      d.fillRect(d.width() - 2, y, 2, h);
    }
    return scrolling ? MARQUEE_STEP_MILLIS : 1000;
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT) { select(_sel + 1); return true; }
    if (c == KEY_PREV) { select(_sel - 1); return true; }
    if (c == KEY_ENTER) return count() > 0 && onEnter(_sel);
    if (c == KEY_CONTEXT_MENU) return count() > 0 && onContext(_sel);
    return false;
  }
};

// Yes/no dialog: Enter = yes, Back = no (handled by the stack pop)
class ConfirmScreen : public UIScreen {
  UITask* _task;
  const char* _question;
  void (*_action)(UITask*);
public:
  ConfirmScreen(UITask* task) : _task(task), _question(""), _action(NULL) { }
  void setup(const char* question, void (*action)(UITask*)) { _question = question; _action = action; }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.drawRect(0, 0, d.width(), d.height());
    d.drawTextCentered(d.width() / 2, 14, _question);
    d.drawTextCentered(d.width() / 2, 34, T(S_YES));
    d.drawTextCentered(d.width() / 2, 46, T(S_NO));
    return 1000;
  }
  bool handleInput(char c) override {
    if (c == KEY_ENTER) {
      _task->pop();
      if (_action) _action(_task);
      return true;
    }
    return false;
  }
};

static ConfirmScreen* confirm_screen;

// ---- message reading

class MessageViewScreen : public UIScreen {
  UITask* _task;
  int _idx = 0;
  int _scroll = 0;
  static const int MAX_LINES = 12, LINE_CHARS = 21, VISIBLE = 5;
  const char* _lines[MAX_LINES];   // start of each line within the entry text
  uint8_t _line_len[MAX_LINES];    // characters per line
  int _num_lines = 0;
  unsigned long _opened = 0;

  void wrap(const char* text) {
    _num_lines = 0;
    const char* p = text;
    while (*p && _num_lines < MAX_LINES) {
      while (*p == ' ') p++;
      if (!*p) break;
      const char* start = p;
      const char* last_space = NULL;
      int chars = 0, chars_at_space = 0;
      const char* q = p;
      while (*q && chars < LINE_CHARS) {
        if (*q == ' ') { last_space = q; chars_at_space = chars; }
        q = utf8Skip(q, 1);
        chars++;
      }
      if (*q && *q != ' ' && last_space) {   // break at the last space
        _lines[_num_lines] = start;
        _line_len[_num_lines++] = chars_at_space;
        p = last_space + 1;
      } else {
        _lines[_num_lines] = start;
        _line_len[_num_lines++] = chars;
        p = q;
      }
    }
  }

public:
  MessageViewScreen(UITask* task) : _task(task) { }
  void open(int idx) {
    _idx = idx;
    _scroll = 0;
    _opened = millis();
    UIMsgEntry* m = _task->historyAt(idx);
    if (m) { m->unread = false; wrap(m->text); }
  }
  int render(DisplayDriver& d) override {
    UIMsgEntry* m = _task->historyAt(_idx);
    if (!m) { _task->pop(); return 100; }
    char age[12];
    formatAge(age, sizeof(age), rtc_clock.getCurrentTime() - m->timestamp);
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    int age_w = d.getTextWidth(age);
    bool scrolling = drawMarquee(d, 0, 0, m->from, (d.width() - age_w - 4) / 6, _opened, true);
    d.drawTextRightAlign(d.width() - 1, 0, age);
    d.fillRect(0, 9, d.width(), 1);
    d.setColor(UIColor::primary_txt);
    char line[100], filtered[100];
    for (int r = 0; r < VISIBLE && _scroll + r < _num_lines; r++) {
      utf8Slice(line, sizeof(line), _lines[_scroll + r], 0, _line_len[_scroll + r]);
      d.translateUTF8ToBlocks(filtered, line, sizeof(filtered));
      d.setCursor(0, 12 + r * 10);
      d.print(filtered);
    }
    if (_num_lines > VISIBLE) {
      int track = VISIBLE * 10;
      int h = track * VISIBLE / _num_lines;
      int y = 11 + (track - h) * _scroll / (_num_lines - VISIBLE);
      d.fillRect(d.width() - 2, y, 2, h);
    }
    return scrolling ? MARQUEE_STEP_MILLIS : 5000;
  }
  bool handleInput(char c) override;
};

class MessageActionsScreen : public ListScreen {
  int _idx = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_MSG)); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    snprintf(buf, n, "%s", T((StrId)(S_DEL + i)));
  }
  bool onEnter(int i) override;
public:
  MessageActionsScreen(UITask* task) : ListScreen(task) { }
  void open(int idx) { _idx = idx; reset(); }
};

class MessagesScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_MSGS)); }
  int count() override { return _task->historyCount(); }
  void label(int i, char* buf, size_t n) override {
    UIMsgEntry* m = _task->historyAt(i);
    if (m) snprintf(buf, n, "%s%s: %s", m->unread ? "*" : "", m->from, m->text);   // control chars are filtered out
    else buf[0] = 0;
  }
  const char* emptyText() override { return T(S_NO_MSGS); }
  bool onEnter(int i) override;
  bool onContext(int i) override;
public:
  MessagesScreen(UITask* task) : ListScreen(task) { }
};

static MessagesScreen* messages_screen;
static MessageViewScreen* message_view;
static MessageActionsScreen* message_actions;

bool MessagesScreen::onEnter(int i) {
  message_view->open(i);
  _task->push(message_view);
  return true;
}
bool MessagesScreen::onContext(int i) {
  message_actions->open(i);
  _task->push(message_actions);
  return true;
}

bool MessageViewScreen::handleInput(char c) {
  if (c == KEY_NEXT) { if (_scroll + VISIBLE < _num_lines) _scroll++; return true; }
  if (c == KEY_PREV) { if (_scroll > 0) _scroll--; return true; }
  if (c == KEY_CONTEXT_MENU || c == KEY_ENTER) {
    message_actions->open(_idx);
    _task->push(message_actions);
    return true;
  }
  return false;
}

bool MessageActionsScreen::onEnter(int i) {
  _task->pop();   // close this pop-up
  if (i == 0) {
    if (_task->current() == message_view) _task->pop();
    _task->deleteHistory(_idx);
    _task->showAlert(T(S_DELETED), 800);
  } else if (i == 1) {
    if (_task->current() == message_view) _task->pop();
    _task->clearHistory();
    _task->showAlert(T(S_ALL_DELETED), 800);
  } else {
    _task->markAllRead();
    _task->showAlert(T(S_READ), 800);
  }
  _task->haptic(UI_HAPTIC_ACK_MS);
  return true;
}

// ---- recently heard

class RecentScreen : public ListScreen {
  AdvertPath _recent[16];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_RECENT)); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override {
    char age[12];
    formatAge(age, sizeof(age), rtc_clock.getCurrentTime() - _recent[i].recv_timestamp);
    snprintf(buf, n, "%-4s %s", age, _recent[i].name);
  }
public:
  RecentScreen(UITask* task) : ListScreen(task) { }
  void open() {
    _n = the_mesh.getRecentlyHeard(_recent, 16);
    int k = 0;
    for (int i = 0; i < _n; i++) if (_recent[i].name[0]) _recent[k++] = _recent[i];
    _n = k;
    reset();
  }
};

static RecentScreen* recent_screen;

// ---- settings

static uint8_t encoderSteps(const NodePrefs* p) {
  return p->ui_enc_steps ? p->ui_enc_steps : ENCODER_STEPS_PER_DETENT;
}

class SettingsScreen : public ListScreen {
  bool _editing = false;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, _editing ? T(S_SETTINGS_EDIT) : T(S_SETTINGS)); }
  int count() override { return 7; }
  void label(int i, char* buf, size_t n) override {
    NodePrefs* p = _task->prefs();
    switch (i) {
      case 0: snprintf(buf, n, T(S_LANG_FMT), LANG_NAMES[p->ui_lang % LANG_COUNT]); break;
      case 1: snprintf(buf, n, T(S_DOTS_FMT), T((StrId)(S_DOTS_BOTTOM + p->ui_dots % 3))); break;
      case 2: snprintf(buf, n, T(S_VIBE_FMT), p->vibe_quiet ? T(S_OFF) : T(S_ON)); break;
      case 3: snprintf(buf, n, _editing ? T(S_TZ_EDIT_FMT) : T(S_TZ_FMT), p->ui_tz); break;
      case 4: snprintf(buf, n, T(S_SCREEN_FMT), T((StrId)(S_OFF_15S + p->ui_off % 4))); break;
      case 5: snprintf(buf, n, T(S_ENC_FMT), T(p->ui_enc_rev ? S_ENC_REVERSED : S_ENC_NORMAL)); break;
      default: snprintf(buf, n, T(S_STEPS_FMT), encoderSteps(p)); break;
    }
  }
  bool onEnter(int i) override {
    NodePrefs* p = _task->prefs();
    switch (i) {
      case 0: p->ui_lang = (p->ui_lang + 1) % LANG_COUNT; break;
      case 1: p->ui_dots = (p->ui_dots + 1) % 3; break;
      case 2: p->vibe_quiet = !p->vibe_quiet; break;
      case 3: _editing = !_editing; if (_editing) return true; break;   // save when leaving edit
      case 4: p->ui_off = (p->ui_off + 1) % 4; break;
      case 5: p->ui_enc_rev = !p->ui_enc_rev; _task->applyEncoderPrefs(); break;
      default: p->ui_enc_steps = encoderSteps(p) == 4 ? 2 : 4; _task->applyEncoderPrefs(); break;
    }
    the_mesh.savePrefs();
    _task->haptic(UI_HAPTIC_ACK_MS);
    return true;
  }
public:
  SettingsScreen(UITask* task) : ListScreen(task) { }
  bool handleInput(char c) override {
    if (_editing) {
      NodePrefs* p = _task->prefs();
      if (c == KEY_NEXT && p->ui_tz < 14) { p->ui_tz++; return true; }
      if (c == KEY_PREV && p->ui_tz > -12) { p->ui_tz--; return true; }
      if (c == KEY_CANCEL) { _editing = false; the_mesh.savePrefs(); return true; }
      if (c == KEY_NEXT || c == KEY_PREV) return true;
    }
    return ListScreen::handleInput(c);
  }
};

static SettingsScreen* settings_screen;

// ---- menus

class MainMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_MENU)); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    if (i == 0) {
      int u = _task->unreadCount();
      if (u > 0) snprintf(buf, n, T(S_MSGS_N), u);
      else snprintf(buf, n, T(S_MSGS));
    } else if (i == 1) {
      snprintf(buf, n, T(S_RECENT_ADV));
    } else {
      snprintf(buf, n, T(S_SETTINGS));
    }
  }
  bool onEnter(int i) override {
    if (i == 0) { messages_screen->reset(); _task->push(messages_screen); }
    else if (i == 1) { recent_screen->open(); _task->push(recent_screen); }
    else { settings_screen->reset(); _task->push(settings_screen); }
    return true;
  }
public:
  MainMenuScreen(UITask* task) : ListScreen(task) { }
};

static void doHibernate(UITask* task) { task->hibernate(); }

class QuickMenuScreen : public ListScreen {
  enum { ADVERT, BLUETOOTH,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
    HIBERNATE, COUNT };
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_QUICK)); }
  int count() override { return COUNT; }
  void label(int i, char* buf, size_t n) override {
    switch (i) {
      case ADVERT: snprintf(buf, n, T(S_SEND_ADV)); break;
      case BLUETOOTH: snprintf(buf, n, "Bluetooth: %s", _task->isBluetoothEnabled() ? T(S_ON) : T(S_OFF)); break;
#if ENV_INCLUDE_GPS == 1
      case GPS: snprintf(buf, n, "GPS: %s", _task->getGPSState() ? T(S_ON) : T(S_OFF)); break;
#endif
      default: snprintf(buf, n, T(S_HIBERNATE)); break;
    }
  }
  bool onEnter(int i) override {
    switch (i) {
      case ADVERT:
        if (the_mesh.advert()) {
          _task->notify(UIEventType::ack);
          _task->showAlert(T(S_ADV_SENT), 1000);
        } else {
          _task->showAlert(T(S_ADV_FAIL), 1000);
        }
        break;
      case BLUETOOTH:
        if (_task->isBluetoothEnabled()) _task->disableBluetooth();
        else _task->enableBluetooth();
        _task->notify(UIEventType::ack);
        break;
#if ENV_INCLUDE_GPS == 1
      case GPS:
        _task->toggleGPS();
        break;
#endif
      default:
        confirm_screen->setup(T(S_HIB_Q), doHibernate);
        _task->push(confirm_screen);
        break;
    }
    return true;
  }
public:
  QuickMenuScreen(UITask* task) : ListScreen(task) { }
};

static MainMenuScreen* main_menu;
static QuickMenuScreen* quick_menu;

// ---- standby: status bar + read-only status cards

class StandbyScreen : public UIScreen {
  UITask* _task;
  NodePrefs* _prefs;
  uint8_t _card = 0;
  unsigned long _since = 0;
  AdvertPath _recent[4];
  enum { SUMMARY, RECENT, RADIO,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
    CARDS };

  bool renderHeader(DisplayDriver& d, int w) {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    int x = w - 15;
    drawBattery(d, x, 1, _task->getBattMilliVolts());
    if (_task->isBluetoothEnabled()) { x -= 10; d.drawXbm(x, 1, icon_bt, 8, 8); }
    if (_task->unreadCount() > 0) { x -= 10; d.drawXbm(x, 1, icon_mail, 8, 8); }
    x -= 7;
    d.fillRect(x, 6, 1, 1); d.fillRect(x + 2, 6, 1, 1); d.fillRect(x + 4, 6, 1, 1);   // "..." = hold for quick menu
    bool scrolling = drawMarquee(d, 0, 1, _prefs->node_name, (x - 3) / 6, _since, true);
    d.fillRect(0, 10, w, 1);
    return scrolling;
  }

  void renderDots(DisplayDriver& d) {
    if (_prefs->ui_dots == DOTS_NONE) return;
    for (int i = 0; i < CARDS; i++) {
      int x, y;
      if (_prefs->ui_dots == DOTS_RIGHT) { x = d.width() - 3; y = 12 + (52 - CARDS * 8) / 2 + i * 8; }
      else { x = d.width() / 2 - CARDS * 4 + i * 8; y = 59; }
      if (i == _card) d.fillRect(x - 1, y - 1, 3, 3);
      else d.fillRect(x, y, 1, 1);
    }
  }

  bool renderSummary(DisplayDriver& d, int w) {
    bool scrolling = false;
    char buf[200];
    int unread = _task->unreadCount();
    int max_chars = w / 6;

    // row A, size 2: unread count and clock
    uint32_t now = rtc_clock.getCurrentTime();
    if (now >= VALID_EPOCH) {
      uint32_t local = now + (int32_t)_prefs->ui_tz * 3600;
      snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)((local / 3600) % 24), (unsigned long)((local / 60) % 60));
    } else {
      snprintf(buf, sizeof(buf), "--:--");
    }
    d.setTextSize(2);
    d.drawTextRightAlign(w - 1, 15, buf);
    if (unread > 0) {
      d.drawXbm(0, 19, icon_mail, 8, 8);
      snprintf(buf, sizeof(buf), unread > 99 ? "99+" : "%d", unread);
      d.setCursor(11, 15);
      d.print(buf);
    }
    d.setTextSize(1);

    if (unread > 0) {
      // last unread message; inverted for a few seconds after it arrives
      UIMsgEntry* m = NULL;
      for (int i = 0; i < _task->historyCount(); i++) {
        UIMsgEntry* e = _task->historyAt(i);
        if (e->unread) { m = e; break; }
      }
      if (m) {
        snprintf(buf, sizeof(buf), "%s: %s", m->from, m->text);
        bool fresh = millis() - _task->lastNewMsgAt() < NEW_MSG_HIGHLIGHT;
        if (fresh) {
          d.fillRect(0, 35, w, 10);
          d.setColor(COLOR_BLACK);
        }
        scrolling = drawMarquee(d, 1, 36, buf, max_chars, _task->lastNewMsgAt(), true);
        d.setColor(UIColor::primary_txt);
        if (fresh) scrolling = true;
      }
      if (unread > 1) {
        snprintf(buf, sizeof(buf), T(S_MORE_FMT), unread - 1);
        d.setCursor(1, 47);
        d.print(buf);
      }
    } else {
      // empty state: phone connection and last heard node
      if (_task->hasConnection()) snprintf(buf, sizeof(buf), T(S_PHONE));
      else if (!_task->isBluetoothEnabled()) snprintf(buf, sizeof(buf), "Bluetooth: %s", T(S_OFF));
      else if (the_mesh.getBLEPin() != 0) snprintf(buf, sizeof(buf), "BT PIN: %lu", (unsigned long)the_mesh.getBLEPin());
      else snprintf(buf, sizeof(buf), "Bluetooth: %s", T(S_ON));
      drawMarquee(d, 1, 36, buf, max_chars, 0, false);
      int n = the_mesh.getRecentlyHeard(_recent, 1);
      if (n > 0 && _recent[0].name[0]) {
        char age[12];
        formatAge(age, sizeof(age), rtc_clock.getCurrentTime() - _recent[0].recv_timestamp);
        snprintf(buf, sizeof(buf), T(S_HEARD_FMT), _recent[0].name, age);
        if (drawMarquee(d, 1, 47, buf, max_chars, _since, true)) scrolling = true;
      }
    }
    return scrolling;
  }

  void renderRecent(DisplayDriver& d, int w) {
    int n = the_mesh.getRecentlyHeard(_recent, 4);
    char buf[64], age[12];
    int y = 14;
    for (int i = 0; i < n; i++) {
      if (_recent[i].name[0] == 0) continue;
      formatAge(age, sizeof(age), rtc_clock.getCurrentTime() - _recent[i].recv_timestamp);
      int age_w = d.getTextWidth(age);
      drawMarquee(d, 0, y, _recent[i].name, (w - age_w - 4) / 6, 0, false);
      d.drawTextRightAlign(w - 1, y, age);
      y += 11;
    }
    if (y == 14) d.drawTextCentered(w / 2, 30, T(S_NOBODY));
  }

  void renderRadio(DisplayDriver& d) {
    char buf[40];
    d.setCursor(0, 14); snprintf(buf, sizeof(buf), "FQ %.3f SF%d", _prefs->freq, _prefs->sf); d.print(buf);
    d.setCursor(0, 25); snprintf(buf, sizeof(buf), "BW %.1f CR%d", _prefs->bw, _prefs->cr); d.print(buf);
    d.setCursor(0, 36); snprintf(buf, sizeof(buf), "TX %d dBm", _prefs->tx_power_dbm); d.print(buf);
    d.setCursor(0, 47); snprintf(buf, sizeof(buf), T(S_NOISE_FMT), radio_driver.getNoiseFloor()); d.print(buf);
  }

#if ENV_INCLUDE_GPS == 1
  void renderGPS(DisplayDriver& d, int w) {
    char buf[40];
    LocationProvider* nmea = sensors.getLocationProvider();
    d.setCursor(0, 14);
    d.print(_task->getGPSState() ? T(S_GPS_ON) : T(S_GPS_OFF));
    if (nmea == NULL) {
      d.setCursor(0, 25);
      d.print(T(S_NO_GPS));
      return;
    }
    snprintf(buf, sizeof(buf), T(S_SAT_FMT), nmea->isValid() ? T(S_FIX) : T(S_NOFIX), (int)nmea->satellitesCount());
    d.drawTextRightAlign(w - 1, 14, buf);
    d.setCursor(0, 25); snprintf(buf, sizeof(buf), "%.5f", nmea->getLatitude() / 1000000.); d.print(buf);
    d.setCursor(0, 36); snprintf(buf, sizeof(buf), "%.5f", nmea->getLongitude() / 1000000.); d.print(buf);
    d.setCursor(0, 47); snprintf(buf, sizeof(buf), T(S_ALT_FMT), nmea->getAltitude() / 1000.); d.print(buf);
  }
#endif

public:
  StandbyScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) { _since = millis(); }

  void showSummary() { _card = SUMMARY; _since = millis(); }

  int render(DisplayDriver& d) override {
    int w = (_prefs->ui_dots == DOTS_RIGHT) ? d.width() - 6 : d.width();
    bool scrolling = renderHeader(d, w);
    d.setColor(UIColor::primary_txt);
    switch (_card) {
      case SUMMARY: if (renderSummary(d, w)) scrolling = true; break;
      case RECENT: renderRecent(d, w); break;
      case RADIO: renderRadio(d); break;
#if ENV_INCLUDE_GPS == 1
      case GPS: renderGPS(d, w); break;
#endif
    }
    renderDots(d);
    return scrolling ? MARQUEE_STEP_MILLIS : 1000;
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT) { _card = (_card + 1) % CARDS; _since = millis(); return true; }
    if (c == KEY_PREV) { _card = (_card + CARDS - 1) % CARDS; _since = millis(); return true; }
    if (c == KEY_ENTER) { main_menu->reset(); _task->push(main_menu); return true; }
    if (c == KEY_CONTEXT_MENU) { quick_menu->reset(); _task->push(quick_menu); return true; }
    return false;   // Back: UITask turns the screen off
  }
};

static StandbyScreen* standby;

// ---------------------------------------------------------------- UITask

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  lang_prefs = node_prefs;
  _auto_off = millis() + autoOffMillis();

  user_btn.begin();
  user_btn.setDebounce(15);
  encoder_btn.begin();
  encoder_btn.setDebounce(15);
  rotary_input.begin();
  applyEncoderPrefs();

  if (_display != NULL) _display->turnOn();

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif
#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  _splash = new SplashScreen(this);
  standby = new StandbyScreen(this, node_prefs);
  main_menu = new MainMenuScreen(this);
  quick_menu = new QuickMenuScreen(this);
  messages_screen = new MessagesScreen(this);
  message_view = new MessageViewScreen(this);
  message_actions = new MessageActionsScreen(this);
  recent_screen = new RecentScreen(this);
  settings_screen = new SettingsScreen(this);
  confirm_screen = new ConfirmScreen(this);
  _depth = 0;   // splash until home()
  _next_refresh = 100;
}

void UITask::applyEncoderPrefs() {
  rotary_input.setReverse((bool)ENCODER_REVERSE != (_node_prefs->ui_enc_rev != 0));
  rotary_input.setStepsPerDetent(encoderSteps(_node_prefs));
}

unsigned long UITask::autoOffMillis() const {
  return AUTO_OFF_TABLE[_node_prefs ? _node_prefs->ui_off % 4 : 0];
}

void UITask::push(UIScreen* s) {
  if (_depth < (int)(sizeof(_stack) / sizeof(_stack[0]))) _stack[_depth++] = s;
  _next_refresh = 0;
}

void UITask::pop() {
  if (_depth > 1) _depth--;
  _next_refresh = 0;
}

void UITask::home() {
  _stack[0] = standby;
  _depth = 1;
  standby->showSummary();
  _next_refresh = 0;
}

void UITask::showAlert(const char* text, int duration_millis) {
  StrHelper::strncpy(_alert, text, sizeof(_alert));
  _alert_expiry = millis() + duration_millis;
  _next_refresh = 0;
}

void UITask::haptic(uint16_t millis_on) {
#ifdef PIN_VIBRATION
  if (!_node_prefs->vibe_quiet) vibration.pulse(millis_on);
#endif
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  switch (t) {
    case UIEventType::contactMessage: buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7"); break;
    case UIEventType::channelMessage: buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#"); break;
    case UIEventType::ack: buzzer.play("ack:d=32,o=8,b=120:c"); break;
    default: break;
  }
#endif
#ifdef PIN_VIBRATION
  if (_node_prefs->vibe_quiet) return;
  switch (t) {
    case UIEventType::contactMessage:
    case UIEventType::roomMessage:
      vibration.playPattern(HAPTIC_DIRECT, sizeof(HAPTIC_DIRECT) / sizeof(HAPTIC_DIRECT[0]));
      break;
    case UIEventType::channelMessage:
      vibration.playPattern(HAPTIC_CHANNEL, 1);
      break;
    case UIEventType::newContactMessage:
    case UIEventType::ack:
      vibration.pulse(UI_HAPTIC_ACK_MS);
      break;
    default:
      break;
  }
#endif
}

int UITask::unreadCount() const {
  int n = 0;
  for (int i = 0; i < _msg_count; i++) {
    int k = (_msg_head - i + UI_MSG_HISTORY) % UI_MSG_HISTORY;
    if (_msgs[k].unread) n++;
  }
  return n;
}

UIMsgEntry* UITask::historyAt(int i) {
  if (i < 0 || i >= _msg_count) return NULL;
  return &_msgs[(_msg_head - i + UI_MSG_HISTORY) % UI_MSG_HISTORY];
}

void UITask::deleteHistory(int i) {
  if (i < 0 || i >= _msg_count) return;
  // shift older entries one step towards the newer end
  for (int j = i; j < _msg_count - 1; j++) *historyAt(j) = *historyAt(j + 1);
  _msg_count--;
}

void UITask::clearHistory() {
  _msg_count = 0;
}

void UITask::markAllRead() {
  for (int i = 0; i < _msg_count; i++) historyAt(i)->unread = false;
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) markAllRead();   // the phone has fetched everything
  _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  _msg_head = (_msg_head + 1) % UI_MSG_HISTORY;
  if (_msg_count < UI_MSG_HISTORY) _msg_count++;
  UIMsgEntry* m = &_msgs[_msg_head];
  m->timestamp = rtc_clock.getCurrentTime();
  m->path_len = path_len;
  m->unread = true;
  StrHelper::strncpy(m->from, from_name, sizeof(m->from));
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  _last_new_msg = millis();

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
      home();
    } else if (_display->isOn() && current() != standby) {
      char alert[48];
      snprintf(alert, sizeof(alert), T(S_NEW_FMT), from_name);
      showAlert(alert, 1500);
    }
    if (_display->isOn()) _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) return !strcmp(_sensors->getSettingValue(i), "1");
    }
  }
  return false;
}

void UITask::toggleGPS() {
  if (_sensors == NULL) return;
  int num = _sensors->getNumSettings();
  for (int i = 0; i < num; i++) {
    if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
      bool on = strcmp(_sensors->getSettingValue(i), "1") != 0;
      _sensors->setSettingValue("gps", on ? "1" : "0");
      _node_prefs->gps_enabled = on;
      the_mesh.savePrefs();
      notify(UIEventType::ack);
      showAlert(on ? T(S_GPS_ON) : T(S_GPS_OFF), 800);
      break;
    }
  }
}

void UITask::hibernate() {
  if (_display != NULL) {
    _display->startFrame();
    _display->setTextSize(1);
    _display->setColor(UIColor::warning_txt);
    _display->drawTextCentered(_display->width() / 2, 28, T(S_POWER_OFF));
    _display->endFrame();
  }
#ifdef PIN_VIBRATION
  if (!_node_prefs->vibe_quiet) {
    vibration.pulse(UI_HAPTIC_LONG_MS);
    unsigned long t = millis();
    while (millis() - t < UI_HAPTIC_LONG_MS + 50) vibration.loop();
  }
#endif
  if (_display != NULL) _display->turnOff();
  shutdown();
}

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
  buzzer.shutdown();
  uint32_t buzzer_timer = millis();
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer) buzzer.loop();
#endif
#ifdef PIN_VIBRATION
  vibration.stop();
#endif
  if (restart) _board->reboot();
  else _board->powerOff();
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // wake only, consume the key
      home();
      c = 0;
    }
    _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }
  return c;
}

void UITask::renderAlert() {
  _display->setTextSize(1);
  int y = _display->height() / 3;
  int p = _display->height() / 32;
  _display->setColor(UIColor::popup_bkg);
  _display->fillRect(p, y, _display->width() - p * 2, y);
  _display->setColor(UIColor::popup_txt);
  _display->drawRect(p, y, _display->width() - p * 2, y);
  _display->drawTextCentered(_display->width() / 2, y + p * 3, _alert);
}

void UITask::loop() {
  char c = 0;
  bool from_rotary = false;

  // encoder push: click = Enter, hold = context / quick menu
  int ev = encoder_btn.check();
  if (ev == BUTTON_EVENT_CLICK) c = checkDisplayOn(KEY_ENTER);
  else if (ev == BUTTON_EVENT_LONG_PRESS) c = checkDisplayOn(KEY_CONTEXT_MENU);

  // Back: click = up one level, hold = standby (CLI rescue right after boot)
  if (c == 0) {
    ev = user_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_CANCEL);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (millis() - ui_started_at < RESCUE_WINDOW_MILLIS) {
        the_mesh.enterCLIRescue();
      } else {
        c = checkDisplayOn(KEY_HOME);
      }
    }
  }

  // rotation: steps stay queued in the ISR while a button produced a key
  if (c == 0) {
    RotaryInputEvent r = rotary_input.poll();
    if (r == RotaryInputEvent::Next) c = checkDisplayOn(KEY_NEXT);
    else if (r == RotaryInputEvent::Prev) c = checkDisplayOn(KEY_PREV);
    from_rotary = c != 0;
  }

  if (c != 0) {
    haptic(from_rotary ? UI_HAPTIC_TICK_MS : UI_HAPTIC_CLICK_MS);
    if (c == KEY_HOME) {
      home();
    } else {
      UIScreen* s = current();
      bool handled = s && s->handleInput(c);
      if (!handled && c == KEY_CANCEL) {
        if (_depth > 1) pop();
        else if (_display != NULL) _display->turnOff();   // Back on standby = screen off
      }
    }
    if (_display != NULL && _display->isOn()) _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) buzzer.loop();
#endif

  UIScreen* s = current();
  if (s) s->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && current()) {
      _display->startFrame();
      int delay_millis = current()->render(*_display);
      if (millis() < _alert_expiry) {
        renderAlert();
        unsigned long alert_left = _alert_expiry - millis();
        if ((unsigned long)delay_millis > alert_left) delay_millis = alert_left;
      }
      _next_refresh = millis() + delay_millis;
      _display->endFrame();
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS && !board.isExternalPowered()) {
      if (_display != NULL) {
        _display->turnOn();
        _display->startFrame();
        _display->setTextSize(1);
        _display->setColor(UIColor::warning_txt);
        _display->drawTextCentered(_display->width() / 2, 20, T(S_BATT_EMPTY));
        _display->drawTextCentered(_display->width() / 2, 36, T(S_POWER_OFF));
        _display->endFrame();
        delay(2000);
      }
      hibernate();
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}
