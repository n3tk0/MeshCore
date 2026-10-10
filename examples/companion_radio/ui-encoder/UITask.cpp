#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "WordPredictor.h"
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
  S_REPLY,
  S_NEW_MSG,
  S_NO_RECIPIENTS,
  S_SEND,
  S_CLEAR,
  S_DISCARD,
  S_SEND_Q,
  S_SENT,
  S_SEND_FAIL,
  S_NO_TARGET,
  S_QUICK_MSG,
  S_WRITE,
  S_QUICK_MSGS,
  S_QUICK_N_FMT,
  S_SAVE,
  S_SAVED,
  S_ADD_TEXT,
  S_CSORT_FMT,
  S_CSORT_NAME,
  S_CSORT_RECENT,
  S_EMPTY_SLOT,
  S_NO_QUICK,
  S_SAVE_FAIL,
  S_ANT_Q,
  S_TX_FMT,
  S_TX_IS_OFF,
  S_BATT_OUT1,
  S_BATT_OUT2,
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
  { "Отговори", "Reply" },
  { "Ново съобщение", "New message" },
  { "Няма получатели", "No recipients" },
  { "Изпрати", "Send" },
  { "Изчисти", "Clear" },
  { "Откажи", "Discard" },
  { "Изпрати?", "Send?" },
  { "Изпратено", "Sent" },
  { "Неуспешно", "Send failed" },
  { "Няма получател", "Unknown recipient" },
  { "Готово съобщение", "Quick reply" },
  { "Напиши", "Write" },
  { "Готови съобщения", "Quick replies" },
  { "Готово %d", "Quick reply %d" },
  { "Запази", "Save" },
  { "Запазено", "Saved" },
  { "Допиши", "Edit first" },
  { "Контакти: %s", "Contacts: %s" },
  { "по име", "by name" },
  { "последни", "recent" },
  { "(празно)", "(empty)" },
  { "Няма готови", "No quick replies" },
  { "Неуспешен запис", "Save failed" },
  { "Има ли антена?", "Antenna fitted?" },
  { "Предаване: %s", "Transmit: %s" },
  { "Предаването е изкл.", "Transmit is off" },
  { "Батерията може", "Battery can" },
  { "да се извади", "be removed" },
};

// built-in quick replies, used until the user edits one in Settings
static const char* const QUICK_DEFAULTS[UI_QUICK_COUNT][2] = {
  { "Добре", "OK" },
  { "Идвам", "On my way" },
  { "Къде си?", "Where are you?" },
  { "Обади се", "Call me" },
  { "Всичко е наред", "All good" },
  { "Чакам те", "Waiting for you" },
  { "Да", "Yes" },
  { "Не", "No" },
  { "Благодаря", "Thanks" },
  { "Ще закъснея", "Running late" },
};

static const char* T(StrId id) {
  uint8_t lang = lang_prefs ? lang_prefs->ui_lang % LANG_COUNT : 0;
  return STRINGS[id][lang];
}

// failure alert, or why nothing could be sent
static const char* failText(StrId id) { return the_mesh.isTxAllowed() ? T(id) : T(S_TX_IS_OFF); }

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
  virtual bool renderHeader(DisplayDriver& d) { return false; }   // true = drew its own title row
  virtual bool listFocused() { return true; }                     // false = no selection bar

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
    bool scrolling = false;
    if (!renderHeader(d)) {
      title(buf, sizeof(buf));
      scrolling = drawMarquee(d, 0, 0, buf, n > ROWS ? 15 : 21, _sel_since, true);
      if (n > ROWS) {
        char pos[12];
        snprintf(pos, sizeof(pos), "%d/%d", _sel + 1, n);
        d.drawTextRightAlign(d.width() - 1, 0, pos);
      }
      d.fillRect(0, 9, d.width(), 1);
    }
    d.setColor(UIColor::primary_txt);

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
      if (i == _sel && listFocused()) {
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

// Asked before the first transmit (UI_ASK_ANTENNA): sending into a missing antenna can damage the
// SX1262 PA, and the chip cannot detect it. Enter = yes (transmit on), Back = no (receive only).
class AntennaScreen : public UIScreen {
  UITask* _task;
public:
  AntennaScreen(UITask* task) : _task(task) { }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::warning_txt);
    d.drawRect(0, 0, d.width(), d.height());
    d.drawTextCentered(d.width() / 2, 14, T(S_ANT_Q));
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(d.width() / 2, 34, T(S_YES));
    d.drawTextCentered(d.width() / 2, 46, T(S_NO));
    return 1000;
  }
  bool handleInput(char c) override {
    if (c != KEY_ENTER && c != KEY_CANCEL) return false;
    _task->answerAntenna(c == KEY_ENTER);
    return true;
  }
};

static AntennaScreen* antenna_screen;

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
  int count() override { return 4; }
  void label(int i, char* buf, size_t n) override {
    snprintf(buf, n, "%s", T(i == 0 ? S_REPLY : (StrId)(S_DEL + i - 1)));
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

static void replyTo(UITask* task, const UIMsgEntry* m);

bool MessageActionsScreen::onEnter(int i) {
  _task->pop();   // close this pop-up
  if (i == 0) {
    UIMsgEntry* m = _task->historyAt(_idx);
    if (m) {
      m->unread = false;
      if (_task->current() == message_view) _task->pop();
      replyTo(_task, m);
    }
    return true;
  }
  i--;
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

// ---- writing: on-screen keyboard with word prediction

#ifndef KB_DOUBLE_CLICK_MS
  #define KB_DOUBLE_CLICK_MS 350   // two presses this close = accept the suggested word
#endif

struct ComposeTarget {
  bool is_channel;
  uint8_t channel_idx;
  uint8_t pub_key[PUB_KEY_SIZE];
  char name[32];
};

// who to answer for a message in the history: its recorded source, or (older entries) a channel
// or contact with the sender's name
static bool findTarget(const UIMsgEntry* m, ComposeTarget& t) {
  memset(&t, 0, sizeof(t));
  const char* name = m->from;
  if (m->src == MSG_SRC_CONTACT) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(m->pub_prefix, sizeof(m->pub_prefix));
    if (!c) return false;
    memcpy(t.pub_key, c->id.pub_key, PUB_KEY_SIZE);
    StrHelper::strncpy(t.name, c->name, sizeof(t.name));
    return true;
  }
#ifdef MAX_GROUP_CHANNELS
  if (m->src == MSG_SRC_CHANNEL) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(m->channel_idx, ch) || !ch.name[0]) return false;
    t.is_channel = true;
    t.channel_idx = m->channel_idx;
    StrHelper::strncpy(t.name, ch.name, sizeof(t.name));
    return true;
  }
