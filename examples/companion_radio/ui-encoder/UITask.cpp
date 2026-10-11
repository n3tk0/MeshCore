#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#ifdef UI_SIM
  #include "UISim.h"          // desktop screen simulator (tools/ui-sim): stand-ins for the mesh
#else
  #include "../MyMesh.h"
#endif
#include "WordPredictor.h"
#include "UIHelpers.h"
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

#define BOOT_SCREEN_MILLIS   2500     // hold Back while the splash shows -> CLI rescue
#define MARQUEE_PAUSE_MILLIS 1000
#define MARQUEE_STEP_MILLIS  300
#define NEW_MSG_HIGHLIGHT    3000
#define SEND_UNDO_MILLIS     3000     // a message waits this long, Back cancels it
#define SIGNAL_STALE_MILLIS  (15 * 60 * 1000UL)   // nothing received for this long: no signal bars
#define VALID_EPOCH          1735689600UL   // 2025-01-01; VolatileRTCClock boots in May 2024, so earlier means "no clock yet"
#define LOW_BATT_PERCENT     10       // warn once below this
#define LOW_BATT_REARM       20       // ... and again after charging above this

// haptic dictionary (ms): key press, confirmed action, channel msg, direct msg, low battery / power off
#ifndef UI_HAPTIC_CLICK_MS
  #define UI_HAPTIC_CLICK_MS  25
#endif
#ifndef UI_HAPTIC_TICK_MS
  #define UI_HAPTIC_TICK_MS   0     // per encoder detent, off: the encoder has mechanical clicks
#endif
#define UI_HAPTIC_ACK_MS      120
#define UI_HAPTIC_DELIVERED   60
#define UI_HAPTIC_LONG_MS     600
static const uint16_t HAPTIC_CHANNEL[] = { 200 };
static const uint16_t HAPTIC_DIRECT[]  = { 150, 150, 150 };

#define COLOR_BLACK  0

#include "../ui-new/icons.h"

using ui::utf8Len;
using ui::utf8Skip;
using ui::utf8Slice;

// millis() wraps after 49.7 days: compare by difference, never by absolute value
static inline bool timeReached(unsigned long t) { return (long)(millis() - t) >= 0; }

// 8x8 icons, MSB first rows (Adafruit drawBitmap format)
static const uint8_t icon_mail[]    = { 0xFF, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0xFF, 0x00 };
static const uint8_t icon_bt[]      = { 0x20, 0x30, 0xA8, 0x70, 0xA8, 0x30, 0x20, 0x00 };
static const uint8_t icon_hash[]    = { 0x24, 0x24, 0x7E, 0x24, 0x7E, 0x24, 0x24, 0x00 };
static const uint8_t icon_person[]  = { 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x3C, 0x7E, 0x00 };
static const uint8_t icon_unread[]  = { 0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00 };
static const uint8_t icon_sent[]    = { 0x20, 0x30, 0x38, 0x3C, 0x38, 0x30, 0x20, 0x00 };
static const uint8_t icon_pending[] = { 0x3C, 0x42, 0x91, 0x91, 0x9D, 0x81, 0x42, 0x3C };
static const uint8_t icon_acked[]   = { 0x00, 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00 };
static const uint8_t icon_failed[]  = { 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00 };
static const uint8_t icon_bolt[]    = { 0x08, 0x10, 0x30, 0x7C, 0x18, 0x10, 0x20, 0x00 };
static const uint8_t icon_radio[]   = { 0x92, 0x54, 0x38, 0x10, 0x10, 0x10, 0x10, 0x00 };
static const uint8_t icon_batt[]    = { 0x18, 0x7E, 0x42, 0x5A, 0x5A, 0x42, 0x7E, 0x00 };
static const uint8_t icon_pin[]     = { 0x38, 0x44, 0x54, 0x44, 0x38, 0x10, 0x10, 0x00 };
static const uint8_t icon_knob[]    = { 0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C };

static const unsigned long AUTO_OFF_TABLE[] = { 15000, 30000, 60000, 300000 };
static const uint8_t CONTRAST_TABLE[] = { 0xFF, 0x60, 0x08 };
#define DOTS_BOTTOM 0
#define DOTS_RIGHT  1
#define DOTS_NONE   2

#define NOTIFY_NO_PHONE 0
#define NOTIFY_ALWAYS   1
#define NOTIFY_NEVER    2

#define ANT_ALWAYS      0
#define ANT_POWER_LOSS  1
#define ANT_NEVER       2

// ---------------------------------------------------------------- reset reason (antenna question)

// "after power loss": ask about the antenna only on a cold start (battery put in), not when waking
// from hibernate or after a reset. RESETREAS is empty only after a power-on reset.
#if defined(NRF52_PLATFORM) && !defined(UI_SIM)
  #include <nrf.h>
  #ifdef NRF52_POWER_MANAGEMENT
    extern uint32_t g_nrf52_reset_reason;
    static bool coldBoot() { return g_nrf52_reset_reason == 0; }
  #else
    static uint32_t ui_reset_reason = 0;
    // runs before the SoftDevice is enabled, so the register can still be written directly
    static void __attribute__((constructor(101))) ui_capture_reset_reason() {
      ui_reset_reason = NRF_POWER->RESETREAS;
      NRF_POWER->RESETREAS = 0xFFFFFFFF;   // the bits accumulate until cleared
    }
    static bool coldBoot() { return ui_reset_reason == 0; }
  #endif
#else
  static bool coldBoot() { return true; }
#endif

// ---------------------------------------------------------------- UI language (0 = BG, 1 = EN)

#define LANG_COUNT 2
static const char* LANG_NAMES[LANG_COUNT] = { "BG", "EN" };
static NodePrefs* lang_prefs = NULL;

enum StrId {
  S_AGE_S, S_AGE_M, S_AGE_H, S_AGE_D,
  S_EMPTY, S_YES, S_NO, S_ON, S_OFF,
  // messages
  S_MSGS, S_MSGS_N, S_NO_MSGS, S_ME, S_REPLY, S_RETRY, S_DEL, S_DEL_CONV, S_DEL_ALL, S_MARK_READ,
  S_DEL_Q, S_DEL_CONV_Q, S_DEL_ALL_Q, S_DELETED, S_ALL_DELETED, S_READ, S_NEW_FMT,
  S_NOT_DELIVERED_FMT,
  // writing
  S_WRITE, S_SEND, S_CLEAR, S_DISCARD, S_SENDING_TO, S_BACK_CANCEL, S_CANCELLED, S_SENT, S_SEND_FAIL,
  S_NO_TARGET, S_QUICK_MSGS, S_QUICK_N_FMT, S_EMPTY_SLOT, S_SAVE, S_SAVED, S_SAVE_FAIL,
  S_NAME_TITLE,
  // contacts / channels
  S_CONTACTS, S_CHANNELS, S_NO_CONTACTS, S_NO_CHANNELS, S_INFO, S_RESET_PATH, S_PATH_RESET,
  S_TYPE_FMT, S_TYPE_CHAT, S_TYPE_RPT, S_TYPE_ROOM, S_TYPE_SENSOR, S_HOPS_FMT, S_DIRECT, S_FLOOD,
  S_HEARD_AGO_FMT, S_ID_FMT,
  // menus
  S_MENU, S_QUICK, S_SEND_ADV, S_ADV_SENT, S_ADV_FAIL, S_HIBERNATE, S_HIB_Q,
  // standby
  S_MORE_FMT, S_PHONE, S_HEARD_FMT, S_NOBODY, S_NOISE_FMT, S_NO_GPS, S_FIX, S_NOFIX, S_SAT_FMT, S_ALT_FMT,
  S_CARD_RECENT, S_CARD_RADIO, S_CARD_BATT, S_CHARGING, S_ON_BATT, S_OFF_AT_FMT,
  S_POWER_OFF, S_BATT_EMPTY, S_BATT_LOW, S_BATT_OUT1, S_BATT_OUT2, S_WAKE_HINT,
  S_GPS_ON, S_GPS_OFF,
  // antenna / transmit
  S_ANT_Q, S_TX_FMT, S_TX_IS_OFF, S_ANT_NEVER_Q,
  // settings
  S_SETTINGS, S_GRP_DISPLAY, S_GRP_INPUT, S_GRP_MSGS, S_GRP_RADIO, S_GRP_SYSTEM,
  S_SET_OFF, S_SET_BRIGHT, S_SET_DOTS, S_SET_VIBE, S_SET_ENC, S_SET_STEPS, S_SET_SKIP,
  S_SET_NOTIFY, S_SET_DELCONF, S_SET_CSORT, S_SET_TXPOW, S_SET_ANT, S_SET_NAME, S_SET_LANG,
  S_SET_TZ, S_SET_DST, S_ABOUT, S_RESCUE, S_RESCUE_Q, S_EDIT_HINT,
  S_OFF_15S, S_OFF_30S, S_OFF_1M, S_OFF_5M,
  S_HIGH, S_MEDIUM, S_LOW,
  S_DOTS_BOTTOM, S_DOTS_RIGHT, S_DOTS_NONE,
  S_ENC_NORMAL, S_ENC_REVERSED, S_STEPS_FMT,
  S_NOTIFY_NO_PHONE, S_NOTIFY_ALWAYS, S_NOTIFY_NEVER,
  S_CSORT_NAME, S_CSORT_RECENT,
  S_ANT_ALWAYS, S_ANT_POWER_LOSS, S_ANT_NEVER,
  S_DST_EU,
  S_VERSION_FMT, S_BATT_FMT, S_CONTACTS_FMT, S_UPTIME_FMT, S_CLOCK_FMT, S_NO_CLOCK,
  S_RESCUE_L1, S_RESCUE_L2, S_RESCUE_L3,
  S_COUNT
};