#endif
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, ch) && ch.name[0] && strcmp(ch.name, name) == 0) {
      t.is_channel = true;
      t.channel_idx = i;
      StrHelper::strncpy(t.name, ch.name, sizeof(t.name));
      return true;
    }
  }
#endif
  ContactInfo c;
  // getContactByIdx() counts the reserved anonymous slots, getNumContacts() does not
  for (int i = MAX_ANON_CONTACTS; i < the_mesh.getNumContacts() + MAX_ANON_CONTACTS; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_NONE && strcmp(c.name, name) == 0) {
      memcpy(t.pub_key, c.id.pub_key, PUB_KEY_SIZE);
      StrHelper::strncpy(t.name, c.name, sizeof(t.name));
      return true;
    }
  }
  return false;
}

static void closeWriting(UITask* task);
static void openReplyChoice(UITask* task, const ComposeTarget& t);

static bool sendText(UITask* task, const ComposeTarget& t, const char* text, int len) {
  if (len <= 0 || !the_mesh.isTxAllowed()) return false;
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  if (t.is_channel) {
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails ch;
    if (the_mesh.getChannel(t.channel_idx, ch) && ch.name[0]) {
      return the_mesh.sendGroupMessage(ts, ch.channel, task->prefs()->node_name, text, len);
    }
#endif
    return false;
  }
  ContactInfo* c = the_mesh.lookupContactByPubKey(t.pub_key, PUB_KEY_SIZE);
  uint32_t ack, timeout;
  return c && the_mesh.sendMessage(*c, ts, 0, text, ack, timeout) != MSG_SEND_FAILED;
}

static void utf8Upper(char* dest, const char* ch, int bytes) {
  memcpy(dest, ch, bytes);
  dest[bytes] = 0;
  if (bytes == 1 && dest[0] >= 'a' && dest[0] <= 'z') dest[0] -= 32;
  if (bytes == 2) {
    uint16_t cp = ((uint16_t)(dest[0] & 0x1F) << 6) | (dest[1] & 0x3F);
    if (cp >= 0x0430 && cp <= 0x044F) {
      cp -= 0x20;
      dest[0] = 0xC0 | (cp >> 6);
      dest[1] = 0x80 | (cp & 0x3F);
    }
  }
}

static const uint8_t icon_shift[]   = { 0x10, 0x38, 0x7C, 0xFE, 0x38, 0x38, 0x38, 0x00 };
static const uint8_t icon_shift_o[] = { 0x10, 0x28, 0x44, 0xEE, 0x28, 0x28, 0x38, 0x00 };

// Jog-dial keyboard: turn = move over the keys, press = type, two quick presses = take the
// suggested word (shown inverted after the cursor) plus a space, Back = delete, hold = menu.
class ComposeScreen : public UIScreen {
  enum Set { SET_BG, SET_EN, SET_SYM };
  enum Special { K_SHIFT = 1, K_SPACE, K_SYM, K_LANG, K_SEND };
  static const int COLS = 16, CELL = 8, KB_TOP = 40, LINE_CHARS = 21, TEXT_LINES = 3;

  UITask* _task;
  WordPredictor _predict;
  ComposeTarget _target;
  char _text[MAX_TEXT_LEN + 1];
  int _len = 0, _max_len = MAX_TEXT_LEN;
  uint8_t _set = SET_BG, _letters = SET_BG;   // _letters: the set the 123 key returns to
  int _sel = 0;
  bool _shift = false;
  char _rest[48];                             // suggested completion of the current word
  bool _has_rest = false;
  int _undo_len = 0;                          // bytes the last press typed, 0 = not undoable
  bool _undo_shift = false;
  unsigned long _last_press = 0;
  bool _edit = false;                         // editing quick reply _slot instead of a message
  int _slot = 0;
  bool _draft = false;                        // _text is an unsent message for _target
  char _stash[MAX_TEXT_LEN + 1];              // the unsent message while the keyboard is used for another one
  ComposeTarget _stash_target;

  const char* chars() const {
    static const char* const SETS[3] = {
      "абвгдежзийклмнопрстуфхцчшщъьюя.,?!",
      "abcdefghijklmnopqrstuvwxyz.,?!'-",
      "1234567890@#&()/.,?!'\"-+=*:;%_<>",
    };
    return SETS[_set];
  }
  int numChars() const { return utf8Len(chars()); }

  // specials after the characters: shift, space, 123/abc, language, send
  int specialAt(int k) const {
    static const uint8_t LETTERS[] = { K_SHIFT, K_SPACE, K_SYM, K_LANG, K_SEND };
    static const uint8_t SYMBOLS[] = { K_SPACE, K_SYM, K_SEND };
    if (_set == SET_SYM) return k < 3 ? SYMBOLS[k] : 0;
    return k < 5 ? LETTERS[k] : 0;
  }
  int numKeys() const { return numChars() + (_set == SET_SYM ? 3 : 5); }
  static int keyWidth(int special) {
    switch (special) {
      case K_SPACE: return 4;
      case K_SYM: return 3;
      case K_LANG: case K_SEND: return 2;
      default: return 1;
    }
  }
  int findSpecial(int special) const {
    for (int i = numChars(); i < numKeys(); i++) if (specialAt(i - numChars()) == special) return i;
    return 0;
  }

  // start of the word before the cursor
  int wordStart() const {
    int i = _len;
    while (i > 0) {
      unsigned char c = _text[i - 1];
      if (c < 0x80 && !isalpha(c)) break;   // space, digit or punctuation ends the word
      i--;
    }
    return i;
  }

  void updateSuggestion() {
    int ws = wordStart();
    _has_rest = ws < _len && _predict.suggest(&_text[ws], _rest, sizeof(_rest));
  }

  void autoShift() {   // capital letter at the start and after . ! ?
    if (_len == 0) { _shift = true; return; }
    if (_text[_len - 1] == ' ') {
      int i = _len - 1;
      while (i > 0 && _text[i - 1] == ' ') i--;
      _shift = i > 0 && strchr(".!?", _text[i - 1]) != NULL;
    }
  }

  bool append(const char* s) {
    int n = strlen(s);
    if (_len + n > _max_len) {
      _task->haptic(UI_HAPTIC_LONG_MS / 3);   // full
      return false;
    }
    memcpy(&_text[_len], s, n + 1);
    _len += n;
    return true;
  }

  void learnLastWord() {
    int ws = wordStart();
    if (ws < _len) _predict.learn(&_text[ws]);
  }

  void backspace() {
    if (_len == 0) return;
    do { _len--; } while (_len > 0 && ((unsigned char)_text[_len] & 0xC0) == 0x80);
    _text[_len] = 0;
    autoShift();
  }

  // the second of two quick presses: take back what the first typed and accept its suggestion
  bool acceptSuggestion() {
    int saved = _len;
    _len -= _undo_len;
    _text[_len] = 0;
    updateSuggestion();
    if (!_has_rest || _len + (int)strlen(_rest) + 1 > _max_len) {
      _len = saved;          // nothing to accept: keep both presses as typed letters
      _text[_len] = 0;
      return false;
    }
    _shift = _undo_shift;
    append(_rest);
    learnLastWord();
    append(" ");
    autoShift();
    _task->haptic(UI_HAPTIC_ACK_MS);
    return true;
  }

  void press() {
    int nc = numChars();
    _undo_len = 0;
    if (_sel < nc) {
      const char* p = utf8Skip(chars(), _sel);
      int bytes = utf8Skip(p, 1) - p;
      char ch[4];
      if (_shift) utf8Upper(ch, p, bytes);
      else { memcpy(ch, p, bytes); ch[bytes] = 0; }
      _undo_shift = _shift;
      if (append(ch)) {
        if (bytes == 2 || isalpha((unsigned char)ch[0])) _undo_len = bytes;   // only a letter can be taken back
        if (_set != SET_SYM) _shift = false;
        if (bytes == 1 && strchr(".!?", ch[0])) _shift = false;   // takes effect after the space
      }
      return;
    }
    switch (specialAt(_sel - nc)) {
      case K_SHIFT: _shift = !_shift; break;
      case K_SPACE:
        learnLastWord();
        if (append(" ")) autoShift();
        break;
      case K_SYM:
        _set = (_set == SET_SYM) ? _letters : SET_SYM;
        _sel = findSpecial(K_SYM);
        break;
      case K_LANG:
        _set = _letters = (_set == SET_BG) ? SET_EN : SET_BG;
        _sel = findSpecial(K_LANG);
        break;
      case K_SEND:
        askSend();
        break;
    }
  }

  void drawKey(DisplayDriver& d, int i, int x, int y, int w) {
    bool sel = i == _sel;
    d.setColor(UIColor::primary_txt);
    if (sel) { d.fillRect(x, y, w * CELL, CELL); d.setColor(COLOR_BLACK); }
    int nc = numChars();
    if (i < nc) {
      const char* p = utf8Skip(chars(), i);
      int bytes = utf8Skip(p, 1) - p;
      char ch[4];
      if (_shift && _set != SET_SYM) utf8Upper(ch, p, bytes);
      else { memcpy(ch, p, bytes); ch[bytes] = 0; }
      d.setCursor(x + 1, y);
      d.print(ch);
    } else {
      int sp = specialAt(i - nc);
      int cw = w * CELL;
      switch (sp) {
        case K_SHIFT: d.drawXbm(x, y, _shift ? icon_shift : icon_shift_o, 8, 8); break;
        case K_SPACE:
          d.fillRect(x + 2, y + 6, cw - 4, 1);
          d.fillRect(x + 2, y + 3, 1, 3);
          d.fillRect(x + cw - 3, y + 3, 1, 3);
          break;
        case K_SYM:
          d.setCursor(x + 3, y);
          d.print(_set != SET_SYM ? "123" : (_letters == SET_BG ? "абв" : "abc"));
          break;
        case K_LANG:
          d.setCursor(x + 2, y);
          d.print(_set == SET_BG ? "EN" : "БГ");
          break;
        case K_SEND:
          if (_edit) { d.setCursor(x + 2, y); d.print("OK"); }
          else d.drawXbm(x + 4, y, icon_mail, 8, 8);
          break;
      }
    }
    d.setColor(UIColor::primary_txt);
  }

  void renderText(DisplayDriver& d) {
    int tlen = utf8Len(_text);
    int glen = _has_rest ? utf8Len(_rest) : 0;
    int cursor_line = tlen / LINE_CHARS;
    int first = cursor_line - (TEXT_LINES - 1);
    if (first < 0) first = 0;
    char part[100];
    for (int r = 0; r < TEXT_LINES; r++) {
      int a = (first + r) * LINE_CHARS, b = a + LINE_CHARS;
      int y = 11 + r * 9;
      if (a < tlen) {
        utf8Slice(part, sizeof(part), _text, a, (b < tlen ? b : tlen) - a);
        d.setCursor(0, y);
        d.print(part);
      }
      int ga = a > tlen ? a : tlen, gb = b < tlen + glen ? b : tlen + glen;
      if (gb > ga) {   // suggestion, inverted
        utf8Slice(part, sizeof(part), _rest, ga - tlen, gb - ga);
        int x = (ga - a) * 6;
        d.fillRect(x, y - 1, (gb - ga) * 6, 9);
        d.setColor(COLOR_BLACK);
        d.setCursor(x, y);
        d.print(part);
        d.setColor(UIColor::primary_txt);
      }
      if (tlen >= a && tlen < b && glen == 0) d.fillRect((tlen - a) * 6, y - 1, 1, 9);   // cursor
    }
  }

public:
  ComposeScreen(UITask* task) : _task(task) {
    _text[0] = 0;
    _stash[0] = 0;
    memset(&_target, 0, sizeof(_target));
    memset(&_stash_target, 0, sizeof(_stash_target));
  }