// Copy rules: at most 21 characters per row (19 with an icon), the sentence case of the app,
// "advert" stays as in the MeshCore app, Bulgarian without abbreviations where it fits.
static const char* const STRINGS[S_COUNT][2] = {
  { "%luс", "%lus" },
  { "%luм", "%lum" },
  { "%luч", "%luh" },
  { "%luд", "%lud" },
  { "Няма записи", "No entries" },
  { "Натисни = да", "Press = yes" },
  { "Back = не", "Back = no" },
  { "вкл", "on" },
  { "изкл", "off" },
  // messages
  { "Съобщения", "Messages" },
  { "Съобщения (%d)", "Messages (%d)" },
  { "Няма съобщения", "No messages" },
  { "Аз", "Me" },
  { "Отговори", "Reply" },
  { "Изпрати пак", "Send again" },
  { "Изтрий", "Delete" },
  { "Изтрий разговора", "Delete conversation" },
  { "Изтрий всички", "Delete all" },
  { "Всички прочетени", "Mark all read" },
  { "Да се изтрие ли съобщението?", "Delete this message?" },
  { "Изтриване на разговора с %s?", "Delete conversation with %s?" },
  { "Изтриване на всички съобщения?", "Delete all messages?" },
  { "Изтрито", "Deleted" },
  { "Всички са изтрити", "All deleted" },
  { "Отбелязани", "Marked as read" },
  { "Ново: %s", "New: %s" },
  { "Не е доставено: %s", "Not delivered: %s" },
  // writing
  { "Напиши...", "Write..." },
  { "Изпрати", "Send" },
  { "Изчисти", "Clear" },
  { "Откажи", "Discard" },
  { "Изпращане до", "Sending to" },
  { "Back = отказ", "Back = cancel" },
  { "Отказано", "Cancelled" },
  { "Изпратено", "Sent" },
  { "Не е изпратено", "Send failed" },
  { "Няма такъв получател", "Unknown recipient" },
  { "Готови съобщения", "Quick replies" },
  { "Готово съобщение %d", "Quick reply %d" },
  { "(празно)", "(empty)" },
  { "Запази", "Save" },
  { "Запазено", "Saved" },
  { "Не е запазено", "Save failed" },
  { "Име на устройството", "Device name" },
  // contacts / channels
  { "Контакти", "Contacts" },
  { "Канали", "Channels" },
  { "Няма контакти", "No contacts" },
  { "Няма канали", "No channels" },
  { "Информация", "Details" },
  { "Нулирай пътя", "Reset path" },
  { "Пътят е нулиран", "Path reset" },
  { "Тип: %s", "Type: %s" },
  { "чат", "chat" },
  { "репийтър", "repeater" },
  { "стая", "room" },
  { "сензор", "sensor" },
  { "Хопове: %d", "Hops: %d" },
  { "Път: директно", "Path: direct" },
  { "Път: flood", "Path: flood" },
  { "Чут преди %s", "Heard %s ago" },
  { "ID: %s", "ID: %s" },
  // menus
  { "Меню", "Menu" },
  { "Бързо меню", "Quick menu" },
  { "Изпрати advert", "Send advert" },
  { "Advert-ът е изпратен", "Advert sent" },
  { "Advert не е изпратен", "Advert failed" },
  { "Хибернация", "Hibernate" },
  { "Хибернация?", "Hibernate?" },
  // standby
  { "+%d още", "+%d more" },
  { "Телефон: свързан", "Phone: connected" },
  { "Чут: %s %s", "Heard: %s %s" },
  { "Никой не е чут", "Nobody heard yet" },
  { "Шум %d", "Noise %d" },
  { "Няма GPS модул", "No GPS module" },
  { "fix", "fix" },
  { "без fix", "no fix" },
  { "%s %d сат.", "%s %d sat" },
  { "%.0f м", "%.0f m" },
  { "Чути наскоро", "Recently heard" },
  { "Радио", "Radio" },
  { "Батерия", "Battery" },
  { "Зарежда (USB)", "Charging (USB)" },
  { "На батерия", "On battery" },
  { "Изкл. под %d.%02d V", "Off below %d.%02d V" },
  { "Изключване...", "Powering off..." },
  { "Батерията е изтощена", "Battery empty" },
  { "Батерия под 10%", "Battery below 10%" },
  { "Батерията може", "Battery can" },
  { "да се извади", "be removed" },
  { "Включване: задръж", "To wake: hold" },
  { "GPS: вкл", "GPS: on" },
  { "GPS: изкл", "GPS: off" },
  // antenna / transmit
  { "Има ли антена?", "Antenna fitted?" },
  { "Предаване: %s", "Transmit: %s" },
  { "Предаването е изкл.", "Transmit is off" },
  { "Без антена PA изгаря. Да не питам?", "No antenna burns the PA. Never ask?" },
  // settings
  { "Настройки", "Settings" },
  { "Екран", "Display" },
  { "Управление", "Controls" },
  { "Съобщения", "Messages" },
  { "Радио", "Radio" },
  { "Система", "System" },
  { "Изгасване", "Screen off" },
  { "Яркост", "Brightness" },
  { "Точки", "Page dots" },
  { "Вибрация", "Vibration" },
  { "Посока", "Direction" },
  { "Щрак", "Detent" },
  { "Скок към буква", "Letter jump" },
  { "Известия", "Alerts" },
  { "Потвърди триене", "Confirm delete" },
  { "Контакти", "Contacts" },
  { "Мощност", "TX power" },
  { "Антена", "Antenna" },
  { "Име", "Name" },
  { "Език", "Language" },
  { "Часова зона", "Time zone" },
  { "Лятно време", "Summer time" },
  { "За устройството", "About" },
  { "CLI rescue", "CLI rescue" },
  { "CLI rescue? Приложението спира.", "CLI rescue? The app stops." },
  { "Натисни = запази", "Press = save" },
  { "15 с", "15 s" },
  { "30 с", "30 s" },
  { "1 мин", "1 min" },
  { "5 мин", "5 min" },
  { "висока", "high" },
  { "средна", "medium" },
  { "ниска", "low" },
  { "отдолу", "bottom" },
  { "вдясно", "right" },
  { "скрити", "hidden" },
  { "нормална", "normal" },
  { "обърната", "reversed" },
  { "%d стъпки", "%d steps" },
  { "без телефон", "no phone" },
  { "винаги", "always" },
  { "никога", "never" },
  { "по име", "by name" },
  { "последни", "recent" },
  { "винаги", "always" },
  { "нова батерия", "new battery" },
  { "никога", "never" },
  { "ЕС", "EU" },
  { "Версия %s", "Version %s" },
  { "Батерия %d.%02d V %d%%", "Battery %d.%02d V %d%%" },
  { "Контакти %d/%d", "Contacts %d/%d" },
  { "Работи от %s", "Up for %s" },
  { "Час %02lu:%02lu%s", "Time %02lu:%02lu%s" },
  { "Час: няма", "Time: not set" },
  { "CLI rescue", "CLI rescue" },
  { "USB сериен порт", "USB serial port" },
  { "Back = рестарт", "Back = restart" },
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

// ---------------------------------------------------------------- text helpers

static void formatAge(char* buf, size_t n, uint32_t secs) {
  if (secs < 60) snprintf(buf, n, T(S_AGE_S), (unsigned long)secs);
  else if (secs < 3600) snprintf(buf, n, T(S_AGE_M), (unsigned long)(secs / 60));
  else if (secs < 86400) snprintf(buf, n, T(S_AGE_H), (unsigned long)(secs / 3600));
  else snprintf(buf, n, T(S_AGE_D), (unsigned long)(secs / 86400));
}

// age of a timestamp taken by our own clock; "" while the clock is not set or it is in the future
static void ageOf(char* buf, size_t n, uint32_t ts) {
  uint32_t now = rtc_clock.getCurrentTime();
  if (ts < VALID_EPOCH || now < ts) { buf[0] = 0; return; }
  formatAge(buf, n, now - ts);
}

// Draws text clipped to max_chars; if it is longer and 'scroll' is set, it scrolls (marquee)
// instead of being cut. Returns true while scrolling, so the caller re-renders soon.
static bool drawMarquee(DisplayDriver& d, int x, int y, const char* text, int max_chars, unsigned long since, bool scroll) {
  char filtered[200];
  d.translateUTF8ToBlocks(filtered, text, sizeof(filtered));
  int len = utf8Len(filtered);
  int offset = 0;
  bool active = false;
  if (max_chars < 1) return false;
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

// up to two centered lines, broken at a space
static void drawTwoLines(DisplayDriver& d, int y, const char* text, int line_chars) {
  uint8_t starts[2], lens[2];
  int n = ui::wrapLines(text, line_chars, starts, lens, 2);
  char line[100];
  for (int i = 0; i < n; i++) {
    utf8Slice(line, sizeof(line), text + starts[i], 0, lens[i]);
    d.drawTextCentered(d.width() / 2, n == 1 ? y + 5 : y + i * 10, line);
  }
}

static void drawBattery(DisplayDriver& d, int x, int y, int pct) {
  d.drawRect(x, y, 12, 7);
  d.fillRect(x + 12, y + 2, 2, 3);
  d.fillRect(x + 2, y + 2, (pct * 8) / 100, 3);
}

// 4 bars by the SNR of the last packet received
static void drawSignal(DisplayDriver& d, int x, int y, float snr) {
  int bars = snr > 5 ? 4 : snr > 0 ? 3 : snr > -7 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    if (i < bars) d.fillRect(x + i * 2, y + 8 - h, 1, h);
    else d.fillRect(x + i * 2, y + 7, 1, 1);
  }
}

static int battPercent(uint16_t mv) { return ui::lipoPercent(mv); }

static const uint8_t* stateIcon(const UIMsgEntry* m) {
  switch (m->state) {
    case MSG_OUT_SENT: return icon_sent;
    case MSG_OUT_PENDING: return icon_pending;
    case MSG_OUT_ACKED: return icon_acked;
    case MSG_OUT_FAILED: return icon_failed;
    default: return m->unread ? icon_unread : NULL;
  }
}

// same conversation: the same channel, the same contact, or (unknown source) the same name
static bool sameConversation(const UIMsgEntry* a, const UIMsgEntry* b) {
  if (a->src != b->src) return false;
  if (a->src == MSG_SRC_CHANNEL) return a->channel_idx == b->channel_idx && strcmp(a->from, b->from) == 0;
  if (a->src == MSG_SRC_CONTACT) return memcmp(a->pub_prefix, b->pub_prefix, sizeof(a->pub_prefix)) == 0;
  return strcmp(a->from, b->from) == 0;
}

// ---------------------------------------------------------------- base screens

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
    if (timeReached(dismiss_after)) _task->home();
  }
};

// CLI rescue: the serial CLI runs instead of the app protocol until the device restarts
class RescueScreen : public UIScreen {
  UITask* _task;
public:
  RescueScreen(UITask* task) : _task(task) { }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::warning_txt);
    d.drawRect(0, 0, d.width(), d.height());
    d.drawTextCentered(d.width() / 2, 8, T(S_RESCUE_L1));
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(d.width() / 2, 26, T(S_RESCUE_L2));
    d.drawTextCentered(d.width() / 2, 44, T(S_RESCUE_L3));
    return 5000;
  }
  bool handleInput(char c) override {
    if (c == KEY_CANCEL) _task->shutdown(true);
    return true;   // nothing else leaves this screen
  }
};

static RescueScreen* rescue_screen;

// Generic scrolling list: title row, up to 5 rows, inverted selection, scrollbar, marquee on the
// selected row, optional 8x8 icon on the left and short text on the right of each row.
// Short lists wrap around, long ones stop at the ends.
class ListScreen : public UIScreen {
protected:
  UITask* _task;
  int _sel = 0, _top = 0;
  unsigned long _sel_since = 0;
  static const int ROWS = 5, ROW_H = 10, TOP = 12;

  virtual void title(char* buf, size_t n) = 0;
  virtual int count() = 0;
  virtual void label(int i, char* buf, size_t n) = 0;
  virtual const uint8_t* icon(int i) { return NULL; }
  virtual bool hasIcons() { return false; }                         // reserve the icon column
  virtual void rightText(int i, char* buf, size_t n) { buf[0] = 0; }
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
  void selectLast() { _sel = 0; _top = 0; select(count() - 1); }

  int render(DisplayDriver& d) override {
    char buf[200], right[16];
    int n = count();
    if (_sel >= n) select(n - 1);
    if (_top > _sel) _top = _sel;
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    bool scrolling = false;
    if (!renderHeader(d)) {
      title(buf, sizeof(buf));
      scrolling = drawMarquee(d, 0, 0, buf, n > ROWS ? 15 : 21, _sel_since, true);
      if (n > ROWS) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d/%d", _sel + 1, n);
        d.drawTextRightAlign(d.width() - 1, 0, pos);
      }
      d.fillRect(0, 9, d.width(), 1);
    }
    d.setColor(UIColor::primary_txt);

    if (n == 0) {
      drawTwoLines(d, 27, emptyText(), 21);
      return scrolling ? MARQUEE_STEP_MILLIS : 1000;
    }
    int text_w = (n > ROWS) ? d.width() - 4 : d.width();
    int x0 = hasIcons() ? 11 : 1;
    for (int r = 0; r < ROWS && _top + r < n; r++) {
      int i = _top + r;
      int y = TOP + r * ROW_H;
      label(i, buf, sizeof(buf));
      rightText(i, right, sizeof(right));
      int right_w = right[0] ? d.getTextWidth(right) + 3 : 0;
      int max_chars = (text_w - x0 - right_w - 1) / 6;
      const uint8_t* ic = icon(i);
      bool selected = i == _sel && listFocused();
      if (selected) {
        d.setColor(UIColor::primary_txt);
        d.fillRect(0, y - 1, text_w, ROW_H);
        d.setColor(COLOR_BLACK);
      }
      if (ic) d.drawXbm(1, y, ic, 8, 8);
      if (drawMarquee(d, x0, y, buf, max_chars, selected ? _sel_since : 0, selected)) scrolling = true;
      if (right[0]) d.drawTextRightAlign(text_w - 2, y, right);
      d.setColor(UIColor::primary_txt);
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

// Yes/no dialog: Enter = yes, Back = no (handled by the stack pop). The question names its object
// ("Delete conversation with Ivan?") and wraps over two lines.
class ConfirmScreen : public UIScreen {
  UITask* _task;
  char _question[96];
  void (*_action)(UITask*);
public:
  ConfirmScreen(UITask* task) : _task(task), _action(NULL) { _question[0] = 0; }
  void setup(const char* question, void (*action)(UITask*)) {
    StrHelper::strncpy(_question, question, sizeof(_question));
    _action = action;
  }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    d.drawRect(0, 0, d.width(), d.height());
    drawTwoLines(d, 8, _question, 20);
    d.fillRect(8, 31, d.width() - 16, 1);
    d.drawTextCentered(d.width() / 2, 37, T(S_YES));
    d.drawTextCentered(d.width() / 2, 49, T(S_NO));
    return 1000;
  }
  bool handleInput(char c) override {
    if (c == KEY_ENTER) {
      _task->pop();
      if (_action) _action(_task);
      return true;
    }
    return c == KEY_NEXT || c == KEY_PREV;   // turning does nothing here
  }
};

static ConfirmScreen* confirm_screen;

static void askConfirm(UITask* task, const char* question, void (*action)(UITask*)) {
  confirm_screen->setup(question, action);
  task->push(confirm_screen);
}

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
    if (c != KEY_ENTER && c != KEY_CANCEL) return c == KEY_NEXT || c == KEY_PREV;
    _task->answerAntenna(c == KEY_ENTER);
    return true;
  }
};

static AntennaScreen* antenna_screen;

static void turnTransmitOn(UITask* task) {
  if (task->prefs()->ui_ant % 3 == ANT_NEVER) task->answerAntenna(true);
  else task->push(antenna_screen);   // turning it on asks about the antenna again
}

// ---------------------------------------------------------------- recipients and sending

struct ComposeTarget {
  bool is_channel;
  uint8_t channel_idx;
  uint8_t pub_key[PUB_KEY_SIZE];
  char name[32];
};

static bool targetForContact(const ContactInfo& c, ComposeTarget& t) {
  memset(&t, 0, sizeof(t));
  memcpy(t.pub_key, c.id.pub_key, PUB_KEY_SIZE);
  StrHelper::strncpy(t.name, c.name, sizeof(t.name));
  return true;
}