  // write to t; 'prefill' (a quick reply to finish) replaces the text, otherwise an unsent draft
  // to the same recipient is kept
  static bool sameTarget(const ComposeTarget& a, const ComposeTarget& b) {
    return a.is_channel == b.is_channel &&
           (a.is_channel ? a.channel_idx == b.channel_idx : memcmp(a.pub_key, b.pub_key, PUB_KEY_SIZE) == 0);
  }

  // keep an unsent message aside (one draft, for the last recipient written to)
  void stashDraft() {
    if (_draft && !_edit && _len > 0) {
      memcpy(_stash, _text, _len + 1);
      _stash_target = _target;
    }
  }

  void openSend(const ComposeTarget& t, const char* prefill) {
    bool same = _draft && !_edit && sameTarget(t, _target);
    if (!same) stashDraft();
    _target = t;
    _edit = false;
    _max_len = MAX_TEXT_LEN;
    if (_target.is_channel) _max_len -= strlen(_task->prefs()->node_name) + 2;   // "<name>: " is prepended
    if (_max_len > MAX_TEXT_LEN || _max_len < 0) _max_len = MAX_TEXT_LEN;
    if (prefill) setText(prefill);
    else if (!same) setText(_stash[0] && sameTarget(t, _stash_target) ? _stash : "");
    if (!prefill && !same && sameTarget(t, _stash_target)) _stash[0] = 0;
    _draft = true;
    start();
  }

  void openEdit(int slot) {
    stashDraft();
    _edit = true;
    _draft = false;
    _slot = slot;
    _max_len = UI_QUICK_LEN - 1;
    setText(_task->quickReply(slot));
    start();
  }

  void start() {
    while (_len > _max_len) backspace();
    _set = _letters = (_task->prefs()->ui_lang % LANG_COUNT) == 0 ? SET_BG : SET_EN;
    _sel = 0;
    _undo_len = 0;
    autoShift();
    if (_len > 0 && _text[_len - 1] != ' ') _shift = false;
    updateSuggestion();
  }

  void setText(const char* s) {
    StrHelper::strncpy(_text, s, sizeof(_text));
    _len = strlen(_text);
  }

  void clear() { _len = 0; _text[0] = 0; _undo_len = 0; autoShift(); updateSuggestion(); }
  bool isEmpty() const { return _len == 0; }
  bool isEditing() const { return _edit; }
  void askSend();

  bool send() {
    // trailing spaces left by accepted words are not sent
    while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;
    if (!sendText(_task, _target, _text, _len)) return false;
    learnLastWord();
    clear();
    _draft = false;
    return true;
  }

  bool save() {
    while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;
    return _task->setQuickReply(_slot, _text);
  }

  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    char left[8];
    // room left in letters of the current layout (a Cyrillic letter takes 2 bytes)
    snprintf(left, sizeof(left), "%d", (_max_len - _len) / (_set == SET_BG ? 2 : 1));
    int lw = d.getTextWidth(left);
    char title[40];
    if (_edit) snprintf(title, sizeof(title), T(S_QUICK_N_FMT), _slot + 1);
    else snprintf(title, sizeof(title), "%s%s", _target.is_channel ? "#" : "", _target.name);
    drawMarquee(d, 0, 0, title, (d.width() - lw - 4) / 6, 0, false);
    d.drawTextRightAlign(d.width() - 1, 0, left);
    d.fillRect(0, 9, d.width(), 1);

    d.setColor(UIColor::primary_txt);
    renderText(d);
    for (int x = 0; x < d.width(); x += 2) d.fillRect(x, KB_TOP - 2, 1, 1);

    int nc = numChars(), n = numKeys(), col = 0, row = 0;
    for (int i = 0; i < n; i++) {
      int w = i < nc ? 1 : keyWidth(specialAt(i - nc));
      if (col + w > COLS) { col = 0; row++; }
      drawKey(d, i, col * CELL, KB_TOP + row * CELL, w);
      col += w;
    }
    return 5000;
  }

  bool handleInput(char c) override {
    int n = numKeys();
    if (c == KEY_NEXT) { _sel = (_sel + 1) % n; _undo_len = 0; return true; }
    if (c == KEY_PREV) { _sel = (_sel + n - 1) % n; _undo_len = 0; return true; }
    if (c == KEY_ENTER) {
      unsigned long now = millis();
      bool quick = _undo_len > 0 && now - _last_press < KB_DOUBLE_CLICK_MS;
      _last_press = now;
      if (!(quick && acceptSuggestion())) press();
      else _undo_len = 0;
      updateSuggestion();
      return true;
    }
    if (c == KEY_CANCEL) {
      if (_len == 0) return false;   // empty: leave
      backspace();
      _undo_len = 0;
      updateSuggestion();
      return true;
    }
    if (c == KEY_CONTEXT_MENU) {
      _undo_len = 0;
      return openComposeMenu();
    }
    return false;
  }
  bool openComposeMenu();
};

static ComposeScreen* compose_screen;

static void doSend(UITask* task) {
  if (compose_screen->send()) {
    closeWriting(task);
    task->notify(UIEventType::ack);
    task->showAlert(T(S_SENT), 1000);
  } else {
    task->showAlert(failText(S_SEND_FAIL), 1500);
  }
}

void ComposeScreen::askSend() {
  if (_edit) {   // quick reply: save straight away (an empty one removes it)
    if (save()) {
      if (_task->current() == this) _task->pop();
      _task->haptic(UI_HAPTIC_ACK_MS);
      _task->showAlert(T(S_SAVED), 800);
    } else {
      _task->showAlert(T(S_SAVE_FAIL), 1500);
    }
    return;
  }
  if (_len == 0) return;
  confirm_screen->setup(T(S_SEND_Q), doSend);
  _task->push(confirm_screen);
}

// hold on the keyboard: send / clear / discard
class ComposeMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_MENU)); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    snprintf(buf, n, "%s", T(i == 0 && compose_screen->isEditing() ? S_SAVE : (StrId)(S_SEND + i)));
  }
  bool onEnter(int i) override {
    _task->pop();   // close this pop-up
    if (i == 0) {
      compose_screen->askSend();
    } else {
      if (i == 1 || !compose_screen->isEditing()) compose_screen->clear();   // discarding an edit keeps the saved text
      if (i == 2 && _task->current() == compose_screen) _task->pop();
      _task->haptic(UI_HAPTIC_ACK_MS);
    }
    return true;
  }
public:
  ComposeMenuScreen(UITask* task) : ListScreen(task) { }
};

static ComposeMenuScreen* compose_menu;

bool ComposeScreen::openComposeMenu() {
  compose_menu->reset();
  _task->push(compose_menu);
  return true;
}

// recipient list: channels ("#") first, then chat / room contacts by name or by last heard.
// Sorted by name it has a row of first letters on top: turn left from the first entry to reach
// it, press to pick a letter, press again to jump to the first name with that letter.
class RecipientsScreen : public ListScreen {
  struct Entry { uint8_t pub_prefix[6]; uint32_t lastmod; char key[14]; };   // by key: the table can change while open
  struct Group { char label[3]; uint16_t first; };
  enum { BAR_OFF, BAR_FOCUS, BAR_ACTIVE };
  uint8_t _channels[64];
  int _num_channels = 0;
  Entry* _contacts;
  int _num_contacts = 0;
  Group _groups[64];   // '#', A..Z, А..Я, '*'
  int _num_groups = 0;
  uint8_t _bar = BAR_OFF;
  int _bar_sel = 0;

  bool byName() const { return _task->prefs()->ui_csort == 0; }

  // upper-case UTF-8 copy of the start of a name, used for sorting and the letter row
  static void foldName(const char* name, char* key, int key_size) {
    const uint8_t* p = (const uint8_t*)name;
    int j = 0;
    while (*p && j < key_size - 2) {
      if (*p >= 'a' && *p <= 'z') { key[j++] = *p++ - 32; continue; }
      if (*p == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) { key[j++] = 0xD0; key[j++] = p[1] - 0x20; p += 2; continue; }   // а..п
      if (*p == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { key[j++] = 0xD0; key[j++] = p[1] + 0x20; p += 2; continue; }   // р..я
      key[j++] = *p++;
    }
    key[j] = 0;
  }
  // first letter group of a folded key: "A".."Z", "А".."Я", or "*" for anything else
  static void groupLabel(const char* key, char* label) {
    uint8_t c = key[0];
    if (c >= 'A' && c <= 'Z') { label[0] = c; label[1] = 0; return; }
    if (c == 0xD0 && (uint8_t)key[1] >= 0x90 && (uint8_t)key[1] <= 0xAF) { label[0] = key[0]; label[1] = key[1]; label[2] = 0; return; }
    label[0] = '*'; label[1] = 0;
  }
  static bool isLetterKey(const char* key) {
    char l[3];
    groupLabel(key, l);
    return l[0] != '*';
  }
  bool before(const Entry& a, const Entry& b) const {
    if (!byName()) return a.lastmod > b.lastmod;
    bool la = isLetterKey(a.key), lb = isLetterKey(b.key);
    if (la != lb) return la;   // names starting with a symbol go last
    return strcmp(a.key, b.key) < 0;
  }
  int groupOf(int item) const {
    int g = 0;
    for (int i = 0; i < _num_groups; i++) if (_groups[i].first <= item) g = i;
    return g;
  }

protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, T(S_NEW_MSG)); }
  int count() override { return _num_channels + _num_contacts; }
  const char* emptyText() override { return T(S_NO_RECIPIENTS); }
  bool listFocused() override { return _bar == BAR_OFF; }

  bool renderHeader(DisplayDriver& d) override {
    if (!byName() || _num_groups == 0) return false;
    int cur = _bar == BAR_ACTIVE ? _bar_sel : groupOf(_sel);
    const int CELL_W = 8, VISIBLE = 16;
    int first = cur - VISIBLE / 2;
    if (first > _num_groups - VISIBLE) first = _num_groups - VISIBLE;
    if (first < 0) first = 0;
    d.setColor(UIColor::title_txt);
    for (int i = first; i < _num_groups && i < first + VISIBLE; i++) {
      int x = (i - first) * CELL_W;
      if (i == cur && _bar == BAR_ACTIVE) {
        d.fillRect(x, 0, CELL_W - 1, 9);
        d.setColor(COLOR_BLACK);
      } else if (i == cur && _bar == BAR_FOCUS) {
        d.drawRect(x, 0, CELL_W - 1, 9);
      } else if (i == cur) {
        d.fillRect(x + 1, 8, CELL_W - 3, 1);
      }
      d.setCursor(x + 1, 1);
      d.print(_groups[i].label);
      d.setColor(UIColor::title_txt);
    }
    d.fillRect(0, 9, d.width(), 1);
    return true;
  }

  bool target(int i, ComposeTarget& t) {
    memset(&t, 0, sizeof(t));
    if (i < _num_channels) {
#ifdef MAX_GROUP_CHANNELS
      ChannelDetails ch;
      if (!the_mesh.getChannel(_channels[i], ch) || !ch.name[0]) return false;
      t.is_channel = true;
      t.channel_idx = _channels[i];
      StrHelper::strncpy(t.name, ch.name, sizeof(t.name));
      return true;
#endif
    } else if (i - _num_channels < _num_contacts) {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_contacts[i - _num_channels].pub_prefix, 6);
      if (!c) return false;
      memcpy(t.pub_key, c->id.pub_key, PUB_KEY_SIZE);
      StrHelper::strncpy(t.name, c->name, sizeof(t.name));
      return true;
    }
    return false;
  }
  void label(int i, char* buf, size_t n) override {
    ComposeTarget t;
    if (target(i, t)) snprintf(buf, n, "%s%s", t.is_channel ? "#" : "", t.name);
    else buf[0] = 0;
  }
  bool onEnter(int i) override {
    ComposeTarget t;
    if (!target(i, t)) return false;
    openReplyChoice(_task, t);
    return true;
  }