static bool targetForChannel(int idx, ComposeTarget& t) {
  memset(&t, 0, sizeof(t));
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails ch;
  if (!the_mesh.getChannel(idx, ch) || !ch.name[0]) return false;
  t.is_channel = true;
  t.channel_idx = idx;
  StrHelper::strncpy(t.name, ch.name, sizeof(t.name));
  return true;
#else
  return false;
#endif
}

// who to answer for a message in the history: its recorded source, or (older entries) a channel
// or contact with the conversation's name
static bool findTarget(const UIMsgEntry* m, ComposeTarget& t) {
  memset(&t, 0, sizeof(t));
  const char* name = m->from;
  if (m->src == MSG_SRC_CONTACT) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(m->pub_prefix, sizeof(m->pub_prefix));
    return c && targetForContact(*c, t);
  }
#ifdef MAX_GROUP_CHANNELS
  if (m->src == MSG_SRC_CHANNEL) {
    // the slot may have been reassigned from the phone since: it must still carry the same name
    ChannelDetails ch;
    if (the_mesh.getChannel(m->channel_idx, ch) && ch.name[0] &&
        strncmp(ch.name, m->from, sizeof(m->from) - 1) == 0) {
      return targetForChannel(m->channel_idx, t);
    }
    // otherwise fall through to the search by name below
  }
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, ch) && ch.name[0] && strcmp(ch.name, name) == 0) return targetForChannel(i, t);
  }
  if (m->src == MSG_SRC_CHANNEL) return false;   // never answer a channel message privately
#endif
  ContactInfo c;
  // getContactByIdx() counts the reserved anonymous slots, getNumContacts() does not
  for (int i = MAX_ANON_CONTACTS; i < the_mesh.getNumContacts() + MAX_ANON_CONTACTS; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type != ADV_TYPE_NONE && strcmp(c.name, name) == 0) {
      return targetForContact(c, t);
    }
  }
  return false;
}

// Sends right away; on success the message goes into the history (direct ones wait for an ACK).
static UIMsgEntry* sendNow(UITask* task, const ComposeTarget& t, const char* text) {
  int len = strlen(text);
  if (len <= 0 || !the_mesh.isTxAllowed()) return NULL;
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  if (t.is_channel) {
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails ch;
    if (the_mesh.getChannel(t.channel_idx, ch) && ch.name[0] &&
        the_mesh.sendGroupMessage(ts, ch.channel, task->prefs()->node_name, text, len)) {
      return task->addSent(t.name, MSG_SRC_CHANNEL, t.channel_idx, NULL, text, 0, 0);
    }
#endif
    return NULL;
  }
  ContactInfo* c = the_mesh.lookupContactByPubKey(t.pub_key, PUB_KEY_SIZE);
  uint32_t ack = 0, timeout = 0;
  if (!c || the_mesh.sendMessage(*c, ts, 0, text, ack, timeout) == MSG_SEND_FAILED) return NULL;
  return task->addSent(t.name, MSG_SRC_CONTACT, 0, t.pub_key, text, ack, timeout);
}

static void closeWriting(UITask* task);

// ---- sending with undo: the message waits SEND_UNDO_MILLIS, Back cancels it

class SendUndoScreen : public UIScreen {
  UITask* _task;
  ComposeTarget _target;
  char _text[MAX_TEXT_LEN + 1];
  unsigned long _started = 0;
  bool _from_compose = false;
public:
  SendUndoScreen(UITask* task) : _task(task) { _text[0] = 0; memset(&_target, 0, sizeof(_target)); }
  void open(const ComposeTarget& t, const char* text, bool from_compose) {
    _target = t;
    StrHelper::strncpy(_text, text, sizeof(_text));
    _from_compose = from_compose;
    _started = millis();
  }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s%s", T(S_SENDING_TO), _target.is_channel ? "#" : "", _target.name);
    drawMarquee(d, 0, 0, buf, 21, _started, true);
    d.fillRect(0, 9, d.width(), 1);
    d.setColor(UIColor::primary_txt);
    uint8_t starts[3], lens[3];
    int n = ui::wrapLines(_text, 21, starts, lens, 3);
    char line[100], filtered[100];
    for (int i = 0; i < n; i++) {
      utf8Slice(line, sizeof(line), _text + starts[i], 0, lens[i]);
      d.translateUTF8ToBlocks(filtered, line, sizeof(filtered));
      d.setCursor(0, 13 + i * 9);
      d.print(filtered);
    }
    unsigned long el = millis() - _started;
    if (el > SEND_UNDO_MILLIS) el = SEND_UNDO_MILLIS;
    int w = (int)((SEND_UNDO_MILLIS - el) * (unsigned long)d.width() / SEND_UNDO_MILLIS);
    d.fillRect(0, 43, w, 3);
    d.drawTextCentered(d.width() / 2, 52, T(S_BACK_CANCEL));
    return 100;
  }
  void poll() override;
  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->pop();
      _task->showAlert(T(S_CANCELLED), 800);
      return true;
    }
    return true;   // everything else waits for the send
  }
};

static SendUndoScreen* send_undo;

static void sendWithUndo(UITask* task, const ComposeTarget& t, const char* text, bool from_compose) {
  send_undo->open(t, text, from_compose);
  task->push(send_undo);
}

// ---------------------------------------------------------------- keyboard

#ifndef UI_DRAFTS
  #define UI_DRAFTS 4   // unsent messages kept, each for a different recipient
#endif

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

static void utf8Lower(char* dest, const char* ch, int bytes) {
  memcpy(dest, ch, bytes);
  dest[bytes] = 0;
  if (bytes == 1 && dest[0] >= 'A' && dest[0] <= 'Z') dest[0] += 32;
  if (bytes == 2) {
    uint16_t cp = ((uint16_t)(dest[0] & 0x1F) << 6) | (dest[1] & 0x3F);
    if (cp >= 0x0410 && cp <= 0x042F) {
      cp += 0x20;
      dest[0] = 0xC0 | (cp >> 6);
      dest[1] = 0x80 | (cp & 0x3F);
    }
  }
}

static const uint8_t icon_shift[]   = { 0x10, 0x38, 0x7C, 0xFE, 0x38, 0x38, 0x38, 0x00 };
static const uint8_t icon_shift_o[] = { 0x10, 0x28, 0x44, 0xEE, 0x28, 0x28, 0x38, 0x00 };