public:
  RecipientsScreen(UITask* task) : ListScreen(task), _contacts(NULL) { }

  bool handleInput(char c) override {
    if (_bar == BAR_ACTIVE) {
      if (c == KEY_NEXT) { _bar_sel = (_bar_sel + 1) % _num_groups; return true; }
      if (c == KEY_PREV) { _bar_sel = (_bar_sel + _num_groups - 1) % _num_groups; return true; }
      if (c == KEY_ENTER) {
        int item = _groups[_bar_sel].first;
        select(item);
        int n = count();
        _top = item > n - ROWS ? (n > ROWS ? n - ROWS : 0) : item;   // the letter's first name on top
        _bar = BAR_OFF;
        return true;
      }
      if (c == KEY_CANCEL) { _bar = BAR_FOCUS; return true; }
      return true;
    }
    if (_bar == BAR_FOCUS) {
      if (c == KEY_ENTER) { _bar = BAR_ACTIVE; _bar_sel = groupOf(_sel); return true; }
      if (c == KEY_NEXT) { _bar = BAR_OFF; return true; }
      if (c == KEY_CANCEL) return false;   // leave
      return true;
    }
    if (c == KEY_PREV && _sel == 0 && byName() && _num_groups > 0) { _bar = BAR_FOCUS; return true; }
    return ListScreen::handleInput(c);
  }

  void open() {
    _num_channels = 0;
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS && _num_channels < (int)sizeof(_channels); i++) {
      if (the_mesh.getChannel(i, ch) && ch.name[0]) _channels[_num_channels++] = i;
    }
#endif
    _num_contacts = 0;
    if (_contacts == NULL) _contacts = new Entry[MAX_CONTACTS];   // kept once the list was used
    ContactInfo c;
    int total = the_mesh.getNumContacts() + MAX_ANON_CONTACTS;   // indices include the anonymous slots
    for (int i = MAX_ANON_CONTACTS; i < total && _num_contacts < MAX_CONTACTS; i++) {
      if (the_mesh.getContactByIdx(i, c) && (c.type == ADV_TYPE_CHAT || c.type == ADV_TYPE_ROOM)) {
        Entry& e = _contacts[_num_contacts++];
        memcpy(e.pub_prefix, c.id.pub_key, sizeof(e.pub_prefix));
        e.lastmod = c.lastmod;
        foldName(c.name, e.key, sizeof(e.key));
      }
    }
    // insertion sort: a few hundred entries, already mostly in order on the next open
    for (int i = 1; i < _num_contacts; i++) {
      Entry e = _contacts[i];
      int j = i - 1;
      while (j >= 0 && before(e, _contacts[j])) { _contacts[j + 1] = _contacts[j]; j--; }
      _contacts[j + 1] = e;
    }
    _num_groups = 0;
    if (_num_channels > 0) {
      strcpy(_groups[0].label, "#");
      _groups[0].first = 0;
      _num_groups = 1;
    }
    for (int i = 0; i < _num_contacts && _num_groups < (int)(sizeof(_groups) / sizeof(_groups[0])); i++) {
      char l[3];
      groupLabel(_contacts[i].key, l);
      if (_num_groups > 0 && strcmp(_groups[_num_groups - 1].label, l) == 0) continue;
      strcpy(_groups[_num_groups].label, l);
      _groups[_num_groups].first = _num_channels + i;
      _num_groups++;
    }
    _bar = BAR_OFF;
    reset();
  }
};

static RecipientsScreen* recipients_screen;

// after a contact or channel was picked: a quick reply or own text
class ReplyChoiceScreen : public ListScreen {
  ComposeTarget _target;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s%s", _target.is_channel ? "#" : "", _target.name); }
  int count() override { return 2; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", T(i == 0 ? S_QUICK_MSG : S_WRITE)); }
  bool onEnter(int i) override;
public:
  ReplyChoiceScreen(UITask* task) : ListScreen(task) { memset(&_target, 0, sizeof(_target)); }
  void open(const ComposeTarget& t) { _target = t; reset(); }
};

static ReplyChoiceScreen* reply_choice;

// list of the non-empty quick replies; picking one asks send / edit
class QuickPickScreen : public ListScreen {
  ComposeTarget _target;
  uint8_t _slots[UI_QUICK_COUNT];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_QUICK_MSGS)); }
  int count() override { return _n; }
  const char* emptyText() override { return T(S_NO_QUICK); }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _task->quickReply(_slots[i])); }
  bool onEnter(int i) override;
public:
  QuickPickScreen(UITask* task) : ListScreen(task) { memset(&_target, 0, sizeof(_target)); }
  void open(const ComposeTarget& t) {
    _target = t;
    _n = 0;
    for (int i = 0; i < UI_QUICK_COUNT; i++) if (_task->quickReply(i)[0]) _slots[_n++] = i;
    reset();
  }
};

static QuickPickScreen* quick_pick;

class QuickActionScreen : public ListScreen {
  ComposeTarget _target;
  char _text[UI_QUICK_LEN];
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _text); }
  int count() override { return 2; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", T(i == 0 ? S_SEND : S_ADD_TEXT)); }
  bool onEnter(int i) override {
    _task->pop();   // close this pop-up
    if (i == 0) {
      if (sendText(_task, _target, _text, strlen(_text))) {
        closeWriting(_task);
        _task->notify(UIEventType::ack);
        _task->showAlert(T(S_SENT), 1000);
      } else {
        _task->showAlert(failText(S_SEND_FAIL), 1500);
      }
    } else {
      compose_screen->openSend(_target, _text);
      _task->push(compose_screen);
    }
    return true;
  }
public:
  QuickActionScreen(UITask* task) : ListScreen(task) { memset(&_target, 0, sizeof(_target)); _text[0] = 0; }
  void open(const ComposeTarget& t, const char* text) { _target = t; StrHelper::strncpy(_text, text, sizeof(_text)); reset(); }
};

static QuickActionScreen* quick_action;

static void openReplyChoice(UITask* task, const ComposeTarget& t) {
  reply_choice->open(t);
  task->push(reply_choice);
}

bool ReplyChoiceScreen::onEnter(int i) {
  if (i == 0) {
    quick_pick->open(_target);
    _task->push(quick_pick);
  } else {
    compose_screen->openSend(_target, NULL);
    _task->push(compose_screen);
  }
  return true;
}

bool QuickPickScreen::onEnter(int i) {
  quick_action->open(_target, _task->quickReply(_slots[i]));
  _task->push(quick_action);
  return true;
}

// back out of all writing screens after a message went out
static void closeWriting(UITask* task) {
  for (int guard = 0; guard < 8; guard++) {
    UIScreen* s = task->current();
    if (s != compose_screen && s != compose_menu && s != reply_choice && s != quick_pick &&
        s != quick_action && s != recipients_screen) break;
    task->pop();
  }
}

static void replyTo(UITask* task, const UIMsgEntry* m) {
  ComposeTarget t;
  if (!findTarget(m, t)) {
    task->showAlert(T(S_NO_TARGET), 1200);
    return;
  }
  openReplyChoice(task, t);
}

// Settings > Quick replies: the slots, press to edit one on the keyboard
class QuickEditScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_QUICK_MSGS)); }
  int count() override { return UI_QUICK_COUNT; }
  void label(int i, char* buf, size_t n) override {
    const char* q = _task->quickReply(i);
    snprintf(buf, n, "%d. %s", i + 1, q[0] ? q : T(S_EMPTY_SLOT));
  }
  bool onEnter(int i) override {
    compose_screen->openEdit(i);
    _task->push(compose_screen);
    return true;
  }
public:
  QuickEditScreen(UITask* task) : ListScreen(task) { }
};

static QuickEditScreen* quick_edit;

// ---- settings

static uint8_t encoderSteps(const NodePrefs* p) {
  // only 2 or 4 are valid; anything else (0, corrupt prefs) falls back to the build default
  return (p->ui_enc_steps == 2 || p->ui_enc_steps == 4) ? p->ui_enc_steps : ENCODER_STEPS_PER_DETENT;
}