// Jog-dial keyboard: turn = move over the keys, press = type, Back = delete, hold = menu.
// While a word is being typed the dictionary suggests its completion; the cursor then sits on the
// suggestion (shown inverted), so a press takes it plus a space. Turning leaves the suggestion and
// lands on its next letter ("letter jump", Settings) or back on the last letter typed.
class ComposeScreen : public UIScreen {
public:
  enum Mode { MODE_SEND, MODE_QUICK, MODE_NAME };
private:
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
  bool _on_word = false;                      // the cursor is on the suggestion
  uint8_t _mode = MODE_SEND;
  int _slot = 0;                              // quick reply being edited
  bool _draft = false;                        // _text is an unsent message for _target
  // unsent messages while the keyboard is used for another one, one per recipient (least recently
  // stashed is dropped when all are taken)
  struct Draft {
    ComposeTarget target;
    char text[MAX_TEXT_LEN + 1];
    uint32_t used;
  };
  Draft _drafts[UI_DRAFTS];
  uint32_t _draft_clock = 0;

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
  // key of a character (either case), -1 if the current set has none
  int keyOf(const char* ch) const {
    int bytes = utf8Skip(ch, 1) - ch;
    if (bytes <= 0 || bytes > 2) return -1;
    char low[4];
    utf8Lower(low, ch, bytes);
    const char* p = chars();
    for (int i = 0; *p; i++) {
      const char* q = utf8Skip(p, 1);
      if (q - p == bytes && memcmp(p, low, bytes) == 0) return i;
      p = q;
    }
    return -1;
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

  // after every edit: a suggestion, if any, takes the cursor
  void updateSuggestion() {
    int ws = wordStart();
    _has_rest = _mode != MODE_NAME && ws < _len && _predict.suggest(&_text[ws], _rest, sizeof(_rest)) &&
                _len + (int)strlen(_rest) + 1 <= _max_len;
    _on_word = _has_rest;
  }

  void autoShift() {   // capital letter at the start and after . ! ?
    if (_mode == MODE_NAME) { _shift = false; return; }
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

  void acceptSuggestion() {
    append(_rest);
    learnLastWord();
    append(" ");
    autoShift();
    _task->haptic(UI_HAPTIC_ACK_MS);
  }

  // turning off the suggestion: onto its next letter, or back to the key last typed
  void leaveSuggestion() {
    _on_word = false;
    if (_task->prefs()->ui_skip == 0) {
      int k = keyOf(_rest);
      if (k >= 0) _sel = k;
    }
  }

  void press() {
    int nc = numChars();
    if (_sel < nc) {
      const char* p = utf8Skip(chars(), _sel);
      int bytes = utf8Skip(p, 1) - p;
      char ch[4];
      if (_shift) utf8Upper(ch, p, bytes);
      else { memcpy(ch, p, bytes); ch[bytes] = 0; }
      if (append(ch)) {
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
        _set = (_set == SET_SYM) ? (int)_letters : (int)SET_SYM;
        _sel = findSpecial(K_SYM);
        break;
      case K_LANG:
        _set = _letters = (_set == SET_BG) ? SET_EN : SET_BG;
        _sel = findSpecial(K_LANG);
        break;
      case K_SEND:
        finish();
        break;
    }
  }

  void drawKey(DisplayDriver& d, int i, int x, int y, int w) {
    bool sel = i == _sel && !_on_word;
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
          if (_mode != MODE_SEND) { d.setCursor(x + 2, y); d.print("OK"); }
          else d.drawXbm(x + 4, y, icon_mail, 8, 8);
          break;
      }
    }
    d.setColor(UIColor::primary_txt);
  }

  void renderText(DisplayDriver& d) {
    int tlen = utf8Len(_text);
    int glen = _has_rest ? utf8Len(_rest) : 0;
    int cursor_line = (tlen + glen) / LINE_CHARS;
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
      if (gb > ga) {   // suggestion: inverted while the cursor is on it, underlined otherwise
        utf8Slice(part, sizeof(part), _rest, ga - tlen, gb - ga);
        int x = (ga - a) * 6;
        if (_on_word) {
          d.fillRect(x, y - 1, (gb - ga) * 6, 9);
          d.setColor(COLOR_BLACK);
        } else {
          for (int u = x; u < x + (gb - ga) * 6; u += 2) d.fillRect(u, y + 8, 1, 1);
        }
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
    memset(&_target, 0, sizeof(_target));
    memset(_drafts, 0, sizeof(_drafts));
  }

  static bool sameTarget(const ComposeTarget& a, const ComposeTarget& b) {
    return a.is_channel == b.is_channel &&
           (a.is_channel ? a.channel_idx == b.channel_idx : memcmp(a.pub_key, b.pub_key, PUB_KEY_SIZE) == 0);
  }

  Draft* findDraft(const ComposeTarget& t) {
    for (int i = 0; i < UI_DRAFTS; i++) {
      if (_drafts[i].text[0] && sameTarget(_drafts[i].target, t)) return &_drafts[i];
    }
    return NULL;
  }

  // keep an unsent message aside, in its recipient's slot, a free one, or the oldest
  void stashDraft() {
    if (!_draft || _mode != MODE_SEND || _len == 0) return;
    Draft* d = findDraft(_target);
    for (int i = 0; !d && i < UI_DRAFTS; i++) if (!_drafts[i].text[0]) d = &_drafts[i];
    if (!d) {
      d = &_drafts[0];
      for (int i = 1; i < UI_DRAFTS; i++) if (_drafts[i].used < d->used) d = &_drafts[i];
    }
    memcpy(d->text, _text, _len + 1);
    d->target = _target;
    d->used = ++_draft_clock;
  }

  // write to t; 'prefill' (a quick reply to finish) replaces the text, otherwise an unsent draft
  // to the same recipient is kept
  void openSend(const ComposeTarget& t, const char* prefill) {
    bool same = _draft && _mode == MODE_SEND && sameTarget(t, _target);
    if (!same) stashDraft();
    _target = t;
    _mode = MODE_SEND;
    _max_len = MAX_TEXT_LEN;
    if (_target.is_channel) _max_len -= strlen(_task->prefs()->node_name) + 2;   // "<name>: " is prepended
    if (_max_len > MAX_TEXT_LEN || _max_len < 0) _max_len = MAX_TEXT_LEN;
    if (prefill) {
      setText(prefill);
    } else if (!same) {
      Draft* d = findDraft(t);
      setText(d ? d->text : "");
      if (d) d->text[0] = 0;   // the draft is back in the editor
    }
    _draft = true;
    start();
  }

  void openEdit(int slot) {
    stashDraft();
    _mode = MODE_QUICK;
    _draft = false;
    _slot = slot;
    _max_len = UI_QUICK_LEN - 1;
    setText(_task->quickReply(slot));
    start();
  }

  void openName() {
    stashDraft();
    _mode = MODE_NAME;
    _draft = false;
    _max_len = sizeof(_task->prefs()->node_name) - 1;
    setText(_task->prefs()->node_name);
    start();
  }

  void start() {
    while (_len > _max_len) backspace();
    _set = _letters = (_task->prefs()->ui_lang % LANG_COUNT) == 0 ? SET_BG : SET_EN;
    _sel = 0;
    autoShift();
    if (_len > 0 && _text[_len - 1] != ' ') _shift = false;
    updateSuggestion();
  }

  void setText(const char* s) {
    StrHelper::strncpy(_text, s, sizeof(_text));
    _len = strlen(_text);
  }

  void clear() { _len = 0; _text[0] = 0; autoShift(); updateSuggestion(); }
  bool isEmpty() const { return _len == 0; }
  uint8_t mode() const { return _mode; }
  void sent() { learnLastWord(); clear(); _draft = false; }   // the message went out
  void finish();

  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    char left[8];
    // room left in letters of the current layout (a Cyrillic letter takes 2 bytes)
    snprintf(left, sizeof(left), "%d", (_max_len - _len) / (_set == SET_BG ? 2 : 1));
    int lw = d.getTextWidth(left);
    char title[40];
    if (_mode == MODE_QUICK) snprintf(title, sizeof(title), T(S_QUICK_N_FMT), _slot + 1);
    else if (_mode == MODE_NAME) snprintf(title, sizeof(title), "%s", T(S_NAME_TITLE));
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
    if (c == KEY_NEXT || c == KEY_PREV) {
      if (_on_word) { leaveSuggestion(); return true; }
      _sel = (c == KEY_NEXT) ? (_sel + 1) % n : (_sel + n - 1) % n;
      return true;
    }
    if (c == KEY_ENTER) {
      if (_on_word) acceptSuggestion();
      else press();
      updateSuggestion();
      return true;
    }
    if (c == KEY_CANCEL) {
      if (_len == 0) return false;   // empty: leave
      backspace();
      updateSuggestion();
      return true;
    }
    if (c == KEY_CONTEXT_MENU) return openComposeMenu();
    return false;
  }
  bool openComposeMenu();
};

static ComposeScreen* compose_screen;

void SendUndoScreen::poll() {
  if (!timeReached(_started + SEND_UNDO_MILLIS)) return;
  _task->pop();   // this screen
  if (sendNow(_task, _target, _text)) {
    if (_from_compose) compose_screen->sent();
    closeWriting(_task);
    _task->notify(UIEventType::ack);
    _task->showAlert(T(S_SENT), 1000);
  } else {
    _task->showAlert(failText(S_SEND_FAIL), 1500);
  }
}

void ComposeScreen::finish() {
  while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;   // trailing spaces of accepted words
  if (_mode == MODE_QUICK) {   // quick reply: saved at once (an empty one removes it)
    if (_task->setQuickReply(_slot, _text)) {
      if (_task->current() == this) _task->pop();
      _task->haptic(UI_HAPTIC_ACK_MS);
      _task->showAlert(T(S_SAVED), 800);
    } else {
      _task->showAlert(T(S_SAVE_FAIL), 1500);
    }
    return;
  }
  if (_mode == MODE_NAME) {
    if (_len == 0) return;
    StrHelper::strncpy(_task->prefs()->node_name, _text, sizeof(_task->prefs()->node_name));
    the_mesh.savePrefs();
    if (_task->current() == this) _task->pop();
    _task->haptic(UI_HAPTIC_ACK_MS);
    _task->showAlert(T(S_SAVED), 800);
    return;
  }
  if (_len == 0) return;
  sendWithUndo(_task, _target, _text, true);
}

// hold on the keyboard: send / clear / discard
class ComposeMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_MENU)); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    snprintf(buf, n, "%s", T(i == 0 && compose_screen->mode() != ComposeScreen::MODE_SEND ? S_SAVE : (StrId)(S_SEND + i)));
  }
  bool onEnter(int i) override {
    _task->pop();   // close this pop-up
    if (i == 0) {
      compose_screen->finish();
    } else {
      bool editing = compose_screen->mode() != ComposeScreen::MODE_SEND;
      if (i == 1 || !editing) compose_screen->clear();   // discarding an edit keeps the saved text
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

// ---- what to send: "Write..." first, then the quick replies. Press a quick reply = send it (with
// undo), hold = finish it on the keyboard first.

class ReplyPickScreen : public ListScreen {
  ComposeTarget _target;
  uint8_t _slots[UI_QUICK_COUNT];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s%s", _target.is_channel ? "#" : "", _target.name); }
  int count() override { return 1 + _n; }
  void label(int i, char* buf, size_t n) override {
    if (i == 0) snprintf(buf, n, "%s", T(S_WRITE));
    else snprintf(buf, n, "%s", _task->quickReply(_slots[i - 1]));
  }
  bool onEnter(int i) override {
    if (i == 0) {
      compose_screen->openSend(_target, NULL);
      _task->push(compose_screen);
    } else {
      sendWithUndo(_task, _target, _task->quickReply(_slots[i - 1]), false);
    }
    return true;
  }
  bool onContext(int i) override {
    compose_screen->openSend(_target, i == 0 ? NULL : _task->quickReply(_slots[i - 1]));
    _task->push(compose_screen);
    return true;
  }
public:
  ReplyPickScreen(UITask* task) : ListScreen(task) { memset(&_target, 0, sizeof(_target)); }
  void open(const ComposeTarget& t) {
    _target = t;
    _n = 0;
    for (int i = 0; i < UI_QUICK_COUNT; i++) if (_task->quickReply(i)[0]) _slots[_n++] = i;
    reset();
  }
};

static ReplyPickScreen* reply_pick;

static void openReplyPick(UITask* task, const ComposeTarget& t) {
  reply_pick->open(t);
  task->push(reply_pick);
}

static void replyTo(UITask* task, const UIMsgEntry* m) {
  ComposeTarget t;
  if (!findTarget(m, t)) {
    task->showAlert(T(S_NO_TARGET), 1200);
    return;
  }
  openReplyPick(task, t);
}

// back out of the writing screens after a message went out
static void closeWriting(UITask* task) {
  for (int guard = 0; guard < 8; guard++) {
    UIScreen* s = task->current();
    if (s != compose_screen && s != compose_menu && s != reply_pick && s != send_undo) break;
    task->pop();
  }
}

// ---------------------------------------------------------------- messages: conversations

// pending context for confirm callbacks (dialogs are modal, one at a time)
static uint32_t confirm_seq = 0;

static void deleteConversationOf(UITask* task, uint32_t seq) {
  int idx = task->historyIndexOf(seq);
  if (idx < 0) return;
  UIMsgEntry key = *task->historyAt(idx);
  for (int i = task->historyCount() - 1; i >= 0; i--) {
    if (sameConversation(task->historyAt(i), &key)) task->deleteHistory(i);
  }
}

class MessageViewScreen;
class ThreadScreen;
static MessageViewScreen* message_view;
static ThreadScreen* thread_screen;

// one row per conversation, newest first: icon (# channel, person, envelope = unread), name,
// unread count or age
class ConversationsScreen : public ListScreen {
  uint32_t _conv[UI_MSG_HISTORY];   // seq of the newest message of each conversation
  int _n = 0;

  void rebuild() {
    _n = 0;
    for (int i = 0; i < _task->historyCount(); i++) {
      UIMsgEntry* m = _task->historyAt(i);
      bool seen = false;
      for (int k = 0; k < _n && !seen; k++) {
        UIMsgEntry* o = _task->historyAt(_task->historyIndexOf(_conv[k]));
        if (o && sameConversation(o, m)) seen = true;
      }
      if (!seen) _conv[_n++] = m->seq;
    }
  }
  UIMsgEntry* entry(int i) { return (i >= 0 && i < _n) ? _task->historyAt(_task->historyIndexOf(_conv[i])) : NULL; }
  int unreadIn(const UIMsgEntry* key) {
    int u = 0;
    for (int i = 0; i < _task->historyCount(); i++) {
      UIMsgEntry* m = _task->historyAt(i);
      if (m->unread && sameConversation(m, key)) u++;
    }
    return u;
  }
protected:
  void title(char* buf, size_t n) override {
    int u = _task->unreadCount();
    if (u > 0) snprintf(buf, n, T(S_MSGS_N), u);
    else snprintf(buf, n, "%s", T(S_MSGS));
  }
  int count() override { rebuild(); return _n; }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override {
    UIMsgEntry* m = entry(i);
    if (!m) return NULL;
    if (unreadIn(m) > 0) return icon_mail;
    return m->src == MSG_SRC_CHANNEL ? icon_hash : icon_person;
  }
  void label(int i, char* buf, size_t n) override {
    UIMsgEntry* m = entry(i);
    if (m) snprintf(buf, n, "%s", m->from);
    else buf[0] = 0;
  }
  void rightText(int i, char* buf, size_t n) override {
    UIMsgEntry* m = entry(i);
    if (!m) { buf[0] = 0; return; }
    int u = unreadIn(m);
    if (u > 0) snprintf(buf, n, "%d", u);
    else ageOf(buf, n, m->timestamp);
  }
  const char* emptyText() override { return T(S_NO_MSGS); }
  bool onEnter(int i) override;
  bool onContext(int i) override;
public:
  ConversationsScreen(UITask* task) : ListScreen(task) { }
  uint32_t selectedSeq() { UIMsgEntry* m = entry(_sel); return m ? m->seq : 0; }
};

static ConversationsScreen* conversations;

// pop-up on the conversation list
class ConvActionsScreen : public ListScreen {
  uint32_t _seq = 0;
protected:
  void title(char* buf, size_t n) override {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    snprintf(buf, n, "%s", m ? m->from : T(S_MSGS));
  }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_DEL_CONV, S_DEL_ALL, S_MARK_READ };
    snprintf(buf, n, "%s", T(L[i]));
  }
  bool onEnter(int i) override;
public:
  ConvActionsScreen(UITask* task) : ListScreen(task) { }
  void open(uint32_t seq) { _seq = seq; reset(); }
};

static ConvActionsScreen* conv_actions;

static void doDeleteConv(UITask* task) {
  deleteConversationOf(task, confirm_seq);
  task->haptic(UI_HAPTIC_ACK_MS);
  task->showAlert(T(S_DELETED), 800);
}

static void doDeleteAll(UITask* task) {
  task->clearHistory();
  task->haptic(UI_HAPTIC_ACK_MS);
  task->showAlert(T(S_ALL_DELETED), 800);
}

static void askDeleteConv(UITask* task, uint32_t seq) {
  UIMsgEntry* m = task->historyAt(task->historyIndexOf(seq));
  if (!m) return;
  confirm_seq = seq;
  char q[96];
  snprintf(q, sizeof(q), T(S_DEL_CONV_Q), m->from);
  askConfirm(task, q, doDeleteConv);
}

bool ConvActionsScreen::onEnter(int i) {
  _task->pop();   // close this pop-up
  if (i == 0) askDeleteConv(_task, _seq);
  else if (i == 1) askConfirm(_task, T(S_DEL_ALL_Q), doDeleteAll);
  else {
    _task->markAllRead();
    _task->haptic(UI_HAPTIC_ACK_MS);
    _task->showAlert(T(S_READ), 800);
  }
  return true;
}

bool ConversationsScreen::onContext(int i) {
  conv_actions->open(selectedSeq());
  _task->push(conv_actions);
  return true;
}

// ---- one conversation, oldest first, opened at the newest message

class ThreadScreen : public ListScreen {
  UIMsgEntry _key;                  // copy of a message of the conversation (its identity fields)
  uint32_t _items[UI_MSG_HISTORY];  // seqs, oldest first
  int _n = 0;

  void rebuild() {
    _n = 0;
    for (int i = _task->historyCount() - 1; i >= 0; i--) {
      UIMsgEntry* m = _task->historyAt(i);
      if (sameConversation(m, &_key)) _items[_n++] = m->seq;
    }
  }
protected:
  UIMsgEntry* entry(int i) { return (i >= 0 && i < _n) ? _task->historyAt(_task->historyIndexOf(_items[i])) : NULL; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s%s", _key.src == MSG_SRC_CHANNEL ? "#" : "", _key.from); }
  int count() override { rebuild(); return _n; }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override { UIMsgEntry* m = entry(i); return m ? stateIcon(m) : NULL; }
  void label(int i, char* buf, size_t n) override {
    UIMsgEntry* m = entry(i);
    if (m) snprintf(buf, n, "%s", m->text);   // channel texts already start with "sender: "
    else buf[0] = 0;
  }
  bool onEnter(int i) override;
  bool onContext(int i) override;
public:
  ThreadScreen(UITask* task) : ListScreen(task) { memset(&_key, 0, sizeof(_key)); }
  // open the conversation of message 'seq', with 'seq' selected
  void open(uint32_t seq) {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(seq));
    if (!m) return;
    _key = *m;
    rebuild();
    reset();
    for (int i = 0; i < _n; i++) if (_items[i] == seq) select(i);
  }
  int render(DisplayDriver& d) override {
    if (count() == 0) { _task->pop(); return 100; }   // the whole conversation was deleted
    // what is on screen counts as read
    for (int r = 0; r < ROWS && _top + r < _n; r++) {
      UIMsgEntry* m = entry(_top + r);
      if (m && m->state == MSG_IN) m->unread = false;
    }
    return ListScreen::render(d);
  }
  uint32_t selectedSeq() { UIMsgEntry* m = entry(_sel); return m ? m->seq : 0; }
};