class SettingsScreen : public ListScreen {
  bool _editing = false;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, _editing ? T(S_SETTINGS_EDIT) : T(S_SETTINGS)); }
  int count() override { return 9; }
  void label(int i, char* buf, size_t n) override {
    NodePrefs* p = _task->prefs();
    switch (i) {
      case 0: snprintf(buf, n, T(S_LANG_FMT), LANG_NAMES[p->ui_lang % LANG_COUNT]); break;
      case 1: snprintf(buf, n, T(S_DOTS_FMT), T((StrId)(S_DOTS_BOTTOM + p->ui_dots % 3))); break;
      case 2: snprintf(buf, n, T(S_VIBE_FMT), p->vibe_quiet ? T(S_OFF) : T(S_ON)); break;
      case 3: snprintf(buf, n, _editing ? T(S_TZ_EDIT_FMT) : T(S_TZ_FMT), p->ui_tz); break;
      case 4: snprintf(buf, n, T(S_SCREEN_FMT), T((StrId)(S_OFF_15S + p->ui_off % 4))); break;
      case 5: snprintf(buf, n, T(S_ENC_FMT), T(p->ui_enc_rev ? S_ENC_REVERSED : S_ENC_NORMAL)); break;
      case 6: snprintf(buf, n, T(S_STEPS_FMT), encoderSteps(p)); break;
      case 7: snprintf(buf, n, T(S_CSORT_FMT), T(p->ui_csort ? S_CSORT_RECENT : S_CSORT_NAME)); break;
      default: snprintf(buf, n, "%s", T(S_QUICK_MSGS)); break;
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
      case 6: p->ui_enc_steps = encoderSteps(p) == 4 ? 2 : 4; _task->applyEncoderPrefs(); break;
      case 7: p->ui_csort = p->ui_csort ? 0 : 1; break;
      default: quick_edit->reset(); _task->push(quick_edit); return true;
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
  int count() override { return 4; }
  void label(int i, char* buf, size_t n) override {
    if (i == 0) {
      int u = _task->unreadCount();
      if (u > 0) snprintf(buf, n, T(S_MSGS_N), u);
      else snprintf(buf, n, T(S_MSGS));
    } else if (i == 1) {
      snprintf(buf, n, T(S_NEW_MSG));
    } else if (i == 2) {
      snprintf(buf, n, T(S_RECENT_ADV));
    } else {
      snprintf(buf, n, T(S_SETTINGS));
    }
  }
  bool onEnter(int i) override {
    if (i == 0) { messages_screen->reset(); _task->push(messages_screen); }
    else if (i == 1) { recipients_screen->open(); _task->push(recipients_screen); }
    else if (i == 2) { recent_screen->open(); _task->push(recent_screen); }
    else { settings_screen->reset(); _task->push(settings_screen); }
    return true;
  }
public:
  MainMenuScreen(UITask* task) : ListScreen(task) { }
};

static void doHibernate(UITask* task) { task->hibernate(); }

class QuickMenuScreen : public ListScreen {
  enum { ADVERT, TRANSMIT, BLUETOOTH,
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
      case TRANSMIT: snprintf(buf, n, T(S_TX_FMT), the_mesh.isTxAllowed() ? T(S_ON) : T(S_OFF)); break;
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
        if (the_mesh.isTxAllowed() && the_mesh.advert()) {
          _task->notify(UIEventType::ack);
          _task->showAlert(T(S_ADV_SENT), 1000);
        } else {
          _task->showAlert(failText(S_ADV_FAIL), 1000);
        }
        break;
      case TRANSMIT:
        if (the_mesh.isTxAllowed()) {
          the_mesh.setTxAllowed(false);
          _task->notify(UIEventType::ack);
        } else {
          _task->push(antenna_screen);   // turning it on asks about the antenna again
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
    if (!the_mesh.isTxAllowed()) {   // crossed-out "TX": receive only
      x -= 14;
      d.setCursor(x, 1);
      d.print("TX");
      d.fillRect(x - 1, 4, 13, 1);
    }
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
  antenna_screen = new AntennaScreen(this);
#ifdef UI_ASK_ANTENNA
  _ask_antenna = !the_mesh.isTxAllowed();
#endif
  compose_screen = new ComposeScreen(this);
  compose_menu = new ComposeMenuScreen(this);
  recipients_screen = new RecipientsScreen(this);
  reply_choice = new ReplyChoiceScreen(this);
  quick_pick = new QuickPickScreen(this);
  quick_action = new QuickActionScreen(this);
  quick_edit = new QuickEditScreen(this);
  loadQuickReplies();
  _depth = 0;   // splash until home()
  _next_refresh = 100;
}

#define QUICK_FILE "/ui_quick"

void UITask::loadQuickReplies() {
  char buf[UI_QUICK_COUNT * UI_QUICK_LEN];
  int n = the_mesh.loadUIFile(QUICK_FILE, (uint8_t*)buf, sizeof(buf) - 1);
  memset(_quick, 0, sizeof(_quick));
  _quick_custom = n >= 0;
  if (n <= 0) return;
  buf[n] = 0;
  char* p = buf;
  for (int i = 0; i < UI_QUICK_COUNT && p; i++) {   // one reply per line, empty lines are empty slots
    char* nl = strchr(p, '\n');
    if (nl) *nl = 0;
    StrHelper::strncpy(_quick[i], p, UI_QUICK_LEN);
    p = nl ? nl + 1 : NULL;
  }
}

const char* UITask::quickReply(int i) const {
  if (i < 0 || i >= UI_QUICK_COUNT) return "";
  if (!_quick_custom) return QUICK_DEFAULTS[i][_node_prefs ? _node_prefs->ui_lang % LANG_COUNT : 0];
  return _quick[i];
}

bool UITask::setQuickReply(int i, const char* text) {
  if (i < 0 || i >= UI_QUICK_COUNT) return false;
  bool was_custom = _quick_custom;
  char old[UI_QUICK_LEN];
  StrHelper::strncpy(old, _quick[i], sizeof(old));
  if (!_quick_custom) {   // first edit: the defaults of the current language become the user's list
    for (int k = 0; k < UI_QUICK_COUNT; k++) StrHelper::strncpy(_quick[k], quickReply(k), UI_QUICK_LEN);
    _quick_custom = true;
  }
  StrHelper::strncpy(_quick[i], text, UI_QUICK_LEN);
  for (char* c = _quick[i]; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
  char buf[UI_QUICK_COUNT * UI_QUICK_LEN];
  int len = 0;
  for (int k = 0; k < UI_QUICK_COUNT; k++) {
    int l = strlen(_quick[k]);
    memcpy(&buf[len], _quick[k], l);
    len += l;
    if (k < UI_QUICK_COUNT - 1) buf[len++] = '\n';
  }
  if (the_mesh.saveUIFile(QUICK_FILE, (uint8_t*)buf, len)) return true;
  StrHelper::strncpy(_quick[i], old, UI_QUICK_LEN);   // not saved: show what is on flash
  _quick_custom = was_custom;
  return false;
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
  if (_ask_antenna) _stack[_depth++] = antenna_screen;   // until answered, also after screen off
  _next_refresh = 0;
}

void UITask::answerAntenna(bool fitted) {
  _ask_antenna = false;
  the_mesh.setTxAllowed(fitted);
  if (current() == antenna_screen) pop();
  notify(UIEventType::ack);
  snprintf(_alert, sizeof(_alert), T(S_TX_FMT), fitted ? T(S_ON) : T(S_OFF));
  _alert_expiry = millis() + 1200;
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

void UITask::msgSource(bool is_channel, uint8_t channel_idx, const uint8_t* pub_key) {
  _next_src = is_channel ? MSG_SRC_CHANNEL : MSG_SRC_CONTACT;
  _next_channel = channel_idx;
  memset(_next_prefix, 0, sizeof(_next_prefix));
  if (pub_key) memcpy(_next_prefix, pub_key, sizeof(_next_prefix));
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
  m->src = _next_src;
  m->channel_idx = _next_channel;
  memcpy(m->pub_prefix, _next_prefix, sizeof(m->pub_prefix));
  _next_src = MSG_SRC_UNKNOWN;
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
  the_mesh.flushPendingSaves();   // contacts are otherwise written a few seconds after a change
  if (_display != NULL) {
    _display->turnOn();
    _display->startFrame();
    _display->setTextSize(1);
    _display->setColor(UIColor::warning_txt);
    _display->drawTextCentered(_display->width() / 2, 12, T(S_POWER_OFF));
    _display->setColor(UIColor::primary_txt);
    _display->drawTextCentered(_display->width() / 2, 32, T(S_BATT_OUT1));
    _display->drawTextCentered(_display->width() / 2, 44, T(S_BATT_OUT2));
    _display->endFrame();
  }
  unsigned long shown = millis();
#ifdef PIN_VIBRATION
  if (!_node_prefs->vibe_quiet) {
    vibration.pulse(UI_HAPTIC_LONG_MS);
    unsigned long t = millis();
    while (millis() - t < UI_HAPTIC_LONG_MS + 50) vibration.loop();
  }
#endif
  while (millis() - shown < 2500) delay(10);   // long enough to read
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