bool ConversationsScreen::onEnter(int i) {
  UIMsgEntry* m = entry(i);
  if (!m) return false;
  thread_screen->open(m->seq);
  _task->push(thread_screen);
  return true;
}

// ---- one message: full text, turn to scroll, press = reply, hold = actions

class MessageActionsScreen;
static MessageActionsScreen* message_actions;
static void openMessageActions(UITask* task, uint32_t seq);

class MessageViewScreen : public UIScreen {
  UITask* _task;
  uint32_t _seq = 0;   // the message shown; new messages shift history indexes
  int _scroll = 0;
  static const int MAX_LINES = 12, LINE_CHARS = 21, VISIBLE = 5;
  uint8_t _lines[MAX_LINES];       // start of each line, as offset into the entry text (entries move)
  uint8_t _line_len[MAX_LINES];    // characters per line
  int _num_lines = 0;
  unsigned long _opened = 0;

public:
  MessageViewScreen(UITask* task) : _task(task) { }
  void open(uint32_t seq) {
    _scroll = 0;
    _opened = millis();
    _num_lines = 0;
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(seq));
    _seq = m ? m->seq : 0;
    if (m) { m->unread = false; _num_lines = ui::wrapLines(m->text, LINE_CHARS, _lines, _line_len, MAX_LINES); }
  }
  int render(DisplayDriver& d) override {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    if (!m) { _task->pop(); return 100; }
    char age[12];
    ageOf(age, sizeof(age), m->timestamp);
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    int right_x = d.width() - 1;
    if (age[0]) { d.drawTextRightAlign(right_x, 0, age); right_x -= d.getTextWidth(age) + 2; }
    const uint8_t* st = m->state != MSG_IN ? stateIcon(m) : NULL;
    if (st) { right_x -= 8; d.drawXbm(right_x, 0, st, 8, 8); right_x -= 2; }
    char who[48];
    if (m->state == MSG_IN) snprintf(who, sizeof(who), "%s", m->from);
    else snprintf(who, sizeof(who), "%s > %s", T(S_ME), m->from);
    bool scrolling = drawMarquee(d, 0, 0, who, (right_x - 2) / 6, _opened, true);
    d.fillRect(0, 9, d.width(), 1);
    d.setColor(UIColor::primary_txt);
    char line[100], filtered[100];
    for (int r = 0; r < VISIBLE && _scroll + r < _num_lines; r++) {
      utf8Slice(line, sizeof(line), m->text + _lines[_scroll + r], 0, _line_len[_scroll + r]);
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
  bool handleInput(char c) override {
    if (c == KEY_NEXT) { if (_scroll + VISIBLE < _num_lines) _scroll++; return true; }
    if (c == KEY_PREV) { if (_scroll > 0) _scroll--; return true; }
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    if (!m) return false;
    if (c == KEY_ENTER) { replyTo(_task, m); return true; }
    if (c == KEY_CONTEXT_MENU) { openMessageActions(_task, _seq); return true; }
    return false;
  }
};

bool ThreadScreen::onEnter(int i) {
  UIMsgEntry* m = entry(i);
  if (!m) return false;
  message_view->open(m->seq);
  _task->push(message_view);
  return true;
}

bool ThreadScreen::onContext(int i) {
  UIMsgEntry* m = entry(i);
  if (!m) return false;
  openMessageActions(_task, m->seq);
  return true;
}

// pop-up for one message: reply, send again (when not delivered), delete, delete conversation
class MessageActionsScreen : public ListScreen {
  uint32_t _seq = 0;   // the message acted on, independent of history shifts
  enum { A_REPLY, A_RETRY, A_DEL, A_DEL_CONV };
  uint8_t _acts[4];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    snprintf(buf, n, "%s", m ? m->from : T(S_MSGS));
  }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_REPLY, S_RETRY, S_DEL, S_DEL_CONV };
    snprintf(buf, n, "%s", T(L[_acts[i]]));
  }
  bool onEnter(int i) override;
public:
  MessageActionsScreen(UITask* task) : ListScreen(task) { }
  void open(uint32_t seq) {
    _seq = seq;
    _n = 0;
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(seq));
    _acts[_n++] = A_REPLY;
    if (m && m->state == MSG_OUT_FAILED) _acts[_n++] = A_RETRY;
    _acts[_n++] = A_DEL;
    _acts[_n++] = A_DEL_CONV;
    reset();
  }
};

static void openMessageActions(UITask* task, uint32_t seq) {
  message_actions->open(seq);
  task->push(message_actions);
}

static void doDeleteOne(UITask* task) {
  int idx = task->historyIndexOf(confirm_seq);
  if (idx < 0) return;   // already gone (pushed out of the history)
  if (task->current() == message_view) task->pop();
  task->deleteHistory(idx);
  task->haptic(UI_HAPTIC_ACK_MS);
  task->showAlert(T(S_DELETED), 800);
}

// send a failed direct message again: the new copy waits for its ACK, the failed one goes
static void resend(UITask* task, uint32_t seq) {
  UIMsgEntry* m = task->historyAt(task->historyIndexOf(seq));
  if (!m) return;
  ComposeTarget t;
  if (!findTarget(m, t)) { task->showAlert(T(S_NO_TARGET), 1200); return; }
  char text[sizeof(m->text)];
  StrHelper::strncpy(text, m->text, sizeof(text));
  if (!sendNow(task, t, text)) { task->showAlert(failText(S_SEND_FAIL), 1500); return; }
  int old = task->historyIndexOf(seq);   // still there unless the full history pushed it out
  if (old >= 0) task->deleteHistory(old);
  task->notify(UIEventType::ack);
  task->showAlert(T(S_SENT), 1000);
}

bool MessageActionsScreen::onEnter(int i) {
  _task->pop();   // close this pop-up
  UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
  if (!m) return true;
  switch (_acts[i]) {
    case A_REPLY:
      m->unread = false;
      replyTo(_task, m);
      break;
    case A_RETRY:
      resend(_task, _seq);
      break;
    case A_DEL:
      confirm_seq = _seq;
      if (_task->prefs()->ui_delconf) doDeleteOne(_task);
      else askConfirm(_task, T(S_DEL_Q), doDeleteOne);
      break;
    default:
      askDeleteConv(_task, _seq);
      break;
  }
  return true;
}

// ---------------------------------------------------------------- contacts and channels

static const char* contactTypeName(uint8_t type) {
  switch (type) {
    case ADV_TYPE_REPEATER: return T(S_TYPE_RPT);
    case ADV_TYPE_ROOM: return T(S_TYPE_ROOM);
    case ADV_TYPE_SENSOR: return T(S_TYPE_SENSOR);
    default: return T(S_TYPE_CHAT);
  }
}

// read-only details of one contact
class ContactInfoScreen : public ListScreen {
  uint8_t _prefix[6];
  char _lines[6][40];
  int _n = 0;
  char _name[32];
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _name); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _lines[i]); }
public:
  ContactInfoScreen(UITask* task) : ListScreen(task) { _name[0] = 0; }
  void open(const uint8_t* pub_key) {
    memcpy(_prefix, pub_key, sizeof(_prefix));
    _n = 0;
    ContactInfo* c = the_mesh.lookupContactByPubKey(_prefix, sizeof(_prefix));
    if (!c) return;
    StrHelper::strncpy(_name, c->name, sizeof(_name));
    snprintf(_lines[_n++], 40, T(S_TYPE_FMT), contactTypeName(c->type));
    if (c->out_path_len == OUT_PATH_UNKNOWN) snprintf(_lines[_n++], 40, "%s", T(S_FLOOD));
    else if ((c->out_path_len & 63) == 0) snprintf(_lines[_n++], 40, "%s", T(S_DIRECT));
    else snprintf(_lines[_n++], 40, T(S_HOPS_FMT), c->out_path_len & 63);
    char age[12];
    ageOf(age, sizeof(age), c->lastmod);
    if (age[0]) snprintf(_lines[_n++], 40, T(S_HEARD_AGO_FMT), age);
    char hex[16];
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", c->id.pub_key[0], c->id.pub_key[1], c->id.pub_key[2], c->id.pub_key[3]);
    snprintf(_lines[_n++], 40, T(S_ID_FMT), hex);
    if (c->gps_lat != 0 || c->gps_lon != 0) {
      snprintf(_lines[_n++], 40, "%.4f %.4f", c->gps_lat / 1000000.0, c->gps_lon / 1000000.0);
    }
    reset();
  }
};

static ContactInfoScreen* contact_info;

// pop-up on a contact: write, details, reset path
class ContactActionsScreen : public ListScreen {
  uint8_t _key[PUB_KEY_SIZE];
  char _name[32];
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _name); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_WRITE, S_INFO, S_RESET_PATH };
    snprintf(buf, n, "%s", T(L[i]));
  }
  bool onEnter(int i) override {
    _task->pop();
    ContactInfo* c = the_mesh.lookupContactByPubKey(_key, PUB_KEY_SIZE);
    if (!c) { _task->showAlert(T(S_NO_TARGET), 1200); return true; }
    if (i == 0) {
      ComposeTarget t;
      targetForContact(*c, t);
      openReplyPick(_task, t);
    } else if (i == 1) {
      contact_info->open(c->id.pub_key);
      _task->push(contact_info);
    } else {
      the_mesh.resetContactPath(*c);
      _task->haptic(UI_HAPTIC_ACK_MS);
      _task->showAlert(T(S_PATH_RESET), 1000);
    }
    return true;
  }
public:
  ContactActionsScreen(UITask* task) : ListScreen(task) { _name[0] = 0; }
  void open(const ContactInfo& c) {
    memcpy(_key, c.id.pub_key, PUB_KEY_SIZE);
    StrHelper::strncpy(_name, c.name, sizeof(_name));
    reset();
  }
};

static ContactActionsScreen* contact_actions;

// Contacts: chat and room contacts by name or by last heard. Sorted by name it has a row of first
// letters on top: turn left from the first entry to reach it, press to pick a letter, press again
// to jump to the first name with that letter. Press = write, hold = more.
class ContactsScreen : public ListScreen {
  struct Entry { uint8_t pub_prefix[6]; uint32_t lastmod; char key[14]; };   // by key: the table can change while open
  struct Group { char label[3]; uint16_t first; };
  enum { BAR_OFF, BAR_FOCUS, BAR_ACTIVE };
  Entry* _contacts;
  int _num_contacts = 0;
  Group _groups[64];   // A..Z, А..Я, '*'
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
  ContactInfo* contact(int i) {
    return (i >= 0 && i < _num_contacts) ? the_mesh.lookupContactByPubKey(_contacts[i].pub_prefix, 6) : NULL;
  }

protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_CONTACTS)); }
  int count() override { return _num_contacts; }
  const char* emptyText() override { return T(S_NO_CONTACTS); }
  bool listFocused() override { return _bar == BAR_OFF; }
  void label(int i, char* buf, size_t n) override {
    ContactInfo* c = contact(i);
    if (c) snprintf(buf, n, "%s", c->name);
    else buf[0] = 0;
  }
  void rightText(int i, char* buf, size_t n) override {
    ContactInfo* c = contact(i);
    if (c) ageOf(buf, n, c->lastmod);
    else buf[0] = 0;
  }

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

  bool onEnter(int i) override {
    ContactInfo* c = contact(i);
    if (!c) return false;
    ComposeTarget t;
    targetForContact(*c, t);
    openReplyPick(_task, t);
    return true;
  }
  bool onContext(int i) override {
    ContactInfo* c = contact(i);
    if (!c) return false;
    contact_actions->open(*c);
    _task->push(contact_actions);
    return true;
  }

public:
  ContactsScreen(UITask* task) : ListScreen(task), _contacts(NULL) { }

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
    for (int i = 0; i < _num_contacts && _num_groups < (int)(sizeof(_groups) / sizeof(_groups[0])); i++) {
      char l[3];
      groupLabel(_contacts[i].key, l);
      if (_num_groups > 0 && strcmp(_groups[_num_groups - 1].label, l) == 0) continue;
      snprintf(_groups[_num_groups].label, sizeof(_groups[_num_groups].label), "%s", l);
      _groups[_num_groups].first = i;
      _num_groups++;
    }
    _bar = BAR_OFF;
    reset();
  }
};

static ContactsScreen* contacts_screen;

class ChannelsScreen : public ListScreen {
  uint8_t _channels[64];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_CHANNELS)); }
  int count() override { return _n; }
  const char* emptyText() override { return T(S_NO_CHANNELS); }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override { return icon_hash; }
  void label(int i, char* buf, size_t n) override {
    ComposeTarget t;
    if (targetForChannel(_channels[i], t)) snprintf(buf, n, "%s", t.name);
    else buf[0] = 0;
  }
  bool onEnter(int i) override {
    ComposeTarget t;
    if (!targetForChannel(_channels[i], t)) return false;
    openReplyPick(_task, t);
    return true;
  }
public:
  ChannelsScreen(UITask* task) : ListScreen(task) { }
  void open() {
    _n = 0;
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS && _n < (int)sizeof(_channels); i++) {
      if (the_mesh.getChannel(i, ch) && ch.name[0]) _channels[_n++] = i;
    }
#endif
    reset();
  }
};

static ChannelsScreen* channels_screen;

// ---------------------------------------------------------------- settings

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

static uint8_t encoderSteps(const NodePrefs* p) {
  // only 2 or 4 are valid; anything else (0, corrupt prefs) falls back to the build default
  return (p->ui_enc_steps == 2 || p->ui_enc_steps == 4) ? p->ui_enc_steps : ENCODER_STEPS_PER_DETENT;
}

enum SettingId : uint8_t {
  ST_OFF, ST_BRIGHT, ST_DOTS,
  ST_VIBE, ST_ENC, ST_STEPS, ST_SKIP,
  ST_NOTIFY, ST_DELCONF, ST_CSORT, ST_QUICK,
  ST_TX, ST_TXPOW, ST_ANT,
  ST_NAME, ST_LANG, ST_TZ, ST_DST, ST_ABOUT, ST_RESCUE,
};

#define TZ_MIN  -12
#define TZ_MAX   14
#define TXPOW_MIN -9

static StrId settingName(uint8_t id) {
  static const StrId N[] = {
    S_SET_OFF, S_SET_BRIGHT, S_SET_DOTS,
    S_SET_VIBE, S_SET_ENC, S_SET_STEPS, S_SET_SKIP,
    S_SET_NOTIFY, S_SET_DELCONF, S_SET_CSORT, S_QUICK_MSGS,
    S_TX_FMT, S_SET_TXPOW, S_SET_ANT,
    S_SET_NAME, S_SET_LANG, S_SET_TZ, S_SET_DST, S_ABOUT, S_RESCUE,
  };
  return N[id];
}

// number of values; 0 = opens a screen or runs an action
static int optCount(uint8_t id) {
  switch (id) {
    case ST_OFF: return 4;
    case ST_BRIGHT: case ST_DOTS: case ST_NOTIFY: case ST_ANT: return 3;
    case ST_VIBE: case ST_ENC: case ST_STEPS: case ST_SKIP: case ST_DELCONF: case ST_CSORT:
    case ST_TX: case ST_LANG: case ST_DST: return 2;
    case ST_TXPOW: return MAX_LORA_TX_POWER - TXPOW_MIN + 1;
    case ST_TZ: return TZ_MAX - TZ_MIN + 1;
    default: return 0;
  }
}

static int getOpt(UITask* task, uint8_t id) {
  NodePrefs* p = task->prefs();
  switch (id) {
    case ST_OFF: return p->ui_off % 4;
    case ST_BRIGHT: return p->ui_bright % 3;
    case ST_DOTS: return p->ui_dots % 3;
    case ST_VIBE: return p->vibe_quiet ? 0 : 1;
    case ST_ENC: return p->ui_enc_rev ? 1 : 0;
    case ST_STEPS: return encoderSteps(p) == 2 ? 0 : 1;
    case ST_SKIP: return p->ui_skip ? 0 : 1;
    case ST_NOTIFY: return p->ui_notify % 3;
    case ST_DELCONF: return p->ui_delconf ? 0 : 1;
    case ST_CSORT: return p->ui_csort ? 1 : 0;
    case ST_TX: return the_mesh.isTxAllowed() ? 1 : 0;
    case ST_TXPOW: return p->tx_power_dbm - TXPOW_MIN;
    case ST_ANT: return p->ui_ant % 3;
    case ST_LANG: return p->ui_lang % LANG_COUNT;
    case ST_TZ: return p->ui_tz - TZ_MIN;
    case ST_DST: return p->ui_dst ? 1 : 0;
    default: return 0;
  }
}

static void optText(uint8_t id, int v, char* buf, size_t n) {
  switch (id) {
    case ST_OFF: snprintf(buf, n, "%s", T((StrId)(S_OFF_15S + v))); break;
    case ST_BRIGHT: snprintf(buf, n, "%s", T((StrId)(S_HIGH + v))); break;
    case ST_DOTS: snprintf(buf, n, "%s", T((StrId)(S_DOTS_BOTTOM + v))); break;
    case ST_ENC: snprintf(buf, n, "%s", T(v ? S_ENC_REVERSED : S_ENC_NORMAL)); break;
    case ST_STEPS: snprintf(buf, n, T(S_STEPS_FMT), v ? 4 : 2); break;
    case ST_NOTIFY: snprintf(buf, n, "%s", T((StrId)(S_NOTIFY_NO_PHONE + v))); break;
    case ST_CSORT: snprintf(buf, n, "%s", T(v ? S_CSORT_RECENT : S_CSORT_NAME)); break;
    case ST_TXPOW: snprintf(buf, n, "%d dBm", v + TXPOW_MIN); break;
    case ST_ANT: snprintf(buf, n, "%s", T((StrId)(S_ANT_ALWAYS + v))); break;
    case ST_LANG: snprintf(buf, n, "%s", LANG_NAMES[v % LANG_COUNT]); break;
    case ST_TZ: snprintf(buf, n, "UTC%+d", v + TZ_MIN); break;
    case ST_DST: snprintf(buf, n, "%s", T(v ? S_DST_EU : S_OFF)); break;
    default: snprintf(buf, n, "%s", T(v ? S_ON : S_OFF)); break;
  }
}

// applies a value; the caller saves the prefs
static void setOpt(UITask* task, uint8_t id, int v) {
  NodePrefs* p = task->prefs();
  switch (id) {
    case ST_OFF: p->ui_off = v; break;
    case ST_BRIGHT: p->ui_bright = v; task->applyContrast(); break;
    case ST_DOTS: p->ui_dots = v; break;
    case ST_VIBE: p->vibe_quiet = v ? 0 : 1; break;
    case ST_ENC: p->ui_enc_rev = v; task->applyEncoderPrefs(); break;
    case ST_STEPS: p->ui_enc_steps = v ? 4 : 2; task->applyEncoderPrefs(); break;
    case ST_SKIP: p->ui_skip = v ? 0 : 1; break;
    case ST_NOTIFY: p->ui_notify = v; break;
    case ST_DELCONF: p->ui_delconf = v ? 0 : 1; break;
    case ST_CSORT: p->ui_csort = v; break;
    case ST_ANT: p->ui_ant = v; break;
    case ST_LANG: p->ui_lang = v; break;
    case ST_TZ: p->ui_tz = v + TZ_MIN; break;
    case ST_DST: p->ui_dst = v; break;
  }
}

static void saveOpt(UITask* task, uint8_t id, int v) {
  if (id == ST_TXPOW) {
    the_mesh.setTxPower(v + TXPOW_MIN);   // applied and saved
  } else {
    setOpt(task, id, v);
    the_mesh.savePrefs();
  }
  task->haptic(UI_HAPTIC_ACK_MS);
}

static void doNeverAskAntenna(UITask* task) {
  saveOpt(task, ST_ANT, ANT_NEVER);
  task->pop();   // the value editor
}

// one value: turn to change, press = save, Back = cancel. Brightness shows while turning.
class ValueEditScreen : public UIScreen {
  UITask* _task;
  uint8_t _id = 0;
  int _val = 0, _orig = 0;
public:
  ValueEditScreen(UITask* task) : _task(task) { }
  void open(uint8_t id) { _id = id; _val = _orig = getOpt(_task, id); }
  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s", T(settingName(_id)));
    drawMarquee(d, 0, 0, buf, 21, 0, false);
    d.fillRect(0, 9, d.width(), 1);
    d.setColor(UIColor::primary_txt);
    char val[24];
    optText(_id, _val, val, sizeof(val));
    snprintf(buf, sizeof(buf), "< %s >", val);
    if (utf8Len(buf) <= 10) {
      d.setTextSize(2);
      d.drawTextCentered(d.width() / 2, 22, buf);
      d.setTextSize(1);
    } else {
      d.drawTextCentered(d.width() / 2, 26, buf);
    }
    d.drawTextCentered(d.width() / 2, 44, T(S_EDIT_HINT));
    d.drawTextCentered(d.width() / 2, 54, T(S_BACK_CANCEL));
    return 1000;
  }
  bool handleInput(char c) override {
    int n = optCount(_id);
    if ((c == KEY_NEXT || c == KEY_PREV) && n > 0) {
      int v = _val + (c == KEY_NEXT ? 1 : -1);
      if (n <= 4) v = (v + n) % n;            // short lists wrap around
      else if (v < 0 || v >= n) return true;  // ranges stop at the ends
      _val = v;
      if (_id == ST_BRIGHT) { _task->prefs()->ui_bright = _val; _task->applyContrast(); }   // preview
      return true;
    }
    if (c == KEY_ENTER) {
      if (_id == ST_ANT && _val == ANT_NEVER && _orig != ANT_NEVER) {
        askConfirm(_task, T(S_ANT_NEVER_Q), doNeverAskAntenna);
        return true;
      }
      saveOpt(_task, _id, _val);
      _task->pop();
      return true;
    }
    if (c == KEY_CANCEL && _id == ST_BRIGHT) {
      _task->prefs()->ui_bright = _orig;
      _task->applyContrast();
    }
    return false;   // Back: the stack pops
  }
};

static ValueEditScreen* value_edit;

// read-only device facts
class AboutScreen : public ListScreen {
  char _lines[8][40];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_ABOUT)); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _lines[i]); }
public:
  AboutScreen(UITask* task) : ListScreen(task) { }
  void open() {
    _n = 0;
    snprintf(_lines[_n++], 40, "%s", _task->prefs()->node_name);
    snprintf(_lines[_n++], 40, T(S_VERSION_FMT), FIRMWARE_VERSION);
    snprintf(_lines[_n++], 40, "%s", FIRMWARE_BUILD_DATE);
    const uint8_t* k = the_mesh.self_id.pub_key;
    char hex[16];
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", k[0], k[1], k[2], k[3]);
    snprintf(_lines[_n++], 40, T(S_ID_FMT), hex);
    uint16_t mv = _task->getBattMilliVolts();
    snprintf(_lines[_n++], 40, T(S_BATT_FMT), mv / 1000, (mv % 1000) / 10, battPercent(mv));
    snprintf(_lines[_n++], 40, T(S_CONTACTS_FMT), the_mesh.getNumContacts(), MAX_CONTACTS);
    char age[12];
    formatAge(age, sizeof(age), millis() / 1000);
    snprintf(_lines[_n++], 40, T(S_UPTIME_FMT), age);
    uint32_t now = rtc_clock.getCurrentTime();
    if (now >= VALID_EPOCH) {
      uint32_t local = ui::localTime(now, _task->prefs()->ui_tz, _task->prefs()->ui_dst);
      snprintf(_lines[_n++], 40, T(S_CLOCK_FMT), (unsigned long)((local / 3600) % 24),
               (unsigned long)((local / 60) % 60), the_mesh.isClockApprox() ? " ~" : "");
    } else {
      snprintf(_lines[_n++], 40, "%s", T(S_NO_CLOCK));
    }
    reset();
  }
};

static AboutScreen* about_screen;

static void doRescue(UITask* task) { task->enterRescue(); }

class SettingsGroupScreen : public ListScreen {
  const uint8_t* _items = NULL;
  int _n = 0;
  StrId _title = S_SETTINGS;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(_title)); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override {
    uint8_t id = _items[i];
    if (optCount(id) == 0) {
      if (id == ST_NAME) snprintf(buf, n, "%s: %s", T(S_SET_NAME), _task->prefs()->node_name);
      else snprintf(buf, n, "%s", T(settingName(id)));
      return;
    }
    char val[24];
    optText(id, getOpt(_task, id), val, sizeof(val));
    if (id == ST_TX) snprintf(buf, n, T(S_TX_FMT), val);
    else snprintf(buf, n, "%s: %s", T(settingName(id)), val);
  }
  bool onEnter(int i) override {
    uint8_t id = _items[i];
    int n = optCount(id);
    if (id == ST_TX) {
      if (the_mesh.isTxAllowed()) { the_mesh.setTxAllowed(false); _task->haptic(UI_HAPTIC_ACK_MS); }
      else turnTransmitOn(_task);
    } else if (n == 2) {
      saveOpt(_task, id, 1 - getOpt(_task, id));
    } else if (n > 2) {
      value_edit->open(id);
      _task->push(value_edit);
    } else if (id == ST_QUICK) {
      quick_edit->reset();
      _task->push(quick_edit);
    } else if (id == ST_NAME) {
      compose_screen->openName();
      _task->push(compose_screen);
    } else if (id == ST_ABOUT) {
      about_screen->open();
      _task->push(about_screen);
    } else if (id == ST_RESCUE) {
      askConfirm(_task, T(S_RESCUE_Q), doRescue);
    }
    return true;
  }
public:
  SettingsGroupScreen(UITask* task) : ListScreen(task) { }
  void open(StrId title, const uint8_t* items, int n) { _title = title; _items = items; _n = n; reset(); }
};

static SettingsGroupScreen* settings_group;

static const uint8_t GROUP_DISPLAY[] = { ST_OFF, ST_BRIGHT, ST_DOTS };
static const uint8_t GROUP_INPUT[]   = { ST_VIBE, ST_ENC, ST_STEPS, ST_SKIP };
static const uint8_t GROUP_MSGS[]    = { ST_NOTIFY, ST_DELCONF, ST_CSORT, ST_QUICK };
static const uint8_t GROUP_RADIO[]   = { ST_TX, ST_TXPOW, ST_ANT };
static const uint8_t GROUP_SYSTEM[]  = { ST_NAME, ST_LANG, ST_TZ, ST_DST, ST_ABOUT, ST_RESCUE };

class SettingsScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_SETTINGS)); }
  int count() override { return 5; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", T((StrId)(S_GRP_DISPLAY + i))); }
  bool onEnter(int i) override {
    switch (i) {
      case 0: settings_group->open(S_GRP_DISPLAY, GROUP_DISPLAY, sizeof(GROUP_DISPLAY)); break;
      case 1: settings_group->open(S_GRP_INPUT, GROUP_INPUT, sizeof(GROUP_INPUT)); break;
      case 2: settings_group->open(S_GRP_MSGS, GROUP_MSGS, sizeof(GROUP_MSGS)); break;
      case 3: settings_group->open(S_GRP_RADIO, GROUP_RADIO, sizeof(GROUP_RADIO)); break;
      default: settings_group->open(S_GRP_SYSTEM, GROUP_SYSTEM, sizeof(GROUP_SYSTEM)); break;
    }
    _task->push(settings_group);
    return true;
  }
public:
  SettingsScreen(UITask* task) : ListScreen(task) { }
};

static SettingsScreen* settings_screen;

// ---------------------------------------------------------------- menus

class MainMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_MENU)); }
  int count() override { return 4; }
  void label(int i, char* buf, size_t n) override {
    if (i == 0) {
      int u = _task->unreadCount();
      if (u > 0) snprintf(buf, n, T(S_MSGS_N), u);
      else snprintf(buf, n, "%s", T(S_MSGS));
    } else {
      static const StrId L[] = { S_CONTACTS, S_CHANNELS, S_SETTINGS };
      snprintf(buf, n, "%s", T(L[i - 1]));
    }
  }
  bool onEnter(int i) override {
    if (i == 0) { conversations->reset(); _task->push(conversations); }
    else if (i == 1) { contacts_screen->open(); _task->push(contacts_screen); }
    else if (i == 2) { channels_screen->open(); _task->push(channels_screen); }
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
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_QUICK)); }
  int count() override { return COUNT; }
  void label(int i, char* buf, size_t n) override {
    switch (i) {
      case ADVERT: snprintf(buf, n, "%s", T(S_SEND_ADV)); break;
      case TRANSMIT: snprintf(buf, n, T(S_TX_FMT), the_mesh.isTxAllowed() ? T(S_ON) : T(S_OFF)); break;
      case BLUETOOTH:
        // the pairing PIN is shown while Bluetooth waits for a phone
        if (_task->isBluetoothEnabled() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
          snprintf(buf, n, "BT: %s PIN %06lu", T(S_ON), (unsigned long)the_mesh.getBLEPin());
        } else {
          snprintf(buf, n, "Bluetooth: %s", _task->isBluetoothEnabled() ? T(S_ON) : T(S_OFF));
        }
        break;
#if ENV_INCLUDE_GPS == 1
      case GPS: snprintf(buf, n, "GPS: %s", _task->getGPSState() ? T(S_ON) : T(S_OFF)); break;
#endif
      default: snprintf(buf, n, "%s", T(S_HIBERNATE)); break;
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
          turnTransmitOn(_task);
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
        askConfirm(_task, T(S_HIB_Q), doHibernate);
        break;
    }
    return true;
  }
public:
  QuickMenuScreen(UITask* task) : ListScreen(task) { }
};

static MainMenuScreen* main_menu;
static QuickMenuScreen* quick_menu;

// ---------------------------------------------------------------- standby: status bar + read-only status cards

// last packet heard, for the signal bars
static uint32_t rx_count_seen = 0;
static unsigned long rx_at = 0;
static float rx_snr = 0;
static bool rx_any = false;

static void trackReception() {
  uint32_t n = radio_driver.getPacketsRecv();
  if (n != rx_count_seen) {
    rx_count_seen = n;
    rx_at = millis();
    rx_snr = radio_driver.getLastSNR();
    rx_any = true;
  }
}

static bool isCharging() { return board.isExternalPowered(); }

class StandbyScreen : public UIScreen {
  UITask* _task;
  NodePrefs* _prefs;
  uint8_t _card = 0;
  unsigned long _since = 0;
  AdvertPath _recent[3];
  enum { SUMMARY, RECENT, RADIO, BATTERY,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
    CARDS };

  bool renderHeader(DisplayDriver& d, int w) {
    d.setTextSize(1);
    d.setColor(UIColor::title_txt);
    int x = w - 15;
    drawBattery(d, x, 1, battPercent(_task->getBattMilliVolts()));
    if (isCharging()) { x -= 7; d.drawXbm(x, 0, icon_bolt, 8, 8); }
    if (_task->isBluetoothEnabled()) { x -= 9; d.drawXbm(x, 1, icon_bt, 8, 8); }
    if (_task->unreadCount() > 0) { x -= 10; d.drawXbm(x, 1, icon_mail, 8, 8); }
    if (!the_mesh.isTxAllowed()) {   // crossed-out "TX": receive only
      x -= 14;
      d.setCursor(x, 1);
      d.print("TX");
      d.fillRect(x - 1, 4, 13, 1);
    }
    if (rx_any && millis() - rx_at < SIGNAL_STALE_MILLIS) { x -= 10; drawSignal(d, x, 0, rx_snr); }
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

  void cardTitle(DisplayDriver& d, const uint8_t* icon, const char* text) {
    d.drawXbm(0, 13, icon, 8, 8);
    d.setCursor(11, 13);
    d.print(text);
  }

  bool renderSummary(DisplayDriver& d, int w) {
    bool scrolling = false;
    char buf[200];
    int unread = _task->unreadCount();
    int max_chars = w / 6;

    // row A, size 2: unread count and clock ("~" = time taken from an advert)
    uint32_t now = rtc_clock.getCurrentTime();
    if (now >= VALID_EPOCH) {
      uint32_t local = ui::localTime(now, _prefs->ui_tz, _prefs->ui_dst);
      snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)((local / 3600) % 24), (unsigned long)((local / 60) % 60));
    } else {
      snprintf(buf, sizeof(buf), "--:--");
    }
    d.setTextSize(2);
    d.drawTextRightAlign(w - 1, 15, buf);
    d.setTextSize(1);
    if (now >= VALID_EPOCH && the_mesh.isClockApprox()) d.drawTextRightAlign(w - 62, 15, "~");
    if (unread > 0) {
      d.drawXbm(0, 19, icon_mail, 8, 8);
      snprintf(buf, sizeof(buf), unread > 99 ? "99+" : "%d", unread);
      d.setTextSize(2);
      d.setCursor(11, 15);
      d.print(buf);
      d.setTextSize(1);
    }

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
      if (_task->hasConnection()) snprintf(buf, sizeof(buf), "%s", T(S_PHONE));
      else if (!_task->isBluetoothEnabled()) snprintf(buf, sizeof(buf), "Bluetooth: %s", T(S_OFF));
      else if (the_mesh.getBLEPin() != 0) snprintf(buf, sizeof(buf), "BT PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
      else snprintf(buf, sizeof(buf), "Bluetooth: %s", T(S_ON));
      drawMarquee(d, 1, 36, buf, max_chars, 0, false);
      int n = the_mesh.getRecentlyHeard(_recent, 1);
      if (n > 0 && _recent[0].name[0]) {
        char age[12];
        ageOf(age, sizeof(age), _recent[0].recv_timestamp);
        snprintf(buf, sizeof(buf), T(S_HEARD_FMT), _recent[0].name, age);
        if (drawMarquee(d, 1, 47, buf, max_chars, _since, true)) scrolling = true;
      }
    }
    return scrolling;
  }

  void renderRecent(DisplayDriver& d, int w) {
    cardTitle(d, icon_person, T(S_CARD_RECENT));
    int n = the_mesh.getRecentlyHeard(_recent, 3);
    char age[12];
    int y = 24;
    for (int i = 0; i < n; i++) {
      if (_recent[i].name[0] == 0) continue;
      ageOf(age, sizeof(age), _recent[i].recv_timestamp);
      int age_w = d.getTextWidth(age);
      drawMarquee(d, 0, y, _recent[i].name, (w - age_w - 4) / 6, 0, false);
      d.drawTextRightAlign(w - 1, y, age);
      y += 10;
    }
    if (y == 24) d.drawTextCentered(w / 2, 34, T(S_NOBODY));
  }

  void renderRadio(DisplayDriver& d, int w) {
    char buf[40];
    cardTitle(d, icon_radio, T(S_CARD_RADIO));
    d.setCursor(0, 24); snprintf(buf, sizeof(buf), "%.3f SF%d CR%d", _prefs->freq, _prefs->sf, _prefs->cr); d.print(buf);
    d.setCursor(0, 34); snprintf(buf, sizeof(buf), "BW %.1f  TX %ddBm", _prefs->bw, _prefs->tx_power_dbm); d.print(buf);
    snprintf(buf, sizeof(buf), T(S_NOISE_FMT), radio_driver.getNoiseFloor());
    d.setCursor(0, 44); d.print(buf);
    if (rx_any) {
      snprintf(buf, sizeof(buf), "SNR %.1f", rx_snr);
      d.drawTextRightAlign(w - 1, 44, buf);
    }
  }

  void renderBattery(DisplayDriver& d, int w) {
    char buf[40];
    cardTitle(d, icon_batt, T(S_CARD_BATT));
    uint16_t mv = _task->getBattMilliVolts();
    snprintf(buf, sizeof(buf), "%d.%02d V", mv / 1000, (mv % 1000) / 10);
    d.setTextSize(2);
    d.setCursor(0, 24);
    d.print(buf);
    snprintf(buf, sizeof(buf), "%d%%", battPercent(mv));
    d.drawTextRightAlign(w - 1, 24, buf);
    d.setTextSize(1);
    d.setCursor(0, 44);
    d.print(isCharging() ? T(S_CHARGING) : T(S_ON_BATT));
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
    if (_prefs->ui_dots != DOTS_BOTTOM) {
      snprintf(buf, sizeof(buf), T(S_OFF_AT_FMT), AUTO_SHUTDOWN_MILLIVOLTS / 1000, (AUTO_SHUTDOWN_MILLIVOLTS % 1000) / 10);
      d.setCursor(0, 54);
      d.print(buf);
    }
#endif
  }

#if ENV_INCLUDE_GPS == 1
  void renderGPS(DisplayDriver& d, int w) {
    char buf[40];
    cardTitle(d, icon_pin, _task->getGPSState() ? T(S_GPS_ON) : T(S_GPS_OFF));
    LocationProvider* nmea = sensors.getLocationProvider();
    if (nmea == NULL) {
      d.setCursor(0, 24);
      d.print(T(S_NO_GPS));
      return;
    }
    snprintf(buf, sizeof(buf), T(S_SAT_FMT), nmea->isValid() ? T(S_FIX) : T(S_NOFIX), (int)nmea->satellitesCount());
    d.drawTextRightAlign(w - 1, 13, buf);
    d.setCursor(0, 24); snprintf(buf, sizeof(buf), "%.5f %.5f", nmea->getLatitude() / 1000000., nmea->getLongitude() / 1000000.); d.print(buf);
    d.setCursor(0, 34); snprintf(buf, sizeof(buf), T(S_ALT_FMT), nmea->getAltitude() / 1000.); d.print(buf);
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
      case RADIO: renderRadio(d, w); break;
      case BATTERY: renderBattery(d, w); break;
#if ENV_INCLUDE_GPS == 1
      case GPS: renderGPS(d, w); break;
#endif
    }
    renderDots(d);
    // nothing moves: the clock shows minutes, so a slow refresh is enough (saves I2C traffic)
    return scrolling ? MARQUEE_STEP_MILLIS : 10000;
  }

  bool handleInput(char c) override;
};

static StandbyScreen* standby;

// press on standby: the newest unread message when there is one, otherwise the main menu.
// The screens under it (menu, conversations, thread) make Back walk up naturally.
bool StandbyScreen::handleInput(char c) {
  if (c == KEY_NEXT) { _card = (_card + 1) % CARDS; _since = millis(); return true; }
  if (c == KEY_PREV) { _card = (_card + CARDS - 1) % CARDS; _since = millis(); return true; }
  if (c == KEY_ENTER) {
    main_menu->reset();
    _task->push(main_menu);
    for (int i = 0; i < _task->historyCount(); i++) {
      UIMsgEntry* m = _task->historyAt(i);
      if (!m->unread) continue;
      conversations->reset();
      _task->push(conversations);
      thread_screen->open(m->seq);
      _task->push(thread_screen);
      message_view->open(m->seq);
      _task->push(message_view);
      break;
    }
    return true;
  }
  if (c == KEY_CONTEXT_MENU) { quick_menu->reset(); _task->push(quick_menu); return true; }
  return false;   // Back: UITask turns the screen off
}

// ---------------------------------------------------------------- UITask

static bool in_rescue = false;

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  lang_prefs = node_prefs;
  _auto_off = millis() + autoOffMillis();
  in_rescue = false;
  rx_count_seen = 0;
  rx_any = false;

  user_btn.begin();
  user_btn.setDebounce(15);
  encoder_btn.begin();
  encoder_btn.setDebounce(15);
  rotary_input.begin();
  applyEncoderPrefs();

  if (_display != NULL) {
    _display->turnOn();
    applyContrast();
  }

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
  rescue_screen = new RescueScreen(this);
  standby = new StandbyScreen(this, node_prefs);
  main_menu = new MainMenuScreen(this);
  quick_menu = new QuickMenuScreen(this);
  conversations = new ConversationsScreen(this);
  conv_actions = new ConvActionsScreen(this);
  thread_screen = new ThreadScreen(this);
  message_view = new MessageViewScreen(this);
  message_actions = new MessageActionsScreen(this);
  contacts_screen = new ContactsScreen(this);
  contact_actions = new ContactActionsScreen(this);
  contact_info = new ContactInfoScreen(this);
  channels_screen = new ChannelsScreen(this);
  settings_screen = new SettingsScreen(this);
  settings_group = new SettingsGroupScreen(this);
  value_edit = new ValueEditScreen(this);
  about_screen = new AboutScreen(this);
  confirm_screen = new ConfirmScreen(this);
  antenna_screen = new AntennaScreen(this);
  compose_screen = new ComposeScreen(this);
  compose_menu = new ComposeMenuScreen(this);
  reply_pick = new ReplyPickScreen(this);
  send_undo = new SendUndoScreen(this);
  quick_edit = new QuickEditScreen(this);
#ifdef UI_ASK_ANTENNA
  {
    uint8_t mode = _node_prefs->ui_ant % 3;
    bool ask = _display != NULL;   // no OLED found: nobody could answer, keep the radio usable
    if (mode == ANT_NEVER || (mode == ANT_POWER_LOSS && !coldBoot())) ask = false;
    if (!ask) the_mesh.setTxAllowed(true);
  }
  _ask_antenna = !the_mesh.isTxAllowed();
#endif
  loadQuickReplies();
  _depth = 0;   // splash until home()
  _next_refresh = 100;
}

#define QUICK_FILE "/ui_quick"

static char quick_buf[UI_QUICK_COUNT * UI_QUICK_LEN];   // file image; static to keep 640 bytes off the loop stack

void UITask::loadQuickReplies() {
  char* buf = quick_buf;
  int n = the_mesh.loadUIFile(QUICK_FILE, (uint8_t*)buf, sizeof(quick_buf) - 1);
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
  char* buf = quick_buf;
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

void UITask::applyContrast() {
  if (_display != NULL) _display->setContrast(CONTRAST_TABLE[_node_prefs->ui_bright % 3]);
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
  if (in_rescue) return;   // stays until restart
  _stack[0] = standby;
  _depth = 1;
  standby->showSummary();
  if (_ask_antenna) _stack[_depth++] = antenna_screen;   // until answered, also after screen off
  _next_refresh = 0;
}

void UITask::enterRescue() {
  the_mesh.enterCLIRescue();
  in_rescue = true;
  _stack[0] = rescue_screen;
  _depth = 1;
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
  if (millis_on > 0 && !_node_prefs->vibe_quiet) vibration.pulse(millis_on);
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

// message alerts by Settings > Messages > Alerts: only without a phone (default), always, never.
// The screen itself wakes only when no phone is connected (newMsg).
void UITask::notifyMsg(UIEventType t, bool connected) {
  uint8_t mode = _node_prefs->ui_notify % 3;
  if (mode == NOTIFY_NEVER) return;
  if (mode == NOTIFY_NO_PHONE && connected) return;
  notify(t);
}

void UITask::ackReceived(const uint8_t* data) {
  uint32_t ack;
  memcpy(&ack, data, sizeof(ack));
  for (int i = 0; i < _msg_count; i++) {
    UIMsgEntry* m = historyAt(i);
    if (m->ack == ack && (m->state == MSG_OUT_PENDING || m->state == MSG_OUT_FAILED)) {
      m->state = MSG_OUT_ACKED;   // a late ACK still counts
      haptic(UI_HAPTIC_DELIVERED);
      _next_refresh = 0;
      return;
    }
  }
}

void UITask::checkAckTimeouts() {
  for (int i = 0; i < _msg_count; i++) {
    UIMsgEntry* m = historyAt(i);
    if (m->state == MSG_OUT_PENDING && timeReached(m->ack_deadline)) {
      m->state = MSG_OUT_FAILED;
      if (_display != NULL && _display->isOn()) {
        char alert[64];
        snprintf(alert, sizeof(alert), T(S_NOT_DELIVERED_FMT), m->from);
        showAlert(alert, 2000);
      }
      _next_refresh = 0;
    }
  }
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

int UITask::historyIndexOf(uint32_t seq) {
  for (int i = 0; i < _msg_count; i++) {
    if (historyAt(i)->seq == seq) return i;
  }
  return -1;
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

UIMsgEntry* UITask::addHistory() {
  _msg_head = (_msg_head + 1) % UI_MSG_HISTORY;
  if (_msg_count < UI_MSG_HISTORY) _msg_count++;
  UIMsgEntry* m = &_msgs[_msg_head];
  memset(m, 0, sizeof(*m));
  m->timestamp = rtc_clock.getCurrentTime();
  m->seq = ++_msg_seq;
  return m;
}

UIMsgEntry* UITask::addSent(const char* conv_name, uint8_t src, uint8_t channel_idx, const uint8_t* pub_prefix,
                            const char* text, uint32_t ack, uint32_t timeout_millis) {
  UIMsgEntry* m = addHistory();
  m->state = ack ? MSG_OUT_PENDING : MSG_OUT_SENT;
  StrHelper::strncpy(m->from, conv_name, sizeof(m->from));
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  m->src = src;
  m->channel_idx = channel_idx;
  if (pub_prefix) memcpy(m->pub_prefix, pub_prefix, sizeof(m->pub_prefix));
  m->ack = ack;
  m->ack_deadline = millis() + timeout_millis + 2000;   // the estimate plus a margin for the way back
  _next_refresh = 0;
  return m;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  UIMsgEntry* m = addHistory();
  m->path_len = path_len;
  m->unread = true;
  m->state = MSG_IN;
  StrHelper::strncpy(m->from, from_name, sizeof(m->from));
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  m->src = _next_src;
  m->channel_idx = _next_channel;
  memcpy(m->pub_prefix, _next_prefix, sizeof(m->pub_prefix));
  _next_src = MSG_SRC_UNKNOWN;
  _last_new_msg = millis();

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      wakeDisplay();
    }
    if (_display->isOn() && current() != standby) {
      char alert[64];
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
    _display->drawTextCentered(_display->width() / 2, 4, T(S_POWER_OFF));
    _display->setColor(UIColor::primary_txt);
    _display->drawTextCentered(_display->width() / 2, 22, T(S_BATT_OUT1));
    _display->drawTextCentered(_display->width() / 2, 32, T(S_BATT_OUT2));
    // how to switch on again: hold the encoder (Back held at power-up starts the bootloader's DFU)
    int w = _display->getTextWidth(T(S_WAKE_HINT)) + 10;
    int x = (_display->width() - w) / 2;
    _display->setCursor(x, 50);
    _display->print(T(S_WAKE_HINT));
    _display->drawXbm(x + w - 8, 50, icon_knob, 8, 8);
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
  while (millis() - shown < 3000) delay(10);   // long enough to read
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
  the_mesh.flushPendingWrites();   // contacts/paths learned in the last moments
  if (restart) _board->reboot();
  else _board->powerOff();
}

#ifndef UI_BATT_MIN_PLAUSIBLE_MV
  #define UI_BATT_MIN_PLAUSIBLE_MV  2500
#endif

#ifndef UI_RESUME_MILLIS
  #define UI_RESUME_MILLIS  (5 * 60 * 1000UL)   // screen off for less than this: wake where you left off
#endif

void UITask::wakeDisplay() {
  _display->turnOn();
  applyContrast();
  // writing or reading something when the screen timed out: keep it, otherwise standby
  if (_depth <= 1 || current() == _splash || millis() - _off_at > UI_RESUME_MILLIS) home();
  _auto_off = millis() + autoOffMillis();
  _next_refresh = 0;
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      wakeDisplay();   // wake only, consume the key
      // turns made while the screen was off must not act on the woken screen
      while (rotary_input.poll() != RotaryInputEvent::None) { }
      c = 0;
    }
    _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }
  return c;
}

// centered box, up to two lines
void UITask::renderAlert() {
  _display->setTextSize(1);
  int y = 14, h = 34, p = 2;
  _display->setColor(UIColor::popup_bkg);
  _display->fillRect(p, y, _display->width() - p * 2, h);
  _display->setColor(UIColor::popup_txt);
  _display->drawRect(p, y, _display->width() - p * 2, h);
  drawTwoLines(*_display, y + 7, _alert, 20);
}

// low battery: one warning below 10% (again after charging above 20%), power off below the limit
void UITask::checkBattery() {
  uint16_t mv = getBattMilliVolts();
  // below ~2.5 V no LiPo-powered nRF52 is still running: such a reading is a broken divider or
  // ADC, and acting on it would switch the device off right after every boot
  if (mv < UI_BATT_MIN_PLAUSIBLE_MV) return;
  bool external = board.isExternalPowered();
  int pct = battPercent(mv);
  if (external || pct >= LOW_BATT_REARM) _low_batt_warned = false;
  if (!external && pct < LOW_BATT_PERCENT && !_low_batt_warned) {
    _low_batt_warned = true;
    haptic(UI_HAPTIC_LONG_MS);
    if (_display != NULL && _display->isOn()) showAlert(T(S_BATT_LOW), 3000);
  }
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (mv < AUTO_SHUTDOWN_MILLIVOLTS && !external) {
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
#endif
}

void UITask::loop() {
  char c = 0;
  int repeat = 1;

  // encoder push: click = Enter, hold = context / quick menu
  int ev = encoder_btn.check();
  if (ev == BUTTON_EVENT_CLICK) c = checkDisplayOn(KEY_ENTER);
  else if (ev == BUTTON_EVENT_LONG_PRESS) c = checkDisplayOn(KEY_CONTEXT_MENU);

  // Back: click = up one level, hold = standby; held while the splash shows = CLI rescue
  if (c == 0) {
    ev = user_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_CANCEL);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      if (current() == _splash) enterRescue();
      else c = checkDisplayOn(KEY_HOME);
    }
  }

  // rotation: all detents queued since the last loop, faster turns move further
  if (c == 0) {
    int net = 0;
    for (int guard = 0; guard < 16; guard++) {
      RotaryInputEvent r = rotary_input.poll();
      if (r == RotaryInputEvent::None) break;
      net += (r == RotaryInputEvent::Next) ? 1 : -1;
    }
    if (net != 0) {
      int8_t dir = net > 0 ? 1 : -1;
      int n = net > 0 ? net : -net;
      unsigned long now = millis();
      unsigned long per_detent = (now - _last_turn) / n;
      int steps = ui::accelSteps(per_detent, dir == _last_dir);
      _last_turn = now;
      _last_dir = dir;
      c = checkDisplayOn(dir > 0 ? KEY_NEXT : KEY_PREV);
      repeat = n * steps;
      if (c != 0) haptic(UI_HAPTIC_TICK_MS);
    }
  } else {
    haptic(UI_HAPTIC_CLICK_MS);
  }

  if (c != 0) {
    if (c == KEY_HOME) {
      home();
    } else {
      for (int k = 0; k < repeat; k++) {
        UIScreen* s = current();
        bool handled = s && s->handleInput(c);
        if (!handled && c == KEY_CANCEL) {
          if (_depth > 1) pop();
          else if (_display != NULL && !in_rescue) _display->turnOff();   // Back on standby = screen off
        }
        if (current() != s) break;   // a step opened or closed a screen: the rest is not for it
      }
    }
    if (_display != NULL && _display->isOn()) _auto_off = millis() + autoOffMillis();
    _next_refresh = 0;
  }

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) buzzer.loop();
#endif

  trackReception();
  checkAckTimeouts();

  UIScreen* s = current();
  if (s) s->poll();

  if (_display != NULL && _display->isOn()) {
    if (_alert_expiry != 0 && timeReached(_alert_expiry)) _alert_expiry = 0;   // expired: forget it
    if ((_next_refresh == 0 || timeReached(_next_refresh)) && current()) {
      _display->startFrame();
      int delay_millis = current()->render(*_display);
      if (_alert_expiry != 0 && !timeReached(_alert_expiry)) {
        renderAlert();
        unsigned long alert_left = _alert_expiry - millis();
        if ((unsigned long)delay_millis > alert_left) delay_millis = alert_left;
      }
      _next_refresh = millis() + delay_millis;
      _display->endFrame();
    }
    if (timeReached(_auto_off) && !in_rescue) {
      _display->turnOff();
      _off_at = millis();
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  if (next_batt_chck == 0 || timeReached(next_batt_chck)) {
    checkBattery();
    next_batt_chck = millis() + 8000;
  }
}
