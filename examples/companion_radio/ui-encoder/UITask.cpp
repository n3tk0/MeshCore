#include "UITask.h"
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <helpers/ui/TextFonts.h>
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
#define ANT_REMIND_MILLIS    (5 * 60 * 1000UL)   // unanswered antenna question: a buzz this often

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
static const uint16_t HAPTIC_FAILED[]  = { 300, 150, 300 };   // a sent message was not delivered
static const uint16_t HAPTIC_REMIND[]  = { 80, 120, 80 };     // the antenna question still waits

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
static const uint8_t icon_back[]    = { 0x10, 0x30, 0x7F, 0xFF, 0x7F, 0x30, 0x10, 0x00 };
static const uint8_t icon_mute[]    = { 0x10, 0x35, 0xF2, 0xF5, 0xF0, 0x30, 0x10, 0x00 };
static const uint8_t icon_up[]     = { 0x10, 0x38, 0x7C, 0xFE, 0x38, 0x38, 0x38, 0x00 };   // they hear us
static const uint8_t icon_down[]   = { 0x38, 0x38, 0x38, 0xFE, 0x7C, 0x38, 0x10, 0x00 };   // we hear them
static const uint8_t icon_range[]  = { 0x00, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x00 };   // range test runs
static const uint8_t icon_tower[]   = { 0x10, 0x38, 0x10, 0x28, 0x28, 0x44, 0x44, 0x00 };

static const unsigned long AUTO_OFF_TABLE[] = { 15000, 30000, 60000, 300000 };
// brightness levels 1..13. The first NIGHT_LEVELS also lower the panel drive (DisplayDriver::setDim),
// which goes far below what contrast alone reaches; the rest are SSD1306 contrast with steps that
// grow because the eye barely sees the top ones. Levels 6, 10 and 13 are the old low / medium / high.
struct BrightStep { uint8_t contrast, dim; };
static const BrightStep BRIGHT_TABLE[] = {
  { 0x00, 2 }, { 0x30, 2 }, { 0x00, 1 },                                  // night
  { 0x01, 0 }, { 0x03, 0 }, { 0x08, 0 }, { 0x12, 0 }, { 0x24, 0 },
  { 0x40, 0 }, { 0x60, 0 }, { 0x8C, 0 }, { 0xC0, 0 }, { 0xFF, 0 },
};
#define BRIGHT_LEVELS ((int)(sizeof(BRIGHT_TABLE) / sizeof(BRIGHT_TABLE[0])))
#define NIGHT_LEVELS  3
#define DOTS_BOTTOM 0
#define DOTS_RIGHT  1
#define DOTS_NONE   2

#define NOTIFY_NO_PHONE 0
#define NOTIFY_ALWAYS   1
#define NOTIFY_NEVER    2

#define ANT_POWER_LOSS  0   // ask after a battery was put in (the default)
#define ANT_ALWAYS      1
#define ANT_NEVER       2

#define WAKE_DIRECT     0   // the screen turns on for direct messages and mentions
#define WAKE_ALL        1
#define WAKE_NEVER      2

#define LOCK_HAND       0
#define LOCK_AUTO       1

// ---------------------------------------------------------------- reset reason (antenna question)

// "after power loss": ask about the antenna only on a cold start (battery put in), not when waking
// from hibernate or after a reset. The board reads RESETREAS once at start-up (0 = power-on);
// boards that cannot tell report 0, so they are asked every time.
#ifdef UI_SIM
  static bool coldBoot() { return true; }
#else
  static bool coldBoot() { return board.getResetReason() == 0; }
#endif

// ---------------------------------------------------------------- UI language (0 = BG, 1 = EN)

#define LANG_COUNT 2
static const char* LANG_NAMES[LANG_COUNT] = { "BG", "EN" };
static NodePrefs* lang_prefs = NULL;

enum StrId {
  S_AGE_S, S_AGE_M, S_AGE_H, S_AGE_D, S_EMPTY, S_YES, S_NO, S_ON, S_OFF,
  // messages
  S_MSGS, S_MSGS_N, S_NO_MSGS, S_ME, S_REPLY, S_RETRY, S_DEL, S_DEL_CONV, S_DEL_ALL, S_MARK_READ,
  S_DEL_Q, S_DEL_CONV_Q, S_DEL_ALL_Q, S_DELETED, S_ALL_DELETED, S_READ, S_NEW_FMT,
  S_NOT_DELIVERED_FMT,
  // writing
  S_WRITE, S_SEND, S_CLEAR, S_DISCARD, S_BACK_CANCEL, S_CANCELLED, S_SENT, S_SEND_FAIL,
  S_NO_TARGET, S_QUICK_MSGS, S_QUICK_N_FMT, S_EMPTY_SLOT, S_SAVE, S_SAVED, S_SAVE_FAIL,
  S_NAME_TITLE,
  // contacts / channels
  S_CONTACTS, S_CHANNELS, S_NO_CONTACTS, S_NO_CHANNELS, S_INFO, S_RESET_PATH, S_PATH_RESET,
  S_TYPE_CHAT, S_TYPE_RPT, S_TYPE_ROOM, S_TYPE_SENSOR, S_HOPS_FMT, S_DIRECT, S_FLOOD,
  S_HEARD_AGO_FMT, S_ID_FMT,
  // menus
  S_MENU, S_QUICK, S_SEND_ADV, S_SEND_ADV_ALL, S_ADV_SENT, S_ADV_FAIL, S_HIBERNATE, S_HIB_Q,
  // standby
  S_PHONE, S_HEARD_FMT, S_NOBODY, S_NOISE_FMT, S_NO_GPS, S_FIX, S_NOFIX, S_SAT_FMT, S_ALT_FMT,
  S_POWER_OFF, S_BATT_EMPTY, S_BATT_LOW, S_BATT_OUT, S_CHARGE, S_WAKE_HINT, S_GPS_ON, S_GPS_OFF,
  S_LOCKED, S_UNLOCKED, S_NAME_SAVED, S_NAME_ADV_Q,
  // antenna / transmit
  S_ANT_Q, S_TX_FMT, S_TX_IS_OFF, S_ANT_NEVER_Q,
  // settings
  S_SETTINGS, S_GRP_DISPLAY, S_GRP_INPUT, S_GRP_MSGS, S_GRP_RADIO, S_GRP_SYSTEM, S_SET_OFF,
  S_SET_BRIGHT, S_SET_DOTS, S_SET_MENU_SZ, S_SET_TEXT_SZ, S_SET_VIBE, S_SET_ENC, S_SET_STEPS, S_SET_SKIP, S_SET_NOTIFY,
  S_SET_DELCONF, S_SET_CSORT, S_SET_WAKE, S_SET_LOCK, S_SET_TXPOW, S_SET_ANT, S_SET_PRESET, S_SET_BATT, S_SET_NAME, S_SET_LANG, S_SET_TZ, S_SET_DST,
  S_ABOUT, S_RESCUE, S_RESCUE_Q, S_SET_DFU_USB, S_SET_DFU_BLE, S_DFU_USB_Q, S_DFU_BLE_Q, S_DFU_USB_L, S_DFU_BLE_L, S_DFU_FAIL, S_EDIT_HINT, S_OFF_15S, S_OFF_30S, S_OFF_1M, S_OFF_5M, S_NIGHT_FMT,
  S_DOTS_BOTTOM, S_DOTS_RIGHT, S_DOTS_NONE, S_SIZE_LARGE, S_SIZE_MEDIUM, S_SIZE_SMALL, S_SIZE_TINY, S_ENC_NORMAL, S_ENC_REVERSED,
  S_STEPS_FMT, S_NOTIFY_NO_PHONE, S_NOTIFY_ALWAYS, S_NOTIFY_NEVER, S_CSORT_NAME, S_CSORT_RECENT,
  S_ANT_POWER_LOSS, S_ANT_ALWAYS, S_ANT_NEVER, S_WAKE_DIRECT, S_WAKE_ALL, S_WAKE_NEVER, S_LOCK_HAND,
  S_LOCK_AUTO, S_PRESET_CUSTOM, S_DST_EU, S_VERSION_FMT, S_BATT_FMT, S_CONTACTS_FMT,
  S_UPTIME_FMT, S_CLOCK_FMT, S_NO_CLOCK, S_RESCUE_L1, S_RESCUE_L2, S_RESCUE_L3,
  // tools
  S_NETWORK, S_NEARBY, S_AIRTIME, S_RANGE, S_TRACE, S_SEARCHING, S_NO_ANSWER, S_TRACE_NONE, S_TRACE_NO_PATH, S_MS_FMT, S_START, S_STOP, S_EVERY_FMT, S_OK_OF_FMT, S_PICK_TARGET, S_NO_TARGETS, S_AIR_HOUR_FMT, S_RX_FMT, S_TXN_FMT, S_ERR_FMT, S_RSSI_FMT, S_SNR_FMT, S_AIR_TOTAL_FMT, S_ATTEMPT_FMT, S_DELIVERED, S_NOT_DELIVERED, S_WAITING, S_NEW_CHANNEL, S_CH_TITLE, S_CH_ADDED, S_CH_EXISTS, S_CH_FULL, S_CH_BAD, S_MUTE, S_UNMUTE, S_MUTED, S_UNMUTED, S_DEL_X_Q, S_HARDWARE, S_ENCODER, S_TURNS_FMT, S_PRESSES_FMT, S_HOLDS_FMT, S_RAW_FMT, S_ADC_FMT, S_USB_FMT, S_RESET_FMT, S_WORDS, S_WORDS_Q_FMT, S_WORDS_NONE, S_TODAY, S_TURN_HINT, S_TRACE_WAIT_FMT,
  S_LET_GO_1, S_LET_GO_2, S_BUTTONS, S_PRESS_HINT, S_CHOOSE,
  S_COUNT
};

// Copy rules: the UI uses the 2x font, so a row holds 10 characters (8 next to an icon). Menu
// items and questions are written to fit that; longer content (names, messages) scrolls when selected.
static const char* const STRINGS[S_COUNT][2] = {
  { "%luс", "%lus" },
  { "%luм", "%lum" },
  { "%luч", "%luh" },
  { "%luд", "%lud" },
  { "Празно", "Empty" },
  { "Да", "Yes" },
  { "Не", "No" },
  { "вкл", "on" },
  { "изкл", "off" },
  // messages
  { "Съобщения", "Messages" },
  { "%d нови", "%d new" },
  { "Няма съобщения", "No messages" },
  { "Аз", "Me" },
  { "Отговори", "Reply" },
  { "Повтори", "Resend" },
  { "Изтрий", "Delete" },
  { "Изтрий чат", "Clear chat" },
  { "Изчисти", "Clear all" },
  { "Всички прочетени", "Mark all read" },
  { "Изтрий това?", "Delete this?" },
  { "Изтрий чата с %s?", "Clear chat with %s?" },
  { "Изтрий всичко?", "Delete all?" },
  { "Изтрито", "Deleted" },
  { "Всичко изтрито", "All deleted" },
  { "Прочетени", "Marked read" },
  { "Ново: %s", "New: %s" },
  { "Не е доставено: %s", "Not delivered: %s" },
  // writing
  { "Напиши...", "Write..." },
  { "Изпрати", "Send" },
  { "Изчисти", "Clear" },
  { "Откажи", "Discard" },
  { "Отказ", "Cancel" },
  { "Отказано", "Cancelled" },
  { "Изпратено", "Sent" },
  { "Не е изпратено", "Send failed" },
  { "Няма такъв получател", "Unknown recipient" },
  { "Шаблони", "Templates" },
  { "Шаблон %d", "Quick %d" },
  { "(празно)", "(empty)" },
  { "Запази", "Save" },
  { "Запазено", "Saved" },
  { "Не е запазено", "Save failed" },
  { "Име", "Name" },
  // contacts / channels
  { "Контакти", "Contacts" },
  { "Канали", "Channels" },
  { "Няма контакти", "No contacts" },
  { "Няма канали", "No channels" },
  { "Инфо", "Details" },
  { "Нов път", "Reset path" },
  { "Пътят е нулиран", "Path reset" },
  { "чат", "chat" },
  { "репийтър", "repeater" },
  { "стая", "room" },
  { "сензор", "sensor" },
  { "%d хопа", "%d hops" },
  { "Директно", "Direct" },
  { "Flood", "Flood" },
  { "Чут %s", "Heard %s" },
  { "#%s", "#%s" },
  // menus
  { "Меню", "Menu" },
  { "Бързо меню", "Quick menu" },
  { "Адв. близо", "Adv. near" },   // 10 characters: fits the large font
  { "Адв. мрежа", "Adv. all" },
  { "Advert изпратен", "Advert sent" },
  { "Advert не е изпратен", "Advert failed" },
  { "Изключи", "Power off" },
  { "Изключи?", "Power off?" },
  // standby
  { "Телефон", "Phone" },
  { "%s %s", "%s %s" },
  { "Никой не е чут", "Nobody heard" },
  { "Шум %d", "Noise %d" },
  { "Няма GPS", "No GPS" },
  { "fix", "fix" },
  { "без fix", "no fix" },
  { "%d сат.", "%d sat" },
  { "%.0f м", "%.0f m" },
  { "Изключване", "Power off" },
  { "Батерията е изтощена", "Battery empty" },
  { "Батерия под 10%", "Battery below 10%" },
  { "Батерията може да се извади", "Battery can be removed" },
  { "Зареди батерията", "Charge the battery" },
  { "+ Back", "+ Back" },
  { "GPS: вкл", "GPS: on" },
  { "GPS: изкл", "GPS: off" },
  { "Заключено. Задръж Back", "Locked. Hold Back" },
  { "Отключено", "Unlocked" },
  { "Записано. BT име след рестарт", "Saved. BT name after restart" },
  { "Прати advert с новото име?", "Send an advert with the new name?" },
  // antenna / transmit
  { "Има ли антена?", "Antenna fitted?" },
  { "TX: %s", "TX: %s" },
  { "TX е изключен", "TX is off" },
  { "Без антена PA изгаря! Не питай?", "No antenna burns PA! Never ask?" },
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
  { "Шрифт меню", "Menu font" },
  { "Шрифт текст", "Text font" },
  { "Вибрация", "Vibration" },
  { "Посока", "Direction" },
  { "Стъпки/щрак", "Steps/click" },
  { "Скок букви", "Auto jump" },
  { "Известия", "Alerts" },
  { "Потвърди", "Confirm" },
  { "Подредба", "Sort" },
  { "Събуждане", "Wake for" },
  { "Заключване", "Key lock" },
  { "Мощност", "TX power" },
  { "Антена", "Antenna" },
  { "Пресет", "Preset" },
  { "Батерия", "Battery" },
  { "Име", "Name" },
  { "Език", "Language" },
  { "Час. пояс", "Time zone" },
  { "Лятно", "Summer" },
  { "Инфо", "About" },
  { "CLI rescue", "CLI rescue" },
  { "Rescue? Без app до рестарт", "Rescue? No app until restart" },
  { "Ъпдейт USB", "Update USB" },
  { "Ъпдейт BLE", "Update BLE" },
  { "USB ъпдейт?", "USB update?" },
  { "BLE ъпдейт?", "BLE update?" },
  { "USB ъпдейт: копирай UF2 файла", "USB update: copy the UF2 file" },
  { "BLE ъпдейт: DFU от телефона", "BLE update: DFU from the phone" },
  { "Не се поддържа", "Not supported" },
  { "Запази", "Save" },
  { "15 с", "15 s" },
  { "30 с", "30 s" },
  { "1 мин", "1 min" },
  { "5 мин", "5 min" },
  { "нощ %d", "night %d" },
  { "отдолу", "bottom" },
  { "вдясно", "right" },
  { "скрити", "hidden" },
  { "голям", "large" },
  { "среден", "medium" },
  { "малък", "small" },
  { "мини", "tiny" },
  { "нормална", "normal" },
  { "обърната", "reversed" },
  { "%d стъпки", "%d steps" },
  { "без телефон", "no phone" },
  { "винаги", "always" },
  { "никога", "never" },
  { "по име", "by name" },
  { "последни", "recent" },
  { "нова батерия", "new battery" },
  { "винаги", "always" },
  { "никога", "never" },
  { "лични", "direct" },
  { "всички", "all" },
  { "никога", "never" },
  { "ръчно", "by hand" },
  { "автоматично", "automatic" },
  { "свой", "custom" },
  { "ЕС", "EU" },
  { "%s", "%s" },
  { "%d.%02dV %d%%", "%d.%02dV %d%%" },
  { "%d контакта", "%d contacts" },
  { "Работи %s", "Up %s" },
  { "Час %02lu:%02lu%s", "Now %02lu:%02lu%s" },
  { "Няма час", "No clock" },
  { "CLI rescue", "CLI rescue" },
  { "USB порт", "USB port" },
  { "Рестарт", "Restart" },
  // tools
  { "Мрежа", "Network" },
  { "Наоколо", "Nearby" },
  { "Ефир", "Airtime" },
  { "Обхват", "Range test" },
  { "Проследи", "Trace" },
  { "Търся...", "Searching..." },
  { "Никой не отговори", "Nobody answered" },
  { "Няма отговор", "No answer" },
  { "Няма ретранслатор по пътя", "No repeater on the path" },
  { "Време %lu.%luс", "Time %lu.%lus" },
  { "Старт", "Start" },
  { "Стоп", "Stop" },
  { "През %d с", "Every %d s" },
  { "Успех %d/%d", "OK %d/%d" },
  { "Избери цел", "Pick target" },
  { "Няма цели. Виж Наоколо", "No targets. See Nearby" },
  { "1ч %d.%d/10%%", "1h %d.%d/10%%" },
  { "RX %lu", "RX %lu" },
  { "TX %lu", "TX %lu" },
  { "Грешки %lu", "Errors %lu" },
  { "RSSI %d", "RSSI %d" },
  { "SNR %.1f", "SNR %.1f" },
  { "TX общо %s", "TX total %s" },
  { "Опит %d", "Try %d" },
  { "Стигна", "Delivered" },
  { "Не стигна", "Failed" },
  { "Чака", "Waiting" },
  { "+ Добави", "+ Add" },
  { "#канал", "#channel" },
  { "Каналът е добавен", "Channel added" },
  { "Вече го има", "Already there" },
  { "Няма свободно място", "No free slot" },
  { "Само a-z 0-9 и -", "Only a-z 0-9 and -" },
  { "Заглуши", "Mute" },
  { "Звук", "Unmute" },
  { "Заглушен", "Muted" },
  { "Със звук", "Sound on" },
  { "Изтрий %s?", "Delete %s?" },
  { "Хардуер", "Hardware" },
  { "Енкодер", "Encoder" },
  { "Стъпки %+d", "Steps %+d" },
  { "Натиск %d", "Presses %d" },
  { "Задърж. %d", "Holds %d" },
  { "Сурово %d.%02dV", "Raw %d.%02dV" },
  { "ADC x%.3f", "ADC x%.3f" },
  { "USB: %s", "USB: %s" },
  { "Рестарт: %s", "Reset: %s" },
  { "Думи", "Words" },
  { "Изтрий %d научени думи?", "Delete %d learned words?" },
  { "Няма научени думи", "No learned words" },
  { "Днес", "Today" },
  { "Завърти", "Turn" },
  { "Чакам %luс", "Waiting %lus" },
  { "Пусни за", "Let go to" },
  { "отказ", "cancel" },
  { "Бутони", "Buttons" },
  { "Натисни", "Press" },
  { "Избери", "Choose" },
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

// Layout: four text sizes, each a grid of fixed character cells on the 128x64 screen. Icons are
// the 8x8 bitmaps, drawn at 2x next to the larger fonts. Dialogs, standby and the keyboard always
// use the large size; menus and content (lists of names or text, messages) follow Settings.
struct UISize {
  uint8_t cw, lh;           // character cell width, row height
  uint8_t dy;               // glyph offset from the top of its row
  uint8_t top;              // y of the first row below the title
  int8_t rule;              // y of the dotted rule under the title (clear of its descenders)
  uint8_t icon_scale;
  uint8_t gfx_size;         // built-in 5x8 font at this scale, when font is NULL
  const TextFont* font;
};
enum { SIZE_TINY, SIZE_SMALL, SIZE_MEDIUM, SIZE_LARGE };
static const UISize UI_SIZES[4] = {
  {  6,  8, 0,  9,  8, 1, 1, NULL },         // 21 chars, title + 6 rows, built-in font
  {  6, 12, 0, 13, 12, 1, 0, &font_6x12 },   // 21 chars, title + 4 rows
  {  8, 16, 1, 16, 15, 2, 0, &font_8x14 },   // 16 chars, title + 3 rows
  { 12, 16, 1, 16, 15, 2, 2, NULL },         // 10 chars, title + 3 rows, built-in font at 2x
};
static const UISize* ui_sz = &UI_SIZES[SIZE_LARGE];   // size of the screen being drawn
static int ui_width = 128, ui_height = 64;
#define CW      (ui_sz->cw)
#define LH      (ui_sz->lh)
#define TEXT_DY (ui_sz->dy)
#define ICON_W  (8 * ui_sz->icon_scale)
#define ROW_TOP (ui_sz->top)

static int rowsFor(int level) { return (ui_height - UI_SIZES[level].top) / UI_SIZES[level].lh; }   // below the title
static int colsFor(int level) { return ui_width / UI_SIZES[level].cw; }

static void drawTitleRule(DisplayDriver& d) {
  for (int x = 0; x < d.width(); x += 2) d.fillRect(x, ui_sz->rule, 1, 1);
}

static void useSize(DisplayDriver& d, int level) {
  ui_sz = &UI_SIZES[level];
  if (ui_sz->font) d.setTextFont(ui_sz->font);
  else d.setTextSize(ui_sz->gfx_size);
}
static void big(DisplayDriver& d) { useSize(d, SIZE_LARGE); }

// size of a list or of the message view. The prefs store 0 = large, so zeroed prefs keep today's look.
static int sizeLevel(bool content) {
  uint8_t v = lang_prefs ? (content ? lang_prefs->ui_text_sz : lang_prefs->ui_menu_sz) % 4 : 0;
  return SIZE_LARGE - v;
}

static void drawIcon2(DisplayDriver& d, int x, int y, const uint8_t* bits) {
  int s = ui_sz->icon_scale;
  y += (LH - 8 * s) / 2;   // centered in its row
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) if (bits[r] & (0x80 >> c)) d.fillRect(x + c * s, y + r * s, s, s);
  }
}

// up to max_lines centered lines broken at spaces, vertically centered in rows [y, y + rows * LH)
static void drawLines(DisplayDriver& d, int y, int rows, const char* text, int max_lines) {
  uint8_t starts[4], lens[4];
  if (max_lines > 4) max_lines = 4;
  int n = ui::wrapLines(text, d.width() / CW, starts, lens, max_lines);
  char line[100], filtered[100];
  int top = y + (rows - n) * LH / 2;
  for (int i = 0; i < n; i++) {
    utf8Slice(line, sizeof(line), text + starts[i], 0, lens[i]);
    d.translateUTF8ToBlocks(filtered, line, sizeof(filtered));
    d.drawTextCentered(d.width() / 2, top + i * LH + TEXT_DY, filtered);
  }
}

// bottom row of a dialog: [knob] yes  [back] no   (no == NULL: only the first)
static void drawHint(DisplayDriver& d, int y, const char* yes, const char* no) {
  int wy = 18 + d.getTextWidth(yes);
  int wn = no ? 12 + 18 + d.getTextWidth(no) : 0;
  int x = (d.width() - wy - wn) / 2;
  drawIcon2(d, x, y, icon_knob);
  d.setCursor(x + 18, y + TEXT_DY);
  d.print(yes);
  if (no) {
    x += wy + 12;
    drawIcon2(d, x, y, icon_back);
    d.setCursor(x + 18, y + TEXT_DY);
    d.print(no);
  }
}

static void drawBattery(DisplayDriver& d, int x, int y, int pct) {   // 24x12
  d.drawRect(x, y, 21, 12);
  d.drawRect(x + 1, y + 1, 19, 10);
  d.fillRect(x + 21, y + 3, 3, 6);
  d.fillRect(x + 3, y + 3, (pct * 15) / 100, 6);
}

// 4 bars by the SNR of the last packet received, 16x14
static void drawSignal(DisplayDriver& d, int x, int y, float snr) {
  int bars = snr > 5 ? 4 : snr > 0 ? 3 : snr > -7 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 3;
    if (i < bars) d.fillRect(x + i * 4, y + 14 - h, 3, h);
    else d.fillRect(x + i * 4, y + 12, 3, 2);
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
    big(display);
    display.drawTextCentered(display.width() / 2, 32 + TEXT_DY, _version_info);
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
    big(d);
    d.setColor(UIColor::warning_txt);
    d.drawTextCentered(d.width() / 2, TEXT_DY, T(S_RESCUE_L1));
    d.setColor(UIColor::primary_txt);
    d.drawTextCentered(d.width() / 2, LH + 8 + TEXT_DY, T(S_RESCUE_L2));
    // Back restarts
    int w = 18 + d.getTextWidth(T(S_RESCUE_L3));
    int x = (d.width() - w) / 2;
    drawIcon2(d, x, 3 * LH, icon_back);
    d.setCursor(x + 18, 3 * LH + TEXT_DY);
    d.print(T(S_RESCUE_L3));
    return 5000;
  }
  bool handleInput(char c) override {
    if (c == KEY_CANCEL) _task->shutdown(true);
    return true;   // nothing else leaves this screen
  }
};

static RescueScreen* rescue_screen;

// Generic scrolling list: title row and 3 to 7 item rows (by the text size), inverted selection, scrollbar,
// marquee on the selected row, optional 16x16 icon on the left and short text on the right of each
// row (only where it fits next to the label). Short lists wrap around, long ones stop at the ends.
class ListScreen : public UIScreen {
protected:
  UITask* _task;
  int _sel = 0, _top = 0;
  unsigned long _sel_since = 0;
  virtual bool isContent() { return false; }   // lists of names or text: Settings > Text, else > Menu
  int level() { return sizeLevel(isContent()); }
  int rows() { return rowsFor(level()); }

  // keeps the selection in view and the window filled; the row count follows the text size
  void clampTop(int n) {
    int rows_n = rows();
    if (_top > _sel) _top = _sel;
    if (_sel >= _top + rows_n) _top = _sel - rows_n + 1;
    if (_top > n - rows_n) _top = n > rows_n ? n - rows_n : 0;
  }

  virtual void title(char* buf, size_t n) = 0;
  virtual int count() = 0;
  virtual void label(int i, char* buf, size_t n) = 0;
  virtual const uint8_t* icon(int i) { return NULL; }
  virtual bool hasIcons() { return false; }                         // reserve the icon column
  virtual void rightText(int i, char* buf, size_t n) { buf[0] = 0; }
  virtual bool rightFirst() { return false; }   // true: the right text (a number) is never dropped
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
    clampTop(n);
  }

public:
  ListScreen(UITask* task) : _task(task) { }
  void reset() { _sel = 0; _top = 0; _sel_since = millis(); }
  void selectLast() { _sel = 0; _top = 0; select(count() - 1); }

  int render(DisplayDriver& d) override {
    char buf[200], right[16];
    int n = count();
    if (_sel >= n) select(n - 1);
    clampTop(n);   // the text size may have changed
    const int ROWS = rows();
    useSize(d, level());
    const int ROW_H = LH, TOP = ROW_TOP;
    d.setColor(UIColor::title_txt);
    bool scrolling = false;
    if (!renderHeader(d)) {
      title(buf, sizeof(buf));
      scrolling = drawMarquee(d, 0, TEXT_DY, buf, d.width() / CW, _sel_since, true);
      drawTitleRule(d);
    }
    d.setColor(UIColor::primary_txt);

    if (n == 0) {
      drawLines(d, TOP, ROWS, emptyText(), ROWS);
      return scrolling ? MARQUEE_STEP_MILLIS : 1000;
    }
    int text_w = (n > ROWS) ? d.width() - 4 : d.width();
    int x0 = hasIcons() ? ICON_W + 2 : 0;
    for (int r = 0; r < ROWS && _top + r < n; r++) {
      int i = _top + r;
      int y = TOP + r * ROW_H;
      label(i, buf, sizeof(buf));
      int max_chars = (text_w - x0) / CW;
      rightText(i, right, sizeof(right));
      int rlen = utf8Len(right);
      if (rlen > 0 && (utf8Len(buf) + 1 + rlen <= max_chars || (rightFirst() && rlen + 2 <= max_chars))) max_chars -= rlen + 1;
      else right[0] = 0;   // no room: the label wins
      const uint8_t* ic = icon(i);
      bool selected = i == _sel && listFocused();
      if (selected) {
        d.setColor(UIColor::primary_txt);
        d.fillRect(0, y, text_w, ROW_H);
        d.setColor(COLOR_BLACK);
      }
      if (ic) drawIcon2(d, 0, y, ic);
      if (drawMarquee(d, x0, y + TEXT_DY, buf, max_chars, selected ? _sel_since : 0, selected)) scrolling = true;
      if (right[0]) d.drawTextRightAlign(text_w, y + TEXT_DY, right);
      d.setColor(UIColor::primary_txt);
    }
    if (n > ROWS) {   // scrollbar
      int track = ROWS * ROW_H;
      int h = track * ROWS / n; if (h < 6) h = 6;
      int y = TOP + (track - h) * _top / (n - ROWS);
      d.fillRect(d.width() - 3, y, 3, h);
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
    big(d);
    d.setColor(UIColor::primary_txt);
    drawLines(d, 0, 3, _question, 3);
    drawHint(d, 3 * LH, T(S_YES), T(S_NO));
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
// SX1262 PA, and the chip cannot detect it. Turn or press up/down to pick yes or no, Enter answers;
// Back = no (receive only).
class AntennaScreen : public UIScreen {
  UITask* _task;
  int _sel = -1;                    // nothing chosen yet: 0 = yes, 1 = no
  unsigned long _nudge_until = 0;   // Enter before a choice: "Choose" in place of the answers
public:
  AntennaScreen(UITask* task) : _task(task) { }
  int render(DisplayDriver& d) override {
    big(d);
    d.setColor(UIColor::warning_txt);
    drawLines(d, 0, 3, T(S_ANT_Q), 3);
    d.setColor(UIColor::primary_txt);
    if (_nudge_until && !timeReached(_nudge_until)) {
      d.drawTextCentered(d.width() / 2, 3 * LH + TEXT_DY, T(S_CHOOSE));
      return 300;
    }
    _nudge_until = 0;
    // no answer is preselected: the first turn or button press picks one, and it also shows
    // whether an encoder or two buttons are fitted
    const char* ans[2] = { T(S_YES), T(S_NO) };
    for (int i = 0; i < 2; i++) {
      int cx = d.width() * (1 + 2 * i) / 4;
      int w = d.getTextWidth(ans[i]) + 8;
      if (i == _sel) {
        d.fillRect(cx - w / 2, 3 * LH, w, LH);
        d.setColor(COLOR_BLACK);
      }
      d.drawTextCentered(cx, 3 * LH + TEXT_DY, ans[i]);
      d.setColor(UIColor::primary_txt);
    }
    return 1000;
  }
  bool handleInput(char c) override {
    if (c == KEY_PREV || c == KEY_NEXT) {
      _sel = (c == KEY_PREV) ? 0 : 1;
      _nudge_until = 0;
      return true;
    }
    if (c == KEY_ENTER && _sel < 0) {
      _nudge_until = millis() + 1500;
      return true;
    }
    if (c != KEY_ENTER && c != KEY_CANCEL) return false;
    bool fitted = (c == KEY_ENTER && _sel == 0);   // Back: no, receive only
    _sel = -1;
    _task->answerAntenna(fitted);
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
      return task->addSent(t.name, MSG_SRC_CHANNEL, t.channel_idx, NULL, text, ts, 0, 0);
    }
#endif
    return NULL;
  }
  ContactInfo* c = the_mesh.lookupContactByPubKey(t.pub_key, PUB_KEY_SIZE);
  uint32_t ack = 0, timeout = 0;
  if (!c || the_mesh.sendMessage(*c, ts, 0, text, ack, timeout) == MSG_SEND_FAILED) return NULL;
  if (ack) the_mesh.expectAck(ack, c);
  return task->addSent(t.name, MSG_SRC_CONTACT, 0, t.pub_key, text, ts, ack, timeout);
}

static void closeWriting(UITask* task);
static void showSentInThread(UITask* task, uint32_t seq);

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
    big(d);
    d.setColor(UIColor::title_txt);
    char buf[48];
    drawIcon2(d, 0, 0, icon_sent);
    snprintf(buf, sizeof(buf), "%s%s", _target.is_channel ? "#" : "", _target.name);
    drawMarquee(d, 18, TEXT_DY, buf, (d.width() - 18) / CW, _started, true);
    d.setColor(UIColor::primary_txt);
    uint8_t starts[2], lens[2];
    int n = ui::wrapLines(_text, d.width() / CW, starts, lens, 2);
    char line[100], filtered[100];
    for (int i = 0; i < n; i++) {
      utf8Slice(line, sizeof(line), _text + starts[i], 0, lens[i]);
      d.translateUTF8ToBlocks(filtered, line, sizeof(filtered));
      d.setCursor(0, LH + i * LH + TEXT_DY);
      d.print(filtered);
    }
    // Back cancels; the bar runs out when the message goes
    int y = 3 * LH;
    drawIcon2(d, 0, y, icon_back);
    d.setCursor(18, y + TEXT_DY);
    d.print(T(S_BACK_CANCEL));
    int x = 18 + d.getTextWidth(T(S_BACK_CANCEL)) + 6;
    unsigned long el = millis() - _started;
    if (el > SEND_UNDO_MILLIS) el = SEND_UNDO_MILLIS;
    int full = d.width() - x;
    if (full > 0) {
      d.drawRect(x, y + 4, full, 8);
      d.fillRect(x + 2, y + 6, (int)((SEND_UNDO_MILLIS - el) * (unsigned long)(full - 4) / SEND_UNDO_MILLIS), 4);
    }
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

static bool addHashtagChannel(UITask* task, const char* typed);

// Jog-dial keyboard: turn = move over the keys, press = type, Back = delete, hold = menu.
// While a word is being typed the dictionary suggests its completion; the cursor then sits on the
// suggestion (shown inverted), so a press takes it plus a space. Turning leaves the suggestion and
// lands on its next letter ("letter jump", Settings) or back on the last letter typed.
class ComposeScreen : public UIScreen {
public:
  enum Mode { MODE_SEND, MODE_QUICK, MODE_NAME, MODE_CHANNEL };
private:
  enum Set { SET_BG, SET_EN, SET_SYM };
  enum Special { K_SHIFT = 1, K_SPACE, K_SYM, K_LANG, K_SEND };
  // rows 0-1: the text, row 2: the keys as a strip with the selected key in the middle,
  // row 3: recipient and characters left
  static const int LINE_CHARS = 10, TEXT_LINES = 2, KB_TOP = 32;   // the keyboard is always 2x

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
  // width of key i on the strip, in pixels
  int keyWidth(int i) const {
    int nc = numChars();
    if (i < nc) return CW + 2;
    switch (specialAt(i - nc)) {
      case K_SPACE: return 3 * CW + 2;
      case K_SYM: return 3 * CW + 4;
      case K_LANG: return 2 * CW + 4;
      case K_SEND: return _mode != MODE_SEND ? 2 * CW + 4 : 20;
      default: return 20;   // shift
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
    _has_rest = _mode != MODE_NAME && _mode != MODE_CHANNEL && ws < _len && _predict.suggest(&_text[ws], _rest, sizeof(_rest)) &&
                _len + (int)strlen(_rest) + 1 <= _max_len;
    _on_word = _has_rest;
  }

  void autoShift() {   // capital letter at the start and after . ! ?
    if (_mode == MODE_NAME || _mode == MODE_CHANNEL) { _shift = false; return; }
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
    if (_mode == MODE_CHANNEL) return;   // a channel name is no word to suggest in messages
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

  void drawKey(DisplayDriver& d, int i, int x, int y) {
    int w = keyWidth(i);
    bool sel = i == _sel;
    d.setColor(UIColor::primary_txt);
    if (sel && !_on_word) { d.fillRect(x, y, w, LH); d.setColor(COLOR_BLACK); }
    else if (sel) d.drawRect(x, y, w, LH);   // the cursor is on the suggestion: key outlined
    int nc = numChars();
    if (i < nc) {
      const char* p = utf8Skip(chars(), i);
      int bytes = utf8Skip(p, 1) - p;
      char ch[4];
      if (_shift && _set != SET_SYM) utf8Upper(ch, p, bytes);
      else { memcpy(ch, p, bytes); ch[bytes] = 0; }
      d.setCursor(x + 2, y + TEXT_DY);
      d.print(ch);
    } else {
      switch (specialAt(i - nc)) {
        case K_SHIFT: drawIcon2(d, x + 2, y, _shift ? icon_shift : icon_shift_o); break;
        case K_SPACE:
          d.fillRect(x + 4, y + 11, w - 8, 2);
          d.fillRect(x + 4, y + 6, 2, 5);
          d.fillRect(x + w - 6, y + 6, 2, 5);
          break;
        case K_SYM:
          d.setCursor(x + 3, y + TEXT_DY);
          d.print(_set != SET_SYM ? "123" : (_letters == SET_BG ? "абв" : "abc"));
          break;
        case K_LANG:
          d.setCursor(x + 2, y + TEXT_DY);
          d.print(_set == SET_BG ? "EN" : "БГ");
          break;
        case K_SEND:
          if (_mode != MODE_SEND) { d.setCursor(x + 2, y + TEXT_DY); d.print("OK"); }
          else drawIcon2(d, x + 2, y, icon_mail);
          break;
      }
    }
    d.setColor(UIColor::primary_txt);
  }

  // the key strip: the selected key centered, its neighbours (wrapping around) as far as they fit
  void renderKeys(DisplayDriver& d) {
    int n = numKeys();
    int w = keyWidth(_sel);
    int x = (d.width() - w) / 2;
    drawKey(d, _sel, x, KB_TOP);
    int left = x, right = x + w;
    for (int k = 1; k < n; k++) {   // to the left
      int i = (_sel - k + n) % n;
      int kw = keyWidth(i);
      if (left - kw < 0) break;
      left -= kw;
      drawKey(d, i, left, KB_TOP);
    }
    for (int k = 1; k < n; k++) {   // to the right
      int i = (_sel + k) % n;
      int kw = keyWidth(i);
      if (right + kw > d.width()) break;
      drawKey(d, i, right, KB_TOP);
      right += kw;
    }
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
      int y = r * LH;
      if (a < tlen) {
        utf8Slice(part, sizeof(part), _text, a, (b < tlen ? b : tlen) - a);
        d.setCursor(0, y + TEXT_DY);
        d.print(part);
      }
      int ga = a > tlen ? a : tlen, gb = b < tlen + glen ? b : tlen + glen;
      if (gb > ga) {   // suggestion: inverted while the cursor is on it, underlined otherwise
        utf8Slice(part, sizeof(part), _rest, ga - tlen, gb - ga);
        int x = (ga - a) * CW;
        if (_on_word) {
          d.fillRect(x, y, (gb - ga) * CW, LH);
          d.setColor(COLOR_BLACK);
        } else {
          for (int u = x; u < x + (gb - ga) * CW; u += 2) d.fillRect(u, y + LH - 1, 1, 1);
        }
        d.setCursor(x, y + TEXT_DY);
        d.print(part);
        d.setColor(UIColor::primary_txt);
      }
      if (tlen >= a && tlen < b && glen == 0) d.fillRect((tlen - a) * CW, y, 2, LH);   // cursor
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

  // a hashtag channel: its name in Latin letters, digits and '-' (the key is derived from it)
  void openChannel() {
    stashDraft();
    _mode = MODE_CHANNEL;
    _draft = false;
    _max_len = 31;   // "#" + 30 letters fit the 32-byte channel name
    setText("#");
    start();
  }

  void start() {
    while (_len > _max_len) backspace();
    _set = _letters = (_task->prefs()->ui_lang % LANG_COUNT) == 0 && _mode != MODE_CHANNEL ? SET_BG : SET_EN;
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
  void sent();   // the message went out
  WordPredictor& predictor() { return _predict; }
  void finish();

  int render(DisplayDriver& d) override {
    big(d);
    d.setColor(UIColor::primary_txt);
    renderText(d);
    renderKeys(d);

    d.setColor(UIColor::title_txt);
    char left[8];
    // room left in letters of the current layout (a Cyrillic letter takes 2 bytes)
    snprintf(left, sizeof(left), "%d", (_max_len - _len) / (_set == SET_BG ? 2 : 1));
    int lw = d.getTextWidth(left);
    char title[40];
    if (_mode == MODE_QUICK) snprintf(title, sizeof(title), T(S_QUICK_N_FMT), _slot + 1);
    else if (_mode == MODE_NAME) snprintf(title, sizeof(title), "%s", T(S_NAME_TITLE));
    else if (_mode == MODE_CHANNEL) snprintf(title, sizeof(title), "%s", T(S_CH_TITLE));
    else snprintf(title, sizeof(title), "%s%s", _target.is_channel ? "#" : "", _target.name);
    int y = 3 * LH;
    for (int x = 0; x < d.width(); x += 2) d.fillRect(x, y, 1, 1);   // dotted rule over the bottom row
    drawMarquee(d, 0, y + TEXT_DY, title, (d.width() - lw - 4) / CW, 0, false);
    d.drawTextRightAlign(d.width(), y + TEXT_DY, left);
    d.setColor(UIColor::primary_txt);
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
      if (_mode == MODE_CHANNEL && _len == 1) { setText(""); return false; }   // only the "#": leave
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

// ---- learned words: names and words the dictionary lacks, from sent messages, in /ui_words

#define WORDS_FILE "/ui_words"
#define WORDS_FILE_MAX (PREDICT_USER_WORDS * (PREDICT_MAX_WORD + 1))

static void loadLearnedWords() {
  uint8_t* buf = (uint8_t*)malloc(WORDS_FILE_MAX);
  if (!buf) return;
  int n = the_mesh.loadUIFile(WORDS_FILE, buf, WORDS_FILE_MAX);
  if (n > 0) compose_screen->predictor().importUser(buf, n);
  free(buf);
}

static bool saveLearnedWords() {
  WordPredictor& p = compose_screen->predictor();
  if (!p.userDirty()) return true;
  uint8_t* buf = (uint8_t*)malloc(WORDS_FILE_MAX);
  if (!buf) return false;
  int n = p.exportUser(buf, WORDS_FILE_MAX);
  bool ok = the_mesh.saveUIFile(WORDS_FILE, buf, n);
  free(buf);
  if (ok) p.markUserSaved();   // a failed write is tried again with the next history save
  return ok;
}

void ComposeScreen::sent() {
  learnLastWord();
  _predict.learnText(_text);   // written to flash with the history (saveHistory), not per message
  clear();
  _draft = false;
}

void SendUndoScreen::poll() {
  if (!timeReached(_started + SEND_UNDO_MILLIS)) return;
  _task->pop();   // this screen
  UIMsgEntry* sent = sendNow(_task, _target, _text);
  if (sent) {
    if (_from_compose) compose_screen->sent();
    closeWriting(_task);
    showSentInThread(_task, sent->seq);
    _task->notify(UIEventType::ack);
    _task->showAlert(T(S_SENT), 1000);
  } else {
    _task->showAlert(failText(S_SEND_FAIL), 1500);
  }
}

static void doAdvertAll(UITask* task) {
  if (the_mesh.advert(true)) {
    task->notify(UIEventType::ack);
    task->showAlert(T(S_ADV_SENT), 1000);
  } else {
    task->showAlert(failText(S_ADV_FAIL), 1000);
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
  if (_mode == MODE_CHANNEL) {
    if (_len == 0) return;
    if (addHashtagChannel(_task, _text)) {
      if (_task->current() == this) _task->pop();
      clear();
    }
    return;
  }
  if (_mode == MODE_NAME) {
    if (_len == 0) return;
    StrHelper::strncpy(_task->prefs()->node_name, _text, sizeof(_task->prefs()->node_name));
    the_mesh.savePrefs();
    if (_task->current() == this) _task->pop();
    _task->haptic(UI_HAPTIC_ACK_MS);
    // others learn the new name from an advert; Bluetooth keeps the old one until a restart
    _task->showAlert(T(S_NAME_SAVED), 2000);
    if (the_mesh.isTxAllowed()) askConfirm(_task, T(S_NAME_ADV_Q), doAdvertAll);
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
  bool isContent() override { return true; }
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

class ThreadScreen;
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
  bool isContent() override { return true; }
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

// ---- one conversation as a chat, oldest first, opened at the newest message. Every message is
// shown in full, wrapped. Turning moves from message to message; one taller than the screen
// scrolls line by line first. The selected message has a bar on its left; my own messages carry
// their delivery icon at the right of their first line. Press = reply, hold = actions.

class MessageActionsScreen;
static MessageActionsScreen* message_actions;
static void openMessageActions(UITask* task, uint32_t seq);

class ThreadScreen : public UIScreen {
  UITask* _task;
  UIMsgEntry _key;                  // copy of a message of the conversation (its identity fields)
  uint32_t _items[UI_MSG_HISTORY];  // seqs, oldest first
  int _n = 0;
  uint32_t _sel_seq = 0;            // selected message
  int _sel = 0;                     // ... its index in _items
  int _first = 0;                   // first line on screen
  unsigned long _opened = 0;

  static const int GUTTER = 4;      // selection bar on the left
  static const int MAX_MSG_LINES = 24;
  struct Line { uint8_t msg, start, len; };
  Line _lines[UI_MSG_HISTORY * MAX_MSG_LINES];
  int _num_lines = 0;
  int _msg_first[UI_MSG_HISTORY], _msg_last[UI_MSG_HISTORY];

  static int level() { return sizeLevel(true); }
  static int rows() { return rowsFor(level()); }
  UIMsgEntry* entry(int i) { return (i >= 0 && i < _n) ? _task->historyAt(_task->historyIndexOf(_items[i])) : NULL; }

  // the conversation's messages and their lines at the current text size
  void rebuild() {
    _n = 0;
    for (int i = _task->historyCount() - 1; i >= 0; i--) {
      UIMsgEntry* m = _task->historyAt(i);
      if (sameConversation(m, &_key)) _items[_n++] = m->seq;
    }
    _sel = _n - 1;
    for (int i = 0; i < _n; i++) if (_items[i] == _sel_seq) _sel = i;
    if (_sel >= 0) _sel_seq = _items[_sel];
    const UISize& z = UI_SIZES[level()];
    int cols = (ui_width - GUTTER - 4) / z.cw;
    int icon_cols = (8 * z.icon_scale + 2 + z.cw - 1) / z.cw;
    _num_lines = 0;
    for (int i = 0; i < _n; i++) {
      UIMsgEntry* m = entry(i);
      uint8_t starts[MAX_MSG_LINES], lens[MAX_MSG_LINES];
      int w = (m && m->state != MSG_IN) ? cols - icon_cols : cols;   // room for the delivery icon
      int k = m ? ui::wrapLines(m->text, w, starts, lens, MAX_MSG_LINES) : 0;
      if (k == 0) { starts[0] = 0; lens[0] = 0; k = 1; }   // an empty message still takes a row
      _msg_first[i] = _num_lines;
      for (int j = 0; j < k; j++) _lines[_num_lines++] = { (uint8_t)i, starts[j], lens[j] };
      _msg_last[i] = _num_lines - 1;
    }
    clampFirst();
  }
  void clampFirst() {
    int max_first = _num_lines - rows();
    if (_first > max_first) _first = max_first;
    if (_first < 0) _first = 0;
  }
  // after the selection moved: show the whole message if it fits, else its start (down) or end (up)
  void reveal(bool down) {
    if (_sel < 0) return;
    int a = _msg_first[_sel], b = _msg_last[_sel], r = rows();
    if (b - a + 1 > r) _first = down ? a : b - r + 1;
    else if (a < _first) _first = a;
    else if (b >= _first + r) _first = b - r + 1;
    clampFirst();
  }

public:
  ThreadScreen(UITask* task) : _task(task) { memset(&_key, 0, sizeof(_key)); }
  // open the conversation of message 'seq', with 'seq' selected and in view
  void open(uint32_t seq) {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(seq));
    if (!m) return;
    _key = *m;
    _sel_seq = seq;
    _opened = millis();
    _first = 1 << 14;   // bottom first, then the selected message moves into view
    rebuild();
    reveal(true);
  }
  uint32_t selectedSeq() const { return _sel_seq; }
  bool showing(const UIMsgEntry* m) const { return sameConversation(m, &_key); }

  int render(DisplayDriver& d) override {
    rebuild();
    if (_n == 0) { _task->pop(); return 100; }   // the whole conversation was deleted
    const int ROWS = rows();
    useSize(d, level());

    // title: the conversation, and the age of the selected message on the right
    UIMsgEntry* sel = entry(_sel);
    char buf[200], age[12];
    age[0] = 0;
    if (sel) ageOf(age, sizeof(age), sel->timestamp);
    d.setColor(UIColor::title_txt);
    int right_x = d.width();
    if (age[0]) { d.drawTextRightAlign(right_x, TEXT_DY, age); right_x -= d.getTextWidth(age) + CW; }
    snprintf(buf, sizeof(buf), "%s%s", _key.src == MSG_SRC_CHANNEL ? "#" : "", _key.from);
    bool scrolling = drawMarquee(d, 0, TEXT_DY, buf, right_x / CW, _opened, true);
    drawTitleRule(d);
    d.setColor(UIColor::primary_txt);

    char line[100], filtered[100];
    for (int r = 0; r < ROWS && _first + r < _num_lines; r++) {
      const Line& L = _lines[_first + r];
      UIMsgEntry* m = entry(L.msg);
      if (!m) continue;
      if (m->state == MSG_IN) m->unread = false;   // what is on screen counts as read
      int y = ROW_TOP + r * LH;
      if (L.msg == _sel) d.fillRect(0, y, 2, LH);
      utf8Slice(line, sizeof(line), m->text + L.start, 0, L.len);
      d.translateUTF8ToBlocks(filtered, line, sizeof(filtered));
      d.setCursor(GUTTER, y + TEXT_DY);
      d.print(filtered);
      const uint8_t* st = m->state != MSG_IN ? stateIcon(m) : NULL;
      if (st && _first + r == _msg_first[L.msg]) drawIcon2(d, d.width() - 4 - ICON_W, y, st);
    }
    if (_num_lines > ROWS) {   // scrollbar
      int track = ROWS * LH;
      int h = track * ROWS / _num_lines; if (h < 6) h = 6;
      int y = ROW_TOP + (track - h) * _first / (_num_lines - ROWS);
      d.fillRect(d.width() - 3, y, 3, h);
    }
    return scrolling ? MARQUEE_STEP_MILLIS : 1000;
  }

  bool handleInput(char c) override {
    rebuild();
    if (_n == 0) return false;
    if (c == KEY_NEXT) {
      if (_msg_last[_sel] >= _first + rows()) _first++;   // the rest of a long message first
      else if (_sel < _n - 1) { _sel_seq = _items[++_sel]; reveal(true); }
      clampFirst();
      return true;
    }
    if (c == KEY_PREV) {
      if (_msg_first[_sel] < _first) _first--;
      else if (_sel > 0) { _sel_seq = _items[--_sel]; reveal(false); }
      clampFirst();
      return true;
    }
    UIMsgEntry* m = entry(_sel);
    if (!m) return false;
    if (c == KEY_ENTER) { replyTo(_task, m); return true; }
    if (c == KEY_CONTEXT_MENU) { openMessageActions(_task, m->seq); return true; }
    return false;
  }
};

// a message sent from inside a conversation: select it, so its delivery icon is in view
static void showSentInThread(UITask* task, uint32_t seq) {
  UIMsgEntry* m = task->historyAt(task->historyIndexOf(seq));
  if (m && task->current() == thread_screen && thread_screen->showing(m)) thread_screen->open(seq);
}

bool ConversationsScreen::onEnter(int i) {
  UIMsgEntry* m = entry(i);
  if (!m) return false;
  thread_screen->open(m->seq);
  _task->push(thread_screen);
  return true;
}

// day and time of a timestamp in the local zone, as two rows for the 10 columns of the large font:
// "Today" or "11.10", and "12:20"; both "" without a clock
static void formatDateTime(char* day, char* hm, size_t n, uint32_t ts, const NodePrefs* p) {
  day[0] = hm[0] = 0;
  if (ts < VALID_EPOCH) return;
  uint32_t local = ui::localTime(ts, p->tz_offset, p->ui_dst);
  snprintf(hm, n, "%02lu:%02lu", (unsigned long)((local / 3600) % 24), (unsigned long)((local / 60) % 60));
  uint32_t now = rtc_clock.getCurrentTime();
  if (now >= VALID_EPOCH && ui::localTime(now, p->tz_offset, p->ui_dst) / 86400 == local / 86400) {
    snprintf(day, n, "%s", T(S_TODAY));
    return;
  }
  int32_t y; uint32_t mo, d;
  ui::civilFromDays((int32_t)(local / 86400), y, mo, d);
  snprintf(day, n, "%02lu.%02lu", (unsigned long)d, (unsigned long)mo);
}

// how a received message travelled: direct route, or flooded over N hops
static void formatRoute(char* buf, size_t n, uint8_t path_len) {
  if (path_len == 0xFF) snprintf(buf, n, "%s", T(S_DIRECT));
  else if ((path_len & 63) == 0) snprintf(buf, n, "%s", T(S_FLOOD));
  else snprintf(buf, n, T(S_HOPS_FMT), path_len & 63);
}

// read-only facts of one message: when, how it came (route, signal) or how its sending went
class MessageInfoScreen : public ListScreen {
  uint32_t _seq = 0;
  char _lines[6][40];
  int _n = 0;
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override {   // whose message: the contact or channel
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    snprintf(buf, n, "%s", m ? m->from : T(S_INFO));
  }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _lines[i]); }
  int render(DisplayDriver& d) override { build(); return ListScreen::render(d); }   // a delivery report may come in
public:
  MessageInfoScreen(UITask* task) : ListScreen(task) { }
  void build() {
    _n = 0;
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    if (!m) return;
    formatDateTime(_lines[_n], _lines[_n + 1], 40, m->timestamp, _task->prefs());
    if (_lines[_n][0]) _n += 2;
    if (m->state == MSG_IN) {
      formatRoute(_lines[_n++], 40, m->path_len);
      if (m->rssi != 0) {
        snprintf(_lines[_n++], 40, T(S_SNR_FMT), m->snr4 / 4.0f);
        snprintf(_lines[_n++], 40, T(S_RSSI_FMT), (int)m->rssi);
      }
    } else if (m->state == MSG_OUT_SENT) {
      snprintf(_lines[_n++], 40, "%s", T(S_SENT));
    } else {
      StrId st = m->state == MSG_OUT_ACKED ? S_DELIVERED : m->state == MSG_OUT_FAILED ? S_NOT_DELIVERED : S_WAITING;
      snprintf(_lines[_n++], 40, "%s", T(st));
      snprintf(_lines[_n++], 40, T(S_ATTEMPT_FMT), m->attempt + 1);
    }
  }
  void open(uint32_t seq) { _seq = seq; build(); reset(); }
};

static MessageInfoScreen* message_info;

static void openMessageInfo(UITask* task, uint32_t seq) {
  message_info->open(seq);
  task->push(message_info);
}

// pop-up for one message: reply, send again (when not delivered), delete, delete conversation
class MessageActionsScreen : public ListScreen {
  uint32_t _seq = 0;   // the message acted on, independent of history shifts
  enum { A_REPLY, A_RETRY, A_INFO, A_DEL, A_DEL_CONV };
  uint8_t _acts[5];
  int _n = 0;
protected:
  void title(char* buf, size_t n) override {
    UIMsgEntry* m = _task->historyAt(_task->historyIndexOf(_seq));
    snprintf(buf, n, "%s", m ? m->from : T(S_MSGS));
  }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_REPLY, S_RETRY, S_INFO, S_DEL, S_DEL_CONV };
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
    _acts[_n++] = A_INFO;
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
  task->deleteHistory(idx);
  task->haptic(UI_HAPTIC_ACK_MS);
  task->showAlert(T(S_DELETED), 800);
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
      if (_task->resend(_seq)) {
        _task->notify(UIEventType::ack);
        _task->showAlert(T(S_SENT), 1000);
      } else {
        _task->showAlert(failText(S_SEND_FAIL), 1500);
      }
      break;
    case A_INFO:
      openMessageInfo(_task, _seq);
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
  char _lines[7][40];
  int _n = 0;
  char _name[32];
protected:
  bool isContent() override { return true; }
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
    snprintf(_lines[_n++], 40, "%s", contactTypeName(c->type));
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
      snprintf(_lines[_n++], 40, "%.4f", c->gps_lat / 1000000.0);
      snprintf(_lines[_n++], 40, "%.4f", c->gps_lon / 1000000.0);
    }
    reset();
  }
};

static ContactInfoScreen* contact_info;

static void traceContact(UITask* task, const ContactInfo& c);

// pop-up on a contact: write, trace, details, reset path
class ContactActionsScreen : public ListScreen {
  uint8_t _key[PUB_KEY_SIZE];
  char _name[32];
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _name); }
  int count() override { return 4; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_WRITE, S_TRACE, S_INFO, S_RESET_PATH };
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
      traceContact(_task, *c);
    } else if (i == 2) {
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
  bool isContent() override { return true; }
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
    const int CELL_W = CW + 2, VISIBLE = d.width() / CELL_W;
    int first = cur - VISIBLE / 2;
    if (first > _num_groups - VISIBLE) first = _num_groups - VISIBLE;
    if (first < 0) first = 0;
    d.setColor(UIColor::title_txt);
    for (int i = first; i < _num_groups && i < first + VISIBLE; i++) {
      int x = (i - first) * CELL_W;
      if (i == cur && _bar == BAR_ACTIVE) {
        d.fillRect(x, 0, CELL_W, ROW_TOP - 1);
        d.setColor(COLOR_BLACK);
      } else if (i == cur && _bar == BAR_FOCUS) {
        d.drawRect(x, 0, CELL_W, ROW_TOP - 1);
      } else if (i == cur) {
        d.fillRect(x + 1, ROW_TOP - 2, CELL_W - 2, 2);
      }
      d.setCursor(x + 1, TEXT_DY);
      d.print(_groups[i].label);
      d.setColor(UIColor::title_txt);
    }
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
        _top = item > n - rows() ? (n > rows() ? n - rows() : 0) : item;   // the letter's first name on top
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

// Channels: press = the channel's conversation, hold = write / mute / delete. The last row adds a
// hashtag channel (its key comes from its name, like in the app).
class ChannelsScreen : public ListScreen {
  uint8_t _channels[64];
  int _n = 0;
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_CHANNELS)); }
  int count() override { return _n + 1; }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override {
    if (i >= _n) return NULL;
    return _task->isChannelMuted(_channels[i]) ? icon_mute : icon_hash;
  }
  void label(int i, char* buf, size_t n) override {
    ComposeTarget t;
    if (i >= _n) snprintf(buf, n, "%s", T(S_NEW_CHANNEL));
    else if (targetForChannel(_channels[i], t)) snprintf(buf, n, "%s", t.name);
    else buf[0] = 0;
  }
  // the newest message of the channel in the history, or NULL
  UIMsgEntry* lastMessage(const ComposeTarget& t) {
    for (int k = 0; k < _task->historyCount(); k++) {
      UIMsgEntry* m = _task->historyAt(k);
      if (m->src == MSG_SRC_CHANNEL && m->channel_idx == t.channel_idx && strcmp(m->from, t.name) == 0) return m;
    }
    return NULL;
  }
  void rightText(int i, char* buf, size_t n) override {
    ComposeTarget t;
    UIMsgEntry* m = i < _n && targetForChannel(_channels[i], t) ? lastMessage(t) : NULL;
    if (m) ageOf(buf, n, m->timestamp);
    else buf[0] = 0;
  }
  // press: the channel's conversation (press there to write), or straight to writing when it has none
  bool onEnter(int i) override;
  bool onContext(int i) override;
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
    if (_sel > _n) _sel = _n;
  }
};

static ChannelsScreen* channels_screen;

static int confirm_channel = -1;

static void doDeleteChannel(UITask* task) {
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails empty;
  memset(&empty, 0, sizeof(empty));
  int idx = confirm_channel;
  task->setChannelMuted(idx, false);
  if (idx >= 0 && the_mesh.setChannelAndSave(idx, empty)) {
    task->haptic(UI_HAPTIC_ACK_MS);
    task->showAlert(T(S_DELETED), 800);
  } else {
    task->showAlert(T(S_SAVE_FAIL), 1500);
  }
  channels_screen->open();
#endif
}

// pop-up on a channel: write, mute or unmute, delete
class ChannelActionsScreen : public ListScreen {
  int _idx = 0;
  char _name[32];
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _name); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    if (i == 0) snprintf(buf, n, "%s", T(S_WRITE));
    else if (i == 1) snprintf(buf, n, "%s", T(_task->isChannelMuted(_idx) ? S_UNMUTE : S_MUTE));
    else snprintf(buf, n, "%s", T(S_DEL));
  }
  bool onEnter(int i) override {
    _task->pop();
    ComposeTarget t;
    if (!targetForChannel(_idx, t)) { _task->showAlert(T(S_NO_TARGET), 1200); return true; }
    if (i == 0) {
      openReplyPick(_task, t);
    } else if (i == 1) {
      bool mute = !_task->isChannelMuted(_idx);
      if (_task->setChannelMuted(_idx, mute)) {
        _task->haptic(UI_HAPTIC_ACK_MS);
        _task->showAlert(T(mute ? S_MUTED : S_UNMUTED), 800);
      } else {
        _task->showAlert(T(S_SAVE_FAIL), 1500);
      }
    } else {
      confirm_channel = _idx;
      char q[64];
      snprintf(q, sizeof(q), T(S_DEL_X_Q), t.name);
      askConfirm(_task, q, doDeleteChannel);
    }
    return true;
  }
public:
  ChannelActionsScreen(UITask* task) : ListScreen(task) { _name[0] = 0; }
  void open(int idx) {
    _idx = idx;
    ComposeTarget t;
    StrHelper::strncpy(_name, targetForChannel(idx, t) ? t.name : "", sizeof(_name));
    reset();
  }
};

static ChannelActionsScreen* channel_actions;

bool ChannelsScreen::onEnter(int i) {
  if (i >= _n) {   // + new channel
    compose_screen->openChannel();
    _task->push(compose_screen);
    return true;
  }
  ComposeTarget t;
  if (!targetForChannel(_channels[i], t)) return false;
  UIMsgEntry* m = lastMessage(t);
  if (m) {
    thread_screen->open(m->seq);
    _task->push(thread_screen);
  } else {
    openReplyPick(_task, t);
  }
  return true;
}

bool ChannelsScreen::onContext(int i) {
  if (i >= _n) return onEnter(i);
  channel_actions->open(_channels[i]);
  _task->push(channel_actions);
  return true;
}

// "#name" from what was typed: lower case a-z, digits and '-'; the key is the first 16 bytes of
// sha256("#name") (docs/companion_protocol.md), so everyone who types the name joins the same channel
static bool addHashtagChannel(UITask* task, const char* typed) {
#ifdef MAX_GROUP_CHANNELS
  char name[32];
  int j = 0;
  name[j++] = '#';
  for (const char* p = typed; *p; p++) {
    char c = *p;
    if (c == '#' && j == 1) continue;   // typed by hand: added anyway
    if (c >= 'A' && c <= 'Z') c += 32;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      task->showAlert(T(S_CH_BAD), 1500);
      return false;
    }
    if (j >= (int)sizeof(name) - 1) break;
    name[j++] = c;
  }
  name[j] = 0;
  if (j < 2) { task->showAlert(T(S_CH_BAD), 1500); return false; }
  ChannelDetails ch;
  int free_slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!the_mesh.getChannel(i, ch) || !ch.name[0]) { if (free_slot < 0) free_slot = i; continue; }
    if (strcmp(ch.name, name) == 0) { task->showAlert(T(S_CH_EXISTS), 1200); return false; }
  }
  if (free_slot < 0) { task->showAlert(T(S_CH_FULL), 1500); return false; }
  memset(&ch, 0, sizeof(ch));
  StrHelper::strncpy(ch.name, name, sizeof(ch.name));
  uint8_t hash[32];
  mesh::Utils::sha256(hash, sizeof(hash), (const uint8_t*)name, strlen(name));
  memcpy(ch.channel.secret, hash, 16);   // 128-bit key, the rest stays zero
  task->setChannelMuted(free_slot, false);   // a muted channel that was in this slot before
  if (!the_mesh.setChannelAndSave(free_slot, ch)) { task->showAlert(T(S_SAVE_FAIL), 1500); return false; }
  task->haptic(UI_HAPTIC_ACK_MS);
  task->showAlert(T(S_CH_ADDED), 1000);
  channels_screen->open();
  return true;
#else
  return false;
#endif
}

// ---------------------------------------------------------------- settings

class QuickEditScreen : public ListScreen {
protected:
  bool isContent() override { return true; }
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

// brightness 1..BRIGHT_LEVELS; anything else (unset, corrupt prefs) is full brightness
static int brightLevel(const NodePrefs* p) {
  int l = p->ui_bright_lvl;
  return (l >= 1 && l <= BRIGHT_LEVELS) ? l : BRIGHT_LEVELS;
}

enum SettingId : uint8_t {
  ST_OFF, ST_BRIGHT, ST_DOTS, ST_MENU_SZ, ST_TEXT_SZ,
  ST_VIBE, ST_ENC, ST_STEPS, ST_SKIP, ST_LOCK,
  ST_NOTIFY, ST_WAKE, ST_DELCONF, ST_CSORT, ST_QUICK,
  ST_TX, ST_TXPOW, ST_ANT, ST_PRESET,
  ST_NAME, ST_LANG, ST_TZ, ST_DST, ST_BATT, ST_ABOUT, ST_RESCUE, ST_DFU_USB, ST_DFU_BLE,
  ST_WORDS, ST_HW,
};

// the EU/UK radio presets of the MeshCore app (api.meshcore.nz/api/v1/config, 2026-10-12):
// "EU/UK (Narrow)" and "EU/UK (Deprecated)"; anything else shows as "custom" and stays
struct RadioPreset { const char* name; float freq, bw; uint8_t sf, cr; };
static const RadioPreset PRESETS[] = {
  { "EU/UK Narrow",     869.618f,  62.5f,  8, 8 },
  { "EU/UK Deprecated", 869.525f, 250.0f, 11, 5 },
};
#define NUM_PRESETS ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static int currentPreset(const NodePrefs* p) {
  for (int i = 0; i < NUM_PRESETS; i++) {
    const RadioPreset& r = PRESETS[i];
    if (fabsf(p->freq - r.freq) < 0.0005f && fabsf(p->bw - r.bw) < 0.05f && p->sf == r.sf && p->cr == r.cr) return i;
  }
  return NUM_PRESETS;   // custom
}

// battery calibration: the editor starts at the voltage measured when it opened, the user turns it
// to what a multimeter shows, and the ADC multiplier is scaled by the ratio
#define BATT_CAL_MIN_MV 3000
#define BATT_CAL_STEPS  151        // 3.00 .. 4.50 V in 10 mV steps
static uint16_t batt_cal_mv = 0;   // measured when the editor opened

#define TZ_MIN  -12
#define TZ_MAX   14
#define TXPOW_MIN -9

static StrId settingName(uint8_t id) {
  static const StrId N[] = {
    S_SET_OFF, S_SET_BRIGHT, S_SET_DOTS, S_SET_MENU_SZ, S_SET_TEXT_SZ,
    S_SET_VIBE, S_SET_ENC, S_SET_STEPS, S_SET_SKIP, S_SET_LOCK,
    S_SET_NOTIFY, S_SET_WAKE, S_SET_DELCONF, S_SET_CSORT, S_QUICK_MSGS,
    S_TX_FMT, S_SET_TXPOW, S_SET_ANT, S_SET_PRESET,
    S_SET_NAME, S_SET_LANG, S_SET_TZ, S_SET_DST, S_SET_BATT, S_ABOUT, S_RESCUE, S_SET_DFU_USB, S_SET_DFU_BLE,
    S_WORDS, S_HARDWARE,
  };
  return N[id];
}

// number of values; 0 = opens a screen or runs an action
static int optCount(uint8_t id) {
  switch (id) {
    case ST_OFF: return 4;
    case ST_BRIGHT: return BRIGHT_LEVELS;
    case ST_DOTS: case ST_NOTIFY: case ST_ANT: case ST_WAKE: return 3;
    case ST_PRESET: return NUM_PRESETS + 1;
    case ST_BATT: return BATT_CAL_STEPS;
    case ST_MENU_SZ: case ST_TEXT_SZ: return 4;
    case ST_VIBE: case ST_ENC: case ST_STEPS: case ST_SKIP: case ST_DELCONF: case ST_CSORT: case ST_LOCK:
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
    case ST_BRIGHT: return brightLevel(p) - 1;
    case ST_DOTS: return p->ui_dots % 3;
    case ST_MENU_SZ: return p->ui_menu_sz % 4;
    case ST_TEXT_SZ: return p->ui_text_sz % 4;
    case ST_VIBE: return p->vibe_quiet ? 0 : 1;
    case ST_ENC: return p->ui_enc_rev ? 1 : 0;
    case ST_STEPS: return encoderSteps(p) == 2 ? 0 : 1;
    case ST_SKIP: return p->ui_skip ? 0 : 1;
    case ST_NOTIFY: return p->ui_notify % 3;
    case ST_WAKE: return p->ui_wake % 3;
    case ST_LOCK: return p->ui_lock ? 1 : 0;
    case ST_PRESET: return currentPreset(p);
    case ST_BATT: {
      batt_cal_mv = task->getBattMilliVolts();
      int v = ((int)batt_cal_mv - BATT_CAL_MIN_MV + 5) / 10;
      return v < 0 ? 0 : v >= BATT_CAL_STEPS ? BATT_CAL_STEPS - 1 : v;
    }
    case ST_DELCONF: return p->ui_delconf ? 0 : 1;
    case ST_CSORT: return p->ui_csort ? 1 : 0;
    case ST_TX: return the_mesh.isTxAllowed() ? 1 : 0;
    case ST_TXPOW: return p->tx_power_dbm - TXPOW_MIN;
    case ST_ANT: return p->ui_ant % 3;
    case ST_LANG: return p->ui_lang % LANG_COUNT;
    case ST_TZ: return p->tz_offset - TZ_MIN;
    case ST_DST: return p->ui_dst ? 1 : 0;
    default: return 0;
  }
}

static void optText(uint8_t id, int v, char* buf, size_t n) {
  switch (id) {
    case ST_OFF: snprintf(buf, n, "%s", T((StrId)(S_OFF_15S + v))); break;
    case ST_BRIGHT:
      if (v < NIGHT_LEVELS) snprintf(buf, n, T(S_NIGHT_FMT), v + 1);
      else snprintf(buf, n, "%d/%d", v + 1 - NIGHT_LEVELS, BRIGHT_LEVELS - NIGHT_LEVELS);
      break;
    case ST_DOTS: snprintf(buf, n, "%s", T((StrId)(S_DOTS_BOTTOM + v))); break;
    case ST_MENU_SZ: case ST_TEXT_SZ: snprintf(buf, n, "%s", T((StrId)(S_SIZE_LARGE + v))); break;
    case ST_ENC: snprintf(buf, n, "%s", T(v ? S_ENC_REVERSED : S_ENC_NORMAL)); break;
    case ST_STEPS: snprintf(buf, n, T(S_STEPS_FMT), v ? 4 : 2); break;
    case ST_NOTIFY: snprintf(buf, n, "%s", T((StrId)(S_NOTIFY_NO_PHONE + v))); break;
    case ST_WAKE: snprintf(buf, n, "%s", T((StrId)(S_WAKE_DIRECT + v))); break;
    case ST_LOCK: snprintf(buf, n, "%s", T(v ? S_LOCK_AUTO : S_LOCK_HAND)); break;
    case ST_PRESET: snprintf(buf, n, "%s", v < NUM_PRESETS ? PRESETS[v].name : T(S_PRESET_CUSTOM)); break;
    case ST_BATT: { int mv = BATT_CAL_MIN_MV + v * 10; snprintf(buf, n, "%d.%02d V", mv / 1000, (mv % 1000) / 10); break; }
    case ST_CSORT: snprintf(buf, n, "%s", T(v ? S_CSORT_RECENT : S_CSORT_NAME)); break;
    case ST_TXPOW: snprintf(buf, n, "%d dBm", v + TXPOW_MIN); break;
    case ST_ANT: snprintf(buf, n, "%s", T((StrId)(S_ANT_POWER_LOSS + v))); break;
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
    case ST_BRIGHT: p->ui_bright_lvl = v + 1; task->applyContrast(); break;
    case ST_DOTS: p->ui_dots = v; break;
    case ST_MENU_SZ: p->ui_menu_sz = v; break;
    case ST_TEXT_SZ: p->ui_text_sz = v; break;
    case ST_VIBE: p->vibe_quiet = v ? 0 : 1; break;
    case ST_ENC: p->ui_enc_rev = v; task->applyEncoderPrefs(); break;
    case ST_STEPS: p->ui_enc_steps = v ? 4 : 2; task->applyEncoderPrefs(); break;
    case ST_SKIP: p->ui_skip = v ? 0 : 1; break;
    case ST_NOTIFY: p->ui_notify = v; break;
    case ST_WAKE: p->ui_wake = v; break;
    case ST_LOCK: p->ui_lock = v; break;
    case ST_BATT: {
      // scale the multiplier by multimeter / shown; the build default when the board has none yet
      float mult = board.getAdcMultiplier();
      int target = BATT_CAL_MIN_MV + v * 10;
      if (mult > 0 && batt_cal_mv > 0 && target != (int)batt_cal_mv) {
        mult = mult * target / batt_cal_mv;
        if (board.setAdcMultiplier(mult)) p->adc_mult = mult;
        task->resetBattery();
      }
      break;
    }
    case ST_DELCONF: p->ui_delconf = v ? 0 : 1; break;
    case ST_CSORT: p->ui_csort = v; break;
    case ST_ANT: p->ui_ant = v; break;
    case ST_LANG: p->ui_lang = v; break;
    case ST_TZ: p->tz_offset = v + TZ_MIN; break;
    case ST_DST: p->ui_dst = v; break;
  }
}

static void saveOpt(UITask* task, uint8_t id, int v) {
  if (id == ST_TXPOW) {
    the_mesh.setTxPower(v + TXPOW_MIN);   // applied and saved
  } else if (id == ST_PRESET) {
    if (v < NUM_PRESETS) {
      const RadioPreset& r = PRESETS[v];
      if (!the_mesh.setRadioParams(r.freq, r.bw, r.sf, r.cr)) { task->showAlert(T(S_SAVE_FAIL), 1500); return; }
    }
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
    big(d);
    d.setColor(UIColor::title_txt);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s", T(settingName(_id)));
    if (_id == ST_TX) snprintf(buf, sizeof(buf), "TX");
    drawMarquee(d, 0, TEXT_DY, buf, d.width() / CW, 0, false);
    for (int x = 0; x < d.width(); x += 2) d.fillRect(x, LH - 1, 1, 1);
    d.setColor(UIColor::primary_txt);
    char val[24];
    optText(_id, _val, val, sizeof(val));
    if (_id == ST_MENU_SZ || _id == ST_TEXT_SZ) {   // the value in its own size
      d.setCursor(0, LH + 8 + TEXT_DY); d.print("<");
      d.drawTextRightAlign(d.width(), LH + 8 + TEXT_DY, ">");
      useSize(d, SIZE_LARGE - _val);
      d.drawTextCentered(d.width() / 2, 32 - LH / 2 + TEXT_DY, val);
      big(d);
    } else if (utf8Len(val) <= 8) {   // one row between arrows
      d.drawTextCentered(d.width() / 2, LH + 8 + TEXT_DY, val);
      d.setCursor(0, LH + 8 + TEXT_DY); d.print("<");
      d.drawTextRightAlign(d.width(), LH + 8 + TEXT_DY, ">");
      if (_id == ST_BRIGHT) {   // level bar: filled up to the level (night ones hollow), a dash for the rest
        const int sw = 7, gap = 2, y = 2 * LH + 10;
        int x = (d.width() - (BRIGHT_LEVELS * (sw + gap) - gap)) / 2;
        for (int i = 0; i < BRIGHT_LEVELS; i++, x += sw + gap) {
          if (i > _val) d.fillRect(x, y + 3, sw, 1);
          else if (i < NIGHT_LEVELS) d.drawRect(x, y, sw, 4);
          else d.fillRect(x, y, sw, 4);
        }
      }
    } else {
      drawLines(d, LH, 2, val, 2);
    }
    drawHint(d, 3 * LH, T(S_EDIT_HINT), NULL);
    return 1000;
  }
  bool handleInput(char c) override {
    int n = optCount(_id);
    if ((c == KEY_NEXT || c == KEY_PREV) && n > 0) {
      int v = _val + (c == KEY_NEXT ? 1 : -1);
      if (n <= 4) v = (v + n) % n;            // short lists wrap around
      else if (v < 0 || v >= n) return true;  // ranges stop at the ends
      _val = v;
      if (_id == ST_BRIGHT) { _task->prefs()->ui_bright_lvl = _val + 1; _task->applyContrast(); }   // preview
      return true;
    }
    if (c == KEY_ENTER && _id == ST_TX) {
      _task->pop();
      if (_val == 0 && the_mesh.isTxAllowed()) { _task->setTransmit(false); _task->haptic(UI_HAPTIC_ACK_MS); }
      else if (_val == 1 && !the_mesh.isTxAllowed()) turnTransmitOn(_task);
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
      _task->prefs()->ui_bright_lvl = _orig + 1;
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
  bool isContent() override { return true; }
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
    uint16_t mv = _task->battMilliVolts();
    snprintf(_lines[_n++], 40, T(S_BATT_FMT), mv / 1000, (mv % 1000) / 10, battPercent(mv));
    snprintf(_lines[_n++], 40, T(S_CONTACTS_FMT), the_mesh.getNumContacts(), MAX_CONTACTS);
    char age[12];
    formatAge(age, sizeof(age), millis() / 1000);
    snprintf(_lines[_n++], 40, T(S_UPTIME_FMT), age);
    uint32_t now = rtc_clock.getCurrentTime();
    if (now >= VALID_EPOCH) {
      uint32_t local = ui::localTime(now, _task->prefs()->tz_offset, _task->prefs()->ui_dst);
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
static void doClearWords(UITask* task) {
  compose_screen->predictor().clearUser();
  if (saveLearnedWords()) { task->haptic(UI_HAPTIC_ACK_MS); task->showAlert(T(S_DELETED), 800); }
  else task->showAlert(T(S_SAVE_FAIL), 1500);
}
static void openHardware(UITask* task);
static void doDfuUsb(UITask* task) { task->enterBootloader(true); }
static void doDfuBle(UITask* task) { task->enterBootloader(false); }

class SettingsGroupScreen : public ListScreen {
  const uint8_t* _items = NULL;
  int _n = 0;
  StrId _title = S_SETTINGS;
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(_title)); }
  int count() override { return _n; }
  // names only (a value does not fit beside them at this font size); press opens the value
  void label(int i, char* buf, size_t n) override {
    uint8_t id = _items[i];
    snprintf(buf, n, "%s", id == ST_TX ? "TX" : T(settingName(id)));
  }
  bool onEnter(int i) override {
    uint8_t id = _items[i];
    int n = optCount(id);
    if (n >= 2) {
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
    } else if (id == ST_WORDS) {
      int n = compose_screen->predictor().userCount();
      if (n == 0) { _task->showAlert(T(S_WORDS_NONE), 1200); return true; }
      char q[64];
      snprintf(q, sizeof(q), T(S_WORDS_Q_FMT), n);
      askConfirm(_task, q, doClearWords);
    } else if (id == ST_HW) {
      openHardware(_task);
    } else if (id == ST_RESCUE) {
      askConfirm(_task, T(S_RESCUE_Q), doRescue);
    } else if (id == ST_DFU_USB) {
      askConfirm(_task, T(S_DFU_USB_Q), doDfuUsb);
    } else if (id == ST_DFU_BLE) {
      askConfirm(_task, T(S_DFU_BLE_Q), doDfuBle);
    }
    return true;
  }
public:
  SettingsGroupScreen(UITask* task) : ListScreen(task) { }
  void open(StrId title, const uint8_t* items, int n) { _title = title; _items = items; _n = n; reset(); }
};

static SettingsGroupScreen* settings_group;

static const uint8_t GROUP_DISPLAY[] = { ST_MENU_SZ, ST_TEXT_SZ, ST_OFF, ST_BRIGHT, ST_DOTS };
static const uint8_t GROUP_INPUT[]   = { ST_VIBE, ST_ENC, ST_STEPS, ST_SKIP, ST_LOCK };
static const uint8_t GROUP_MSGS[]    = { ST_NOTIFY, ST_WAKE, ST_DELCONF, ST_CSORT, ST_QUICK, ST_WORDS };
static const uint8_t GROUP_RADIO[]   = { ST_TX, ST_TXPOW, ST_ANT, ST_PRESET };
static const uint8_t GROUP_SYSTEM[]  = { ST_NAME, ST_LANG, ST_TZ, ST_DST, ST_BATT, ST_ABOUT, ST_HW, ST_RESCUE, ST_DFU_USB, ST_DFU_BLE };

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

static ListScreen* network_menu;   // Network: nearby, airtime, range test (defined with the tools)

class MainMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_MENU)); }
  int count() override { return 5; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_MSGS, S_CONTACTS, S_CHANNELS, S_NETWORK, S_SETTINGS };
    snprintf(buf, n, "%s", T(L[i]));
  }
  bool onEnter(int i) override {
    if (i == 0) { conversations->reset(); _task->push(conversations); }
    else if (i == 1) { contacts_screen->open(); _task->push(contacts_screen); }
    else if (i == 2) { channels_screen->open(); channels_screen->reset(); _task->push(channels_screen); }
    else if (i == 3) { network_menu->reset(); _task->push(network_menu); }
    else { settings_screen->reset(); _task->push(settings_screen); }
    return true;
  }
public:
  MainMenuScreen(UITask* task) : ListScreen(task) { }
};

static void doHibernate(UITask* task) { task->hibernate(false); }

class QuickMenuScreen : public ListScreen {
  enum { ADVERT, ADVERT_ALL, TRANSMIT, BLUETOOTH,
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
      case ADVERT_ALL: snprintf(buf, n, "%s", T(S_SEND_ADV_ALL)); break;
      case TRANSMIT: snprintf(buf, n, T(S_TX_FMT), the_mesh.isTxAllowed() ? T(S_ON) : T(S_OFF)); break;
      case BLUETOOTH:
        // the pairing PIN is shown while Bluetooth waits for a phone
        if (_task->isBluetoothEnabled() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
          snprintf(buf, n, "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
        } else {
          snprintf(buf, n, "BT: %s", _task->isBluetoothEnabled() ? T(S_ON) : T(S_OFF));
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
      case ADVERT_ALL:
        if (the_mesh.advert(i == ADVERT_ALL)) {
          _task->notify(UIEventType::ack);
          _task->showAlert(T(S_ADV_SENT), 1000);
        } else {
          _task->showAlert(failText(S_ADV_FAIL), 1000);
        }
        break;
      case TRANSMIT:
        if (the_mesh.isTxAllowed()) {
          _task->setTransmit(false);
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

static bool rangeRunning();   // the range test (Network) is on: an icon in the status row

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

  // status row: card icon or unread count on the left; signal, Bluetooth, charging, battery on the right
  void renderHeader(DisplayDriver& d) {
    d.setColor(UIColor::title_txt);
    int x = d.width() - 24;
    drawBattery(d, x, 2, battPercent(_task->battMilliVolts()));
    if (isCharging()) { x -= 16; drawIcon2(d, x, 0, icon_bolt); }
    if (_task->isBluetoothEnabled()) { x -= 16; drawIcon2(d, x, 0, icon_bt); }
    if (rx_any && millis() - rx_at < SIGNAL_STALE_MILLIS) { x -= 18; drawSignal(d, x, 0, rx_snr); }
    if (rangeRunning()) { x -= 16; drawIcon2(d, x, 0, icon_range); }
    if (!the_mesh.isTxAllowed()) {   // crossed-out "TX": receive only
      x -= 2 * CW + 4;
      d.setCursor(x, TEXT_DY);
      d.print("TX");
      d.fillRect(x - 1, 7, 2 * CW + 1, 2);
    }
    int unread = _task->unreadCount();
    if (_card == SUMMARY) {
      if (unread > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), unread > 99 ? "99+" : "%d", unread);
        drawIcon2(d, 0, 0, icon_mail);
        if (18 + d.getTextWidth(buf) <= x - 2) { d.setCursor(18, TEXT_DY); d.print(buf); }
      }
    } else {
      static const uint8_t* const ICONS[] = { NULL, icon_person, icon_radio, icon_batt,
#if ENV_INCLUDE_GPS == 1
        icon_pin,
#endif
      };
      drawIcon2(d, 0, 0, ICONS[_card]);
    }
  }

  void renderDots(DisplayDriver& d) {
    if (_prefs->ui_dots == DOTS_NONE) return;
    for (int i = 0; i < CARDS; i++) {
      if (_prefs->ui_dots == DOTS_RIGHT) {
        int x = d.width() - 3, y = LH + (3 * LH - CARDS * 8) / 2 + i * 8;
        if (i == _card) d.fillRect(x, y, 3, 5); else d.fillRect(x + 1, y + 1, 1, 3);
      } else {
        int x = d.width() / 2 - CARDS * 5 + i * 10;
        if (i == _card) d.fillRect(x, 62, 6, 2); else d.fillRect(x + 2, 62, 2, 2);
      }
    }
  }

  // the bottom row, above the page dots
  int bottomY() const { return _prefs->ui_dots == DOTS_BOTTOM ? 3 * LH - 4 : 3 * LH; }
  int cols(DisplayDriver& d) const { return (_prefs->ui_dots == DOTS_RIGHT ? d.width() - 5 : d.width()) / CW; }

  bool renderSummary(DisplayDriver& d) {
    bool scrolling = false;
    char buf[200];
    int unread = _task->unreadCount();
    int w = cols(d) * CW;
    int by = bottomY();

    // the clock, 4x ("~" in the bottom row = time taken from an advert)
    uint32_t now = rtc_clock.getCurrentTime();
    if (now >= VALID_EPOCH) {
      uint32_t local = ui::localTime(now, _prefs->tz_offset, _prefs->ui_dst);
      snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)((local / 3600) % 24), (unsigned long)((local / 60) % 60));
    } else {
      snprintf(buf, sizeof(buf), "--:--");
    }
    d.setTextSize(4);
    d.drawTextCentered(w / 2, LH, buf);
    big(d);
    int max_chars = w / CW;
    if (now >= VALID_EPOCH && the_mesh.isClockApprox()) {
      d.drawTextRightAlign(w, by + TEXT_DY, "~");
      max_chars--;
    }

    // bottom row: the newest unread message (inverted for a few seconds after it arrives), else
    // the pairing PIN, the phone, or the last node heard
    UIMsgEntry* m = NULL;
    for (int i = 0; unread > 0 && i < _task->historyCount(); i++) {
      UIMsgEntry* e = _task->historyAt(i);
      if (e->unread) { m = e; break; }
    }
    if (m) {
      snprintf(buf, sizeof(buf), "%s: %s", m->from, m->text);
      bool fresh = millis() - _task->lastNewMsgAt() < NEW_MSG_HIGHLIGHT;
      if (fresh) {
        d.fillRect(0, by, w, LH);
        d.setColor(COLOR_BLACK);
        scrolling = true;
      }
      if (drawMarquee(d, 0, by + TEXT_DY, buf, max_chars, _task->lastNewMsgAt(), true)) scrolling = true;
      d.setColor(UIColor::primary_txt);
      return scrolling;
    }
    if (_task->isBluetoothEnabled() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
      snprintf(buf, sizeof(buf), "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
    } else if (_task->hasConnection()) {
      snprintf(buf, sizeof(buf), "%s", T(S_PHONE));
    } else if (the_mesh.getRecentlyHeard(_recent, 1) > 0 && _recent[0].name[0]) {
      char age[12];
      ageOf(age, sizeof(age), _recent[0].recv_timestamp);
      snprintf(buf, sizeof(buf), T(S_HEARD_FMT), _recent[0].name, age);
    } else {
      buf[0] = 0;
    }
    if (drawMarquee(d, 0, by + TEXT_DY, buf, max_chars, _since, true)) scrolling = true;
    return scrolling;
  }

  // three rows below the status row
  int rowY(int r) const { return LH + r * (bottomY() - LH) / 2; }

  void renderRecent(DisplayDriver& d) {
    int w = cols(d) * CW;
    int n = the_mesh.getRecentlyHeard(_recent, 3);
    char age[12];
    int row = 0;
    for (int i = 0; i < n; i++) {
      if (_recent[i].name[0] == 0) continue;
      ageOf(age, sizeof(age), _recent[i].recv_timestamp);
      int y = rowY(row++) + TEXT_DY;
      int age_chars = utf8Len(age);
      drawMarquee(d, 0, y, _recent[i].name, w / CW - (age_chars ? age_chars + 1 : 0), 0, false);
      d.drawTextRightAlign(w, y, age);
    }
    if (row == 0) drawLines(d, LH, 3, T(S_NOBODY), 2);
  }

  void renderRadio(DisplayDriver& d) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%.3f", _prefs->freq);
    d.setCursor(0, rowY(0) + TEXT_DY); d.print(buf);
    snprintf(buf, sizeof(buf), "SF%d BW%d", _prefs->sf, (int)(_prefs->bw + 0.5f));
    d.setCursor(0, rowY(1) + TEXT_DY); d.print(buf);
    if (rx_any) snprintf(buf, sizeof(buf), "SNR %.1f", rx_snr);
    else snprintf(buf, sizeof(buf), "TX %ddBm", _prefs->tx_power_dbm);
    d.setCursor(0, rowY(2) + TEXT_DY); d.print(buf);
  }

  void renderBattery(DisplayDriver& d) {
    char buf[40];
    int w = cols(d) * CW;
    uint16_t mv = _task->battMilliVolts();
    snprintf(buf, sizeof(buf), "%d%%", battPercent(mv));
    d.setTextSize(4);
    d.drawTextCentered(w / 2, LH, buf);
    big(d);
    snprintf(buf, sizeof(buf), "%d.%02d V", mv / 1000, (mv % 1000) / 10);
    d.drawTextCentered(w / 2, bottomY() + TEXT_DY, buf);
  }

#if ENV_INCLUDE_GPS == 1
  void renderGPS(DisplayDriver& d) {
    char buf[40];
    LocationProvider* nmea = sensors.getLocationProvider();
    if (nmea == NULL || !_task->getGPSState()) {
      drawLines(d, LH, 3, nmea == NULL ? T(S_NO_GPS) : T(S_GPS_OFF), 2);
      return;
    }
    snprintf(buf, sizeof(buf), T(S_SAT_FMT), (int)nmea->satellitesCount());
    if (!nmea->isValid()) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s", T(S_NOFIX));
    d.setCursor(0, rowY(0) + TEXT_DY); d.print(buf);
    snprintf(buf, sizeof(buf), "%.5f", nmea->getLatitude() / 1000000.);
    d.setCursor(0, rowY(1) + TEXT_DY); d.print(buf);
    snprintf(buf, sizeof(buf), "%.5f", nmea->getLongitude() / 1000000.);
    d.setCursor(0, rowY(2) + TEXT_DY); d.print(buf);
  }
#endif

public:
  StandbyScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) { _since = millis(); }

  void showSummary() { _card = SUMMARY; _since = millis(); }

  int render(DisplayDriver& d) override {
    big(d);
    renderHeader(d);
    d.setColor(UIColor::primary_txt);
    bool scrolling = false;
    switch (_card) {
      case SUMMARY: scrolling = renderSummary(d); break;
      case RECENT: renderRecent(d); break;
      case RADIO: renderRadio(d); break;
      case BATTERY: renderBattery(d); break;
#if ENV_INCLUDE_GPS == 1
      case GPS: renderGPS(d); break;
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
      break;
    }
    return true;
  }
  if (c == KEY_CONTEXT_MENU) { quick_menu->reset(); _task->push(quick_menu); return true; }
  return false;   // Back: UITask turns the screen off
}

// ---------------------------------------------------------------- network tools: trace, nearby, airtime, range test

// a short name for a 1-byte repeater hash: the one repeater contact whose key starts with it, else hex
static void hopName(uint8_t hash, char* buf, size_t n) {
  ContactInfo c;
  char name[32];
  int hits = 0;
  int total = the_mesh.getNumContacts() + MAX_ANON_CONTACTS;   // indices include the anonymous slots
  for (int i = MAX_ANON_CONTACTS; i < total && hits < 2; i++) {
    if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_REPEATER && c.id.pub_key[0] == hash) {
      if (++hits == 1) StrHelper::strncpy(name, c.name, sizeof(name));
    }
  }
  if (hits == 1) snprintf(buf, n, "%s", name);
  else snprintf(buf, n, "%02X", hash);
}

struct TraceTarget {
  char name[32];
  uint8_t hashes[MAX_PATH_SIZE];   // 1-byte repeater hashes, there and back
  uint8_t n;
};

// The round trip to a contact through the repeaters on its path. A TRACE is only passed on by
// repeaters, so it goes to the repeater itself (or the last repeater before a chat contact) and
// back the same way. false = nothing to trace: a chat contact heard directly, or no path known.
static bool traceTargetFor(const ContactInfo& c, TraceTarget& t) {
  memset(&t, 0, sizeof(t));
  StrHelper::strncpy(t.name, c.name, sizeof(t.name));
  bool known = c.out_path_len != OUT_PATH_UNKNOWN;
  int k = known ? (c.out_path_len & 63) : 0;
  int sz = known ? (c.out_path_len >> 6) + 1 : 1;
  int n = 0;
  if (c.type == ADV_TYPE_REPEATER) {   // no known path: try it as a neighbour
    if (2 * k + 1 > MAX_PATH_SIZE) return false;
    for (int i = 0; i < k; i++) t.hashes[n++] = c.out_path[i * sz];
    t.hashes[n++] = c.id.pub_key[0];
    for (int i = k - 1; i >= 0; i--) t.hashes[n++] = c.out_path[i * sz];
  } else {
    if (k == 0 || 2 * k - 1 > MAX_PATH_SIZE) return false;
    for (int i = 0; i < k; i++) t.hashes[n++] = c.out_path[i * sz];
    for (int i = k - 2; i >= 0; i--) t.hashes[n++] = c.out_path[i * sz];
  }
  t.n = n;
  return true;
}

static void traceTargetForNode(const uint8_t* prefix, const char* name, TraceTarget& t) {   // a neighbour
  memset(&t, 0, sizeof(t));
  StrHelper::strncpy(t.name, name, sizeof(t.name));
  t.hashes[0] = prefix[0];
  t.n = 1;
}

#define TRACE_MIN_WAIT_MILLIS 4000

// one trace at a time, shown by TraceScreen
static struct {
  TraceTarget target;
  uint32_t tag;
  unsigned long sent_at, deadline, rtt;
  bool waiting, done, ok;
  int8_t snrs[MAX_PATH_SIZE];   // x 4, as each hop heard the one before
  int8_t final_snr;             // x 4, as this device heard the last hop
  uint8_t n;
} trace_run;

static bool startTrace(const TraceTarget& t) {
  trace_run.target = t;
  trace_run.waiting = trace_run.done = trace_run.ok = false;
  uint32_t timeout = 0;
  if (!the_mesh.sendTrace(t.hashes, t.n, trace_run.tag, timeout)) return false;
  trace_run.sent_at = millis();
  trace_run.deadline = millis() + (timeout < TRACE_MIN_WAIT_MILLIS ? TRACE_MIN_WAIT_MILLIS : timeout) + 2000;
  trace_run.waiting = true;
  return true;
}

// range test: a trace every few seconds, a buzz for each answer; it runs on with the screen off
#define RANGE_RESULTS 8
static const uint16_t RANGE_INTERVALS[] = { 15, 30, 60, 120 };
static const uint16_t HAPTIC_RANGE_MISS[] = { 60, 120, 60 };

struct RangeResult { uint32_t ts; bool ok; int8_t out4, back4; };

static struct {
  TraceTarget target;
  bool has_target, running, waiting;
  uint8_t interval_idx;
  unsigned long next_at, deadline;
  uint32_t tag;
  RangeResult res[RANGE_RESULTS];   // newest at head
  uint8_t head, count;
  uint16_t sent, ok;
} range_test;

static void rangeRecord(UITask* task, bool ok, int8_t out4, int8_t back4) {
  range_test.waiting = false;
  range_test.head = (range_test.head + 1) % RANGE_RESULTS;
  if (range_test.count < RANGE_RESULTS) range_test.count++;
  RangeResult& r = range_test.res[range_test.head];
  r.ts = rtc_clock.getCurrentTime();
  r.ok = ok;
  r.out4 = out4;
  r.back4 = back4;
  if (ok) range_test.ok++;
  if (!task->hapticBusy()) {   // a message alert that is playing wins
    if (ok) task->haptic(UI_HAPTIC_ACK_MS);
    else task->hapticPattern(HAPTIC_RANGE_MISS, sizeof(HAPTIC_RANGE_MISS) / sizeof(HAPTIC_RANGE_MISS[0]));
  }
  task->refresh();
}

static bool rangeRunning() { return range_test.running; }

static void rangeStart() {
  range_test.running = true;
  range_test.waiting = false;
  range_test.head = range_test.count = 0;
  range_test.sent = range_test.ok = 0;
  range_test.next_at = millis();
}

static void rangePoll(UITask* task) {
  if (!range_test.running) return;
  if (!the_mesh.isTxAllowed()) {   // TX switched off (Settings, antenna): nothing would go out
    range_test.running = range_test.waiting = false;
    task->showAlert(T(S_TX_IS_OFF), 1500);
    task->refresh();
    return;
  }
  if (range_test.waiting && timeReached(range_test.deadline)) rangeRecord(task, false, 0, 0);
  if (!range_test.waiting && timeReached(range_test.next_at)) {
    unsigned long every = RANGE_INTERVALS[range_test.interval_idx % 4] * 1000UL;
    range_test.next_at = millis() + every;
    range_test.sent++;
    uint32_t timeout = 0;
    if (the_mesh.sendTrace(range_test.target.hashes, range_test.target.n, range_test.tag, timeout)) {
      unsigned long wait = (timeout < TRACE_MIN_WAIT_MILLIS ? TRACE_MIN_WAIT_MILLIS : timeout) + 2000;
      if (wait > every - 500) wait = every - 500;
      range_test.deadline = millis() + wait;
      range_test.waiting = true;
    } else {
      rangeRecord(task, false, 0, 0);   // nothing went out (TX off)
    }
  }
}

// nearby repeaters: a zero-hop discover request, the answers of the next seconds
#define NEARBY_MAX 16
#define NEARBY_LISTEN_MILLIS 10000

struct NearbyNode { uint8_t prefix[8]; int8_t snr_in4, snr_out4; char name[32]; };

static struct {
  NearbyNode nodes[NEARBY_MAX];   // in arrival order while listening (rows stay put), then strongest first
  int n;
  bool sorted;
  uint32_t tag;
  unsigned long until;
  bool sent;
} nearby;

static void nearbySearch() {
  nearby.n = 0;
  nearby.sorted = false;
  nearby.sent = the_mesh.sendNodeDiscover(nearby.tag);
  nearby.until = millis() + NEARBY_LISTEN_MILLIS;
}

static bool nearbyListening() { return nearby.sent && !timeReached(nearby.until); }

static void nearbySortWhenDone() {   // strongest first, once no more answers come
  if (nearby.sorted || nearbyListening()) return;
  for (int i = 1; i < nearby.n; i++) {
    NearbyNode t = nearby.nodes[i];
    int j = i;
    while (j > 0 && nearby.nodes[j - 1].snr_in4 < t.snr_in4) { nearby.nodes[j] = nearby.nodes[j - 1]; j--; }
    nearby.nodes[j] = t;
  }
  nearby.sorted = true;
}

void UITask::onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  int hops = path_len >> (flags & 3);
  if (trace_run.waiting && tag == trace_run.tag) {
    trace_run.waiting = false;
    trace_run.done = trace_run.ok = true;
    trace_run.rtt = millis() - trace_run.sent_at;
    trace_run.n = hops < MAX_PATH_SIZE ? hops : MAX_PATH_SIZE;
    memcpy(trace_run.snrs, path_snrs, trace_run.n);
    trace_run.final_snr = pkt->_snr;
    haptic(UI_HAPTIC_ACK_MS);
    _next_refresh = 0;
  }
  if (range_test.running && range_test.waiting && tag == range_test.tag) {
    rangeRecord(this, true, hops > 0 ? (int8_t)path_snrs[0] : 0, pkt->_snr);
  }
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
  if (pkt->payload_len < 14 || (pkt->payload[0] & 0xF0) != CTL_TYPE_NODE_DISCOVER_RESP) return;
  uint32_t tag;
  memcpy(&tag, &pkt->payload[2], 4);
  if (!nearbyListening() || tag != nearby.tag) return;
  const uint8_t* prefix = &pkt->payload[6];
  for (int i = 0; i < nearby.n; i++) if (memcmp(nearby.nodes[i].prefix, prefix, 8) == 0) return;   // twice
  NearbyNode node;
  memcpy(node.prefix, prefix, 8);
  node.snr_in4 = pkt->_snr;
  node.snr_out4 = (int8_t)pkt->payload[1];
  ContactInfo* c = the_mesh.lookupContactByPubKey(prefix, 8);
  if (c) StrHelper::strncpy(node.name, c->name, sizeof(node.name));
  else snprintf(node.name, sizeof(node.name), "%02X%02X", prefix[0], prefix[1]);   // short: the SNR must fit
  int at = nearby.n;
  if (at == NEARBY_MAX) {   // full: the weakest goes, unless this one is weaker still
    at = 0;
    for (int i = 1; i < NEARBY_MAX; i++) if (nearby.nodes[i].snr_in4 < nearby.nodes[at].snr_in4) at = i;
    if (nearby.nodes[at].snr_in4 >= node.snr_in4) return;
  } else {
    nearby.n++;
  }
  nearby.nodes[at] = node;
  _next_refresh = 0;
}

// ---- trace result: each hop and how well it heard the one before, then this device

class TraceScreen : public ListScreen {
  char _lines[MAX_PATH_SIZE + 2][40];
  char _right[MAX_PATH_SIZE + 2][8];   // SNR of a hop, on the right so a long name cannot hide it
  int _n = 0;
  void build() {
    _n = 0;
    memset(_right, 0, sizeof(_right));
    if (trace_run.waiting) {
      snprintf(_lines[_n++], 40, T(S_TRACE_WAIT_FMT), (millis() - trace_run.sent_at) / 1000);
      snprintf(_lines[_n++], 40, T(S_HOPS_FMT), trace_run.target.n);
      return;
    }
    if (!trace_run.ok) {
      snprintf(_lines[_n++], 40, "%s", trace_run.done ? T(S_TRACE_NONE) : failText(S_SEND_FAIL));
      return;
    }
    char name[32];
    for (int i = 0; i < trace_run.n && i < trace_run.target.n && _n < MAX_PATH_SIZE; i++) {
      hopName(trace_run.target.hashes[i], name, sizeof(name));
      snprintf(_right[_n], 8, "%+.1f", trace_run.snrs[i] / 4.0f);
      snprintf(_lines[_n++], 40, "%s", name);
    }
    snprintf(_right[_n], 8, "%+.1f", trace_run.final_snr / 4.0f);
    snprintf(_lines[_n++], 40, "%s", T(S_ME));
    unsigned long tenths = (trace_run.rtt + 50) / 100;
    snprintf(_lines[_n++], 40, T(S_MS_FMT), tenths / 10, tenths % 10);
  }
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", trace_run.target.name); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _lines[i]); }
  void rightText(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _right[i]); }
  bool rightFirst() override { return true; }
  bool onEnter(int i) override {   // again
    if (trace_run.waiting) return true;
    if (!startTrace(trace_run.target)) _task->showAlert(failText(S_SEND_FAIL), 1500);
    reset();
    return true;
  }
public:
  TraceScreen(UITask* task) : ListScreen(task) { }
  int render(DisplayDriver& d) override {
    build();
    int next = ListScreen::render(d);
    return trace_run.waiting && next > 1000 ? 1000 : next;
  }
};

static TraceScreen* trace_screen;

// the deadline also passes with the trace screen closed: a late answer then buzzes no more
static void tracePoll(UITask* task) {
  if (trace_run.waiting && timeReached(trace_run.deadline)) {
    trace_run.waiting = false;
    trace_run.done = true;
    task->refresh();
  }
}

static void openTrace(UITask* task, const TraceTarget& t) {
  if (!startTrace(t)) {
    task->showAlert(failText(S_SEND_FAIL), 1500);
    return;
  }
  trace_screen->reset();
  task->push(trace_screen);
}

static void traceContact(UITask* task, const ContactInfo& c) {
  TraceTarget t;
  if (!traceTargetFor(c, t)) { task->showAlert(T(S_TRACE_NO_PATH), 1500); return; }
  openTrace(task, t);
}

// ---- range test

class RangeScreen;
static RangeScreen* range_screen;
static void openTargetPick(UITask* task);

class RangeScreen : public ListScreen {
  enum { R_TARGET, R_RUN, R_EVERY, R_STATS, R_FIRST_RESULT };
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_RANGE)); }
  int count() override { return R_FIRST_RESULT + range_test.count; }
  void label(int i, char* buf, size_t n) override {
    switch (i) {
      case R_TARGET:
        snprintf(buf, n, "%s", range_test.has_target ? range_test.target.name : T(S_PICK_TARGET));
        break;
      case R_RUN: snprintf(buf, n, "%s", T(range_test.running ? S_STOP : S_START)); break;
      case R_EVERY: snprintf(buf, n, T(S_EVERY_FMT), RANGE_INTERVALS[range_test.interval_idx % 4]); break;
      case R_STATS: snprintf(buf, n, T(S_OK_OF_FMT), range_test.ok, range_test.sent); break;
      default: {
        const RangeResult& r = range_test.res[(range_test.head - (i - R_FIRST_RESULT) + RANGE_RESULTS) % RANGE_RESULTS];
        char when[8] = "";
        if (r.ts >= VALID_EPOCH) {
          uint32_t local = ui::localTime(r.ts, _task->prefs()->tz_offset, _task->prefs()->ui_dst);
          snprintf(when, sizeof(when), "%02lu:%02lu", (unsigned long)((local / 3600) % 24), (unsigned long)((local / 60) % 60));
        }
        if (r.ok) snprintf(buf, n, "%s %+d/%+d", when, (r.out4 + (r.out4 >= 0 ? 2 : -2)) / 4, (r.back4 + (r.back4 >= 0 ? 2 : -2)) / 4);
        else snprintf(buf, n, "%s --", when);
      }
    }
  }
  bool onEnter(int i) override {
    switch (i) {
      case R_TARGET: openTargetPick(_task); break;
      case R_RUN:
        if (range_test.running) { range_test.running = false; range_test.waiting = false; }
        else if (!range_test.has_target) openTargetPick(_task);
        else if (!the_mesh.isTxAllowed()) _task->showAlert(T(S_TX_IS_OFF), 1500);
        else rangeStart();
        _task->haptic(UI_HAPTIC_ACK_MS);
        break;
      case R_EVERY: range_test.interval_idx = (range_test.interval_idx + 1) % 4; break;
      default: break;
    }
    return true;
  }
public:
  RangeScreen(UITask* task) : ListScreen(task) { }
  int render(DisplayDriver& d) override {
    int next = ListScreen::render(d);
    return range_test.running && next > 1000 ? 1000 : next;
  }
};

static void setRangeTarget(UITask* task, const TraceTarget& t) {
  range_test.running = range_test.waiting = false;
  range_test.target = t;
  range_test.has_target = true;
  range_test.head = range_test.count = 0;
  range_test.sent = range_test.ok = 0;
  if (task->current() != range_screen) { range_screen->reset(); task->push(range_screen); }
}

// what a trace or range test can go to: repeaters, then chat contacts reached through repeaters,
// most recently heard first
#define PICK_MAX 24

class TargetPickScreen : public ListScreen {
  struct Item { uint8_t prefix[6]; uint32_t lastmod; bool rpt; };
  Item* _items = NULL;
  int _n = 0;
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_PICK_TARGET)); }
  int count() override { return _n; }
  const char* emptyText() override { return T(S_NO_TARGETS); }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override { return _items[i].rpt ? icon_tower : icon_person; }
  void label(int i, char* buf, size_t n) override {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_items[i].prefix, 6);
    snprintf(buf, n, "%s", c ? c->name : "?");
  }
  bool onEnter(int i) override {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_items[i].prefix, 6);
    TraceTarget t;
    if (!c || !traceTargetFor(*c, t)) return true;
    _task->pop();
    setRangeTarget(_task, t);
    return true;
  }
public:
  TargetPickScreen(UITask* task) : ListScreen(task) { }
  void open() {
    if (_items == NULL) _items = new Item[PICK_MAX];
    _n = 0;
    ContactInfo c;
    TraceTarget t;
    int total = the_mesh.getNumContacts() + MAX_ANON_CONTACTS;
    for (int i = MAX_ANON_CONTACTS; i < total; i++) {
      if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE || !traceTargetFor(c, t)) continue;
      Item it;
      memcpy(it.prefix, c.id.pub_key, sizeof(it.prefix));
      it.lastmod = c.lastmod;
      it.rpt = c.type == ADV_TYPE_REPEATER;
      // keep the PICK_MAX best: repeaters first, then the most recently heard
      int at = _n < PICK_MAX ? _n++ : PICK_MAX;
      while (at > 0 && (it.rpt > _items[at - 1].rpt || (it.rpt == _items[at - 1].rpt && it.lastmod > _items[at - 1].lastmod))) {
        if (at < PICK_MAX) _items[at] = _items[at - 1];
        at--;
      }
      if (at < PICK_MAX) _items[at] = it;
    }
    reset();
  }
};

static TargetPickScreen* target_pick;

static void openTargetPick(UITask* task) {
  target_pick->open();
  task->push(target_pick);
}

// ---- nearby repeaters

class NodeScreen;
static NodeScreen* node_screen;

class NearbyScreen : public ListScreen {
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override {
    if (nearby.n > 0) snprintf(buf, n, "%s %d", T(S_NEARBY), nearby.n);
    else snprintf(buf, n, "%s", T(S_NEARBY));
  }
  int count() override { return nearby.n; }
  const char* emptyText() override {
    if (!nearby.sent) return T(S_TX_IS_OFF);
    return nearbyListening() ? T(S_SEARCHING) : T(S_NO_ANSWER);
  }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override { return icon_tower; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", nearby.nodes[i].name); }
  void rightText(int i, char* buf, size_t n) override {
    int v = nearby.nodes[i].snr_in4;
    snprintf(buf, n, "%+d", (v + (v >= 0 ? 2 : -2)) / 4);
  }
  bool rightFirst() override { return true; }
  bool onEnter(int i) override;
  bool onContext(int i) override {   // hold: search again
    if (nearbyListening()) return true;
    nearbySearch();
    reset();
    return true;
  }
public:
  NearbyScreen(UITask* task) : ListScreen(task) { }
  void open() {
    if (!nearbyListening()) nearbySearch();
    reset();
  }
  int render(DisplayDriver& d) override {
    if (!nearby.sorted && !nearbyListening() && _sel < nearby.n) {   // keep the cursor on its repeater
      uint8_t prefix[8];
      memcpy(prefix, nearby.nodes[_sel].prefix, sizeof(prefix));
      nearbySortWhenDone();
      for (int i = 0; i < nearby.n; i++) if (memcmp(nearby.nodes[i].prefix, prefix, sizeof(prefix)) == 0) select(i);
    }
    nearbySortWhenDone();
    int next = ListScreen::render(d);
    return nearbyListening() && next > 500 ? 500 : next;
  }
};

static NearbyScreen* nearby_screen;

// one nearby repeater: both directions of the link, then trace or range test
class NodeScreen : public ListScreen {
  NearbyNode _node;
  char _lines[3][40];
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", _node.name); }
  int count() override { return 5; }
  void label(int i, char* buf, size_t n) override {
    if (i < 3) snprintf(buf, n, "%s", _lines[i]);
    else snprintf(buf, n, "%s", T(i == 3 ? S_TRACE : S_RANGE));
  }
  bool hasIcons() override { return true; }
  const uint8_t* icon(int i) override { return i == 0 ? icon_up : i == 1 ? icon_down : NULL; }
  bool onEnter(int i) override {
    if (i < 3) return true;
    TraceTarget t;
    traceTargetForNode(_node.prefix, _node.name, t);
    if (i == 3) openTrace(_task, t);
    else setRangeTarget(_task, t);
    return true;
  }
public:
  NodeScreen(UITask* task) : ListScreen(task) { }
  void open(const NearbyNode& node) {
    _node = node;
    // arrows: up = how it hears us, down = how we hear it
    snprintf(_lines[0], 40, "%+.1f", node.snr_out4 / 4.0f);
    snprintf(_lines[1], 40, "%+.1f", node.snr_in4 / 4.0f);
    char hex[20];
    snprintf(hex, sizeof(hex), "%02X%02X%02X", node.prefix[0], node.prefix[1], node.prefix[2]);
    snprintf(_lines[2], 40, T(S_ID_FMT), hex);
    reset();
  }
};

bool NearbyScreen::onEnter(int i) {
  node_screen->open(nearby.nodes[i]);
  _task->push(node_screen);
  return true;
}

// ---- airtime and radio counters, live

class AirtimeScreen : public ListScreen {
  char _lines[9][40];
  int _n = 0;
  void build() {
    _n = 0;
    uint32_t win = _task->airtimeWindowSecs();
    uint32_t tx = _task->txAirtimeLastHour();
    uint32_t permille = win > 0 ? (uint32_t)((uint64_t)tx / win) : 0;   // ms per s = 1/1000
    snprintf(_lines[_n++], 40, T(S_AIR_HOUR_FMT), (int)(permille / 10), (int)(permille % 10));
    char total[12];
    formatAge(total, sizeof(total), the_mesh.getTotalAirTime() / 1000);
    snprintf(_lines[_n++], 40, T(S_AIR_TOTAL_FMT), total);
    snprintf(_lines[_n++], 40, T(S_TXN_FMT), (unsigned long)radio_driver.getPacketsSent());
    snprintf(_lines[_n++], 40, T(S_RX_FMT), (unsigned long)radio_driver.getPacketsRecv());
    snprintf(_lines[_n++], 40, T(S_ERR_FMT), (unsigned long)radio_driver.getPacketsRecvErrors());
    snprintf(_lines[_n++], 40, T(S_NOISE_FMT), radio_driver.getNoiseFloor());
    if (rx_any) {
      snprintf(_lines[_n++], 40, T(S_SNR_FMT), rx_snr);
      snprintf(_lines[_n++], 40, T(S_RSSI_FMT), (int)radio_driver.getLastRSSI());
    }
  }
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_AIRTIME)); }
  int count() override { return _n; }
  void label(int i, char* buf, size_t n) override { snprintf(buf, n, "%s", _lines[i]); }
public:
  AirtimeScreen(UITask* task) : ListScreen(task) { }
  int render(DisplayDriver& d) override { build(); return ListScreen::render(d); }
  void open() { build(); reset(); }
};

static AirtimeScreen* airtime_screen;

class NetworkMenuScreen : public ListScreen {
protected:
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_NETWORK)); }
  int count() override { return 3; }
  void label(int i, char* buf, size_t n) override {
    static const StrId L[] = { S_NEARBY, S_AIRTIME, S_RANGE };
    snprintf(buf, n, "%s", T(L[i]));
  }
  bool onEnter(int i) override {
    if (i == 0) { nearby_screen->open(); _task->push(nearby_screen); }
    else if (i == 1) { airtime_screen->open(); _task->push(airtime_screen); }
    else { range_screen->reset(); _task->push(range_screen); }
    return true;
  }
public:
  NetworkMenuScreen(UITask* task) : ListScreen(task) { }
};


void UITask::sampleAirtime() {
  _air_head = (_air_head + 1) % 61;
  if (_air_count < 61) _air_count++;
  _air_total[_air_head] = the_mesh.getTotalAirTime();
  _air_at[_air_head] = millis();
}

uint32_t UITask::txAirtimeLastHour() {
  if (_air_count == 0) return the_mesh.getTotalAirTime();
  int oldest = (_air_head - (_air_count - 1) + 61) % 61;
  return the_mesh.getTotalAirTime() - _air_total[oldest];
}

uint32_t UITask::airtimeWindowSecs() {
  if (_air_count == 0) return millis() / 1000;
  int oldest = (_air_head - (_air_count - 1) + 61) % 61;
  return (millis() - _air_at[oldest]) / 1000;
}

// ---------------------------------------------------------------- hardware check (Settings > System)

static bool buttonsFitted() { return rotary_input.mode() == RotaryInputMode::Buttons; }
static StrId inputLabel() { return buttonsFitted() ? S_BUTTONS : S_ENCODER; }

class EncoderTestScreen : public UIScreen {
  UITask* _task;
  int32_t _base = 0;
  int _presses = 0, _holds = 0;
public:
  EncoderTestScreen(UITask* task) : _task(task) { }
  void open() { _base = _task->rawDetents(); _presses = _holds = 0; }
  int render(DisplayDriver& d) override {
    big(d);
    d.setColor(UIColor::title_txt);
    d.drawTextCentered(d.width() / 2, TEXT_DY, T(inputLabel()));
    d.setColor(UIColor::primary_txt);
    char buf[32];
    snprintf(buf, sizeof(buf), T(S_TURNS_FMT), (int)(_task->rawDetents() - _base));
    d.setCursor(0, LH + TEXT_DY); d.print(buf);
    snprintf(buf, sizeof(buf), T(S_PRESSES_FMT), _presses);
    d.setCursor(0, 2 * LH + TEXT_DY); d.print(buf);
    snprintf(buf, sizeof(buf), T(S_HOLDS_FMT), _holds);
    d.setCursor(0, 3 * LH + TEXT_DY); d.print(buf);
    return 200;
  }
  bool handleInput(char c) override {
    if (c == KEY_ENTER) _presses++;
    else if (c == KEY_CONTEXT_MENU) _holds++;
    else if (c == KEY_CANCEL) return false;   // leave
    return true;   // turns are counted raw, without the acceleration
  }
};

// all pixels on, a fine checkerboard, a frame with a cross: turn for the next one, Back leaves
class DisplayTestScreen : public UIScreen {
  int _pattern = 0;
  unsigned long _hint_until = 0;   // "Turn" over the first pattern, so it is clear how to go on
public:
  void open() { _pattern = 0; _hint_until = millis() + 2000; }
  int render(DisplayDriver& d) override {
    d.setColor(UIColor::primary_txt);
    int w = d.width(), h = d.height();
    if (_pattern == 0) {
      d.fillRect(0, 0, w, h);
      if (_hint_until && !timeReached(_hint_until)) {
        big(d);
        d.setColor(COLOR_BLACK);
        d.drawTextCentered(w / 2, (h - LH) / 2 + TEXT_DY, T(buttonsFitted() ? S_PRESS_HINT : S_TURN_HINT));
        d.setColor(UIColor::primary_txt);
        return 300;
      }
      _hint_until = 0;
    } else if (_pattern == 1) {
      for (int y = 0; y < h; y++) for (int x = (y & 1); x < w; x += 2) d.fillRect(x, y, 1, 1);
    } else {
      d.drawRect(0, 0, w, h);
      for (int i = 0; i < w; i++) { d.fillRect(i, i * h / w, 1, 1); d.fillRect(i, h - 1 - i * h / w, 1, 1); }
    }
    return 5000;
  }
  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_PREV) _hint_until = 0;
    if (c == KEY_NEXT) _pattern = (_pattern + 1) % 3;
    else if (c == KEY_PREV) _pattern = (_pattern + 2) % 3;
    else if (c == KEY_CANCEL) return false;
    return true;
  }
};

static EncoderTestScreen* encoder_test;
static DisplayTestScreen* display_test;

// Settings > System > Hardware: the three tests, then live readings
class HardwareScreen : public ListScreen {
  enum { H_ENCODER, H_VIBRATION, H_DISPLAY, H_FIRST_INFO };
  char _lines[8][40];
  int _n = 0;
  void build() {
    _n = 0;
    uint16_t mv = _task->battMilliVolts();
    snprintf(_lines[_n++], 40, T(S_BATT_FMT), mv / 1000, (mv % 1000) / 10, battPercent(mv));
    uint16_t raw = _task->getBattMilliVolts();
    snprintf(_lines[_n++], 40, T(S_RAW_FMT), raw / 1000, (raw % 1000) / 10);
    snprintf(_lines[_n++], 40, T(S_ADC_FMT), board.getAdcMultiplier());
    snprintf(_lines[_n++], 40, T(S_USB_FMT), T(board.isExternalPowered() ? S_YES : S_NO));
    snprintf(_lines[_n++], 40, T(S_RESET_FMT), board.getResetReasonString(board.getResetReason()));
    snprintf(_lines[_n++], 40, T(S_NOISE_FMT), radio_driver.getNoiseFloor());
    char age[12];
    formatAge(age, sizeof(age), millis() / 1000);
    snprintf(_lines[_n++], 40, T(S_UPTIME_FMT), age);
  }
protected:
  bool isContent() override { return true; }
  void title(char* buf, size_t n) override { snprintf(buf, n, "%s", T(S_HARDWARE)); }
  int count() override { return H_FIRST_INFO + _n; }
  void label(int i, char* buf, size_t n) override {
    if (i == H_ENCODER) snprintf(buf, n, "%s", T(inputLabel()));
    else if (i == H_VIBRATION) snprintf(buf, n, "%s", T(S_SET_VIBE));
    else if (i == H_DISPLAY) snprintf(buf, n, "%s", T(S_GRP_DISPLAY));
    else snprintf(buf, n, "%s", _lines[i - H_FIRST_INFO]);
  }
  bool onEnter(int i) override {
    if (i == H_ENCODER) { encoder_test->open(); _task->push(encoder_test); }
    else if (i == H_VIBRATION) _task->testVibration();
    else if (i == H_DISPLAY) { display_test->open(); _task->push(display_test); }
    return true;
  }
public:
  HardwareScreen(UITask* task) : ListScreen(task) { }
  int render(DisplayDriver& d) override { build(); return ListScreen::render(d); }
  void open() { build(); reset(); }
};

static HardwareScreen* hardware_screen;

static void openHardware(UITask* task) {
  hardware_screen->open();
  task->push(hardware_screen);
}

void UITask::testVibration() {
#ifdef PIN_VIBRATION
  vibration.playPattern(HAPTIC_DIRECT, sizeof(HAPTIC_DIRECT) / sizeof(HAPTIC_DIRECT[0]));
#endif
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
  // the loop sleeps until an interrupt: a pin change wakes it, and the first edge counts
  user_btn.setDebounce(15, true);
  user_btn.wakeOnChange();
  encoder_btn.begin();
  encoder_btn.setDebounce(15, true);
  encoder_btn.wakeOnChange();
  rotary_input.begin();
  applyEncoderPrefs();
  // encoder or two buttons on its pins: remembered, but found out again after a battery was put
  // in (the hardware may have changed); until then both work and the first press or turn decides
  if (coldBoot() || (_node_prefs->ui_input != 1 && _node_prefs->ui_input != 2)) {
    rotary_input.setMode(RotaryInputMode::Detect);
  } else {
    rotary_input.setMode(_node_prefs->ui_input == 2 ? RotaryInputMode::Buttons : RotaryInputMode::Encoder);
  }

  if (_display != NULL) {
    ui_width = _display->width();
    ui_height = _display->height();
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
  message_actions = new MessageActionsScreen(this);
  message_info = new MessageInfoScreen(this);
  contacts_screen = new ContactsScreen(this);
  contact_actions = new ContactActionsScreen(this);
  contact_info = new ContactInfoScreen(this);
  channels_screen = new ChannelsScreen(this);
  channel_actions = new ChannelActionsScreen(this);
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
  network_menu = new NetworkMenuScreen(this);
  nearby_screen = new NearbyScreen(this);
  node_screen = new NodeScreen(this);
  airtime_screen = new AirtimeScreen(this);
  trace_screen = new TraceScreen(this);
  range_screen = new RangeScreen(this);
  target_pick = new TargetPickScreen(this);
  hardware_screen = new HardwareScreen(this);
  encoder_test = new EncoderTestScreen(this);
  display_test = new DisplayTestScreen();
  range_test.interval_idx = 1;   // 30 s
#ifdef UI_ASK_ANTENNA
  {
    uint8_t mode = _node_prefs->ui_ant % 3;
    bool ask = _display != NULL;   // no OLED found: nobody could answer, keep the radio usable
    bool tx = true;
    if (mode == ANT_NEVER) ask = false;
    if (ask && mode == ANT_POWER_LOSS && !coldBoot() && _node_prefs->ui_tx_last != 0) {
      // a reset or a wake from hibernate, not a new battery: the antenna is as it was answered
      ask = false;
      tx = _node_prefs->ui_tx_last == 1;
    }
    if (!ask) the_mesh.setTxAllowed(tx);
    _ask_antenna = ask;
    _ant_remind = millis() + ANT_REMIND_MILLIS;
  }
#endif
  loadQuickReplies();
  loadMuted();
  loadLearnedWords();
  loadHistory();   // messages from before the last power off or reset
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

// ---- muted channels: the first 4 bytes of each one's key, in /ui_mute

#define MUTE_FILE "/ui_mute"

static uint32_t channelKey(int idx) {
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails ch;
  if (idx < 0 || !the_mesh.getChannel(idx, ch) || !ch.name[0]) return 0;
  uint32_t k;
  memcpy(&k, ch.channel.secret, sizeof(k));
  return k;
#else
  return 0;
#endif
}

void UITask::loadMuted() {
  int n = the_mesh.loadUIFile(MUTE_FILE, (uint8_t*)_muted, sizeof(_muted));
  _num_muted = n > 0 ? n / (int)sizeof(_muted[0]) : 0;
}

bool UITask::saveMuted() {
  if (_num_muted == 0) return the_mesh.saveUIFile(MUTE_FILE, (const uint8_t*)_muted, 0);
  return the_mesh.saveUIFile(MUTE_FILE, (const uint8_t*)_muted, _num_muted * sizeof(_muted[0]));
}

bool UITask::isChannelMuted(int idx) {
  uint32_t k = channelKey(idx);
  if (k == 0) return false;
  for (int i = 0; i < _num_muted; i++) if (_muted[i] == k) return true;
  return false;
}

bool UITask::setChannelMuted(int idx, bool muted) {
  uint32_t k = channelKey(idx);
  if (k == 0 || isChannelMuted(idx) == muted) return true;
  if (muted) {
    // keys of channels deleted or replaced from the phone stay behind: drop them first
    for (int i = 0; i < _num_muted; ) {
      bool used = false;
#ifdef MAX_GROUP_CHANNELS
      for (int c = 0; c < MAX_GROUP_CHANNELS && !used; c++) used = channelKey(c) == _muted[i];
#endif
      if (used) i++;
      else _muted[i] = _muted[--_num_muted];
    }
    if (_num_muted >= (int)(sizeof(_muted) / sizeof(_muted[0]))) return false;
    _muted[_num_muted++] = k;
  } else {
    for (int i = 0; i < _num_muted; i++) {
      if (_muted[i] == k) { _muted[i] = _muted[--_num_muted]; break; }
    }
  }
  return saveMuted();
}

void UITask::applyEncoderPrefs() {
  rotary_input.setReverse((bool)ENCODER_REVERSE != (_node_prefs->ui_enc_rev != 0));
  rotary_input.setStepsPerDetent(encoderSteps(_node_prefs));
}

void UITask::applyContrast() {
  if (_display == NULL) return;
  const BrightStep& s = BRIGHT_TABLE[brightLevel(_node_prefs) - 1];
  _display->setDim(s.dim);
  _display->setContrast(s.contrast);
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

// firmware update without the buttons at reset: save what hibernate saves, leave how-to on the
// screen (the bootloader does not touch the OLED) and restart into the bootloader
void UITask::enterBootloader(bool usb) {
  the_mesh.flushPendingWrites();
  saveHistory();
  the_mesh.saveOfflineQueue();
  if (_display != NULL) {
    _display->turnOn();
    _display->startFrame();
    big(*_display);
    _display->setColor(UIColor::primary_txt);
    drawLines(*_display, 0, 4, T(usb ? S_DFU_USB_L : S_DFU_BLE_L), 4);
    _display->endFrame();
  }
#ifdef PIN_VIBRATION
  vibration.stop();
#endif
  delay(200);
  _board->rebootToBootloader(usb);
  // still here: the board has no bootloader update mode; the saved files are read at the next start
  showAlert(T(S_DFU_FAIL), 1500);
}

void UITask::setTransmit(bool on) {
  the_mesh.setTxAllowed(on);
  uint8_t last = on ? 1 : 2;
  if (_node_prefs->ui_tx_last != last) {   // remembered for a restart that is not a battery change
    _node_prefs->ui_tx_last = last;
    the_mesh.savePrefs();
  }
}

void UITask::lock() {
  _locked = true;
  home();
  if (_display != NULL) { _display->turnOff(); _off_at = millis(); }
}

uint16_t UITask::battMilliVolts() {
  if (_batt_mv == 0) _batt_mv = getBattMilliVolts();
  return _batt_mv;
}

void UITask::answerAntenna(bool fitted) {
  _ask_antenna = false;
  setTransmit(fitted);
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

bool UITask::hapticBusy() {
#ifdef PIN_VIBRATION
  return vibration.isBusy();
#else
  return false;
#endif
}

void UITask::hapticPattern(const uint16_t* pattern, int n) {
#ifdef PIN_VIBRATION
  if (!_node_prefs->vibe_quiet) vibration.playPattern(pattern, n);
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
    if (m->state != MSG_OUT_PENDING && m->state != MSG_OUT_FAILED) continue;
    bool match = false;
    for (int k = 0; k < 4; k++) if (m->acks[k] != 0 && m->acks[k] == ack) match = true;   // any attempt
    if (match) {
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
      if (m->tries_left > 0) {   // try again with the same timestamp, the last time by flood
        m->tries_left--;
        if (sendAttempt(m)) continue;
      }
      m->state = MSG_OUT_FAILED;
#ifdef PIN_VIBRATION
      if (!_node_prefs->vibe_quiet) vibration.playPattern(HAPTIC_FAILED, sizeof(HAPTIC_FAILED) / sizeof(HAPTIC_FAILED[0]));
#endif
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

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_SIGNED_PLAIN) return;   // not CLI data
  msgSource(false, 0, from.id.pub_key);
  _next_snr4 = pkt->_snr;
  _next_rssi = (int16_t)radio_driver.getLastRSSI();
  newMsg(pkt->isRouteFlood() ? pkt->path_len : 0xFF, from.name, text, _msgcount);
  notifyMsg(UIEventType::contactMessage, hasConnection());
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  int idx = the_mesh.findChannelIdx(channel_details.channel);
  bool muted = idx >= 0 && isChannelMuted(idx);
  msgSource(true, idx < 0 ? 0 : idx, NULL);
  _next_muted = muted;
  _next_snr4 = pkt->_snr;
  _next_rssi = (int16_t)radio_driver.getLastRSSI();
  bool mention = isMention(text);   // newMsg() gives a mention the direct message buzz: keep it
  newMsg(pkt->isRouteFlood() ? pkt->path_len : 0xFF, channel_details.name, text, _msgcount);
  if (!muted && !mention) notifyMsg(UIEventType::channelMessage, hasConnection());
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
                            const char* text, uint32_t sent_ts, uint32_t ack, uint32_t timeout_millis) {
  UIMsgEntry* m = addHistory();
  m->state = ack ? MSG_OUT_PENDING : MSG_OUT_SENT;
  m->sent_ts = sent_ts;
  m->attempt = 0;
  m->tries_left = ack ? UI_SEND_ATTEMPTS - 1 : 0;
  m->acks[0] = ack;
  StrHelper::strncpy(m->from, conv_name, sizeof(m->from));
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  m->src = src;
  m->channel_idx = channel_idx;
  if (pub_prefix) memcpy(m->pub_prefix, pub_prefix, sizeof(m->pub_prefix));
  m->ack_deadline = millis() + timeout_millis + 2000;   // the estimate plus a margin for the way back
  _next_refresh = 0;
  return m;
}

// one more send of a direct message in the history: same timestamp, next attempt number, so the
// recipient's app sees a repeat and not a new message. The last automatic one floods (path reset).
bool UITask::sendAttempt(UIMsgEntry* m) {
  if (m->src != MSG_SRC_CONTACT || !the_mesh.isTxAllowed()) return false;
  ContactInfo* c = the_mesh.lookupContactByPubKey(m->pub_prefix, sizeof(m->pub_prefix));
  if (!c) return false;
  if (m->tries_left == 0 && c->out_path_len != OUT_PATH_UNKNOWN) the_mesh.resetContactPath(*c);
  uint8_t attempt = m->attempt + 1;
  uint32_t ack = 0, timeout = 0;
  if (the_mesh.sendMessage(*c, m->sent_ts, attempt, m->text, ack, timeout) == MSG_SEND_FAILED) return false;
  if (ack) the_mesh.expectAck(ack, c);
  m->attempt = attempt;
  m->acks[attempt & 3] = ack;
  m->state = MSG_OUT_PENDING;
  m->ack_deadline = millis() + timeout + 2000;
  _next_refresh = 0;
  return true;
}

bool UITask::resend(uint32_t seq) {
  UIMsgEntry* m = historyAt(historyIndexOf(seq));
  if (!m || m->state == MSG_IN || m->src != MSG_SRC_CONTACT) return false;
  m->tries_left = UI_SEND_ATTEMPTS - 1;   // a new round, the first send on the known path
  return sendAttempt(m);
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
  m->snr4 = _next_snr4;
  m->rssi = _next_rssi;
  bool muted = _next_muted;
  _next_muted = false;
  _next_snr4 = 0;
  _next_rssi = 0;
  _last_new_msg = millis();

  // a channel message that names this node counts as a direct one: its buzz and waking the screen
  if (m->src == MSG_SRC_CHANNEL && isMention(text)) {
    m->mention = true;
    notifyMsg(UIEventType::contactMessage, hasConnection());   // replaces the channel buzz
  }
  bool direct = m->src != MSG_SRC_CHANNEL || m->mention;
  if (muted && !m->mention) direct = false;

  if (_display != NULL) {
    // Settings > Messages > Wake for: direct messages (default), all, never; never with a phone
    uint8_t wake = _node_prefs->ui_wake % 3;
    if (!_display->isOn() && !hasConnection() && (wake == WAKE_ALL || (wake == WAKE_DIRECT && direct)) &&
        !(muted && !m->mention)) {   // a muted channel only counts, unless it names this node
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

// "@name" or "@[name]" (the app's mention format) in the text, ASCII letters in any case, whole name only
bool UITask::isMention(const char* text) const {
  const char* name = _node_prefs->node_name;
  int nl = strlen(name);
  if (nl == 0) return false;
  for (const char* p = strchr(text, '@'); p; p = strchr(p + 1, '@')) {
    const char* q = p + 1;
    bool bracket = *q == '[';
    if (bracket) q++;
    int i = 0;
    while (i < nl && q[i] && tolower((unsigned char)q[i]) == tolower((unsigned char)name[i])) i++;
    if (i < nl) continue;
    // the whole name: "@Pet" must not match a node called "Petko", nor "@Petko" one called "Pet"
    char after = q[nl];
    if (bracket ? after == ']' : !(isalnum((unsigned char)after) || after == '_' || (after & 0x80))) return true;
  }
  return false;
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

void UITask::hibernate(bool low_battery) {
  // keep what would otherwise be lost: contacts waiting for their lazy save, the message history
  // and the messages the phone has not fetched yet (restored at the next start)
  the_mesh.flushPendingWrites();
  saveHistory();
  the_mesh.saveOfflineQueue();
#if defined(FAKETEC_V5) && !defined(UI_SIM)
  FaketecBoard::setQuietPowerHaptics(_node_prefs->vibe_quiet);   // also for the buzz at the next wake
#endif

  if (low_battery) {
    if (_display != NULL) {
      _display->turnOn();
      _display->startFrame();
      big(*_display);
      _display->setColor(UIColor::primary_txt);
      drawLines(*_display, 0, 3, T(S_CHARGE), 3);
      // how to switch on again: hold Back + encoder push ~1.5s (120ms buzz confirms)
      drawHint(*_display, 3 * LH, T(S_WAKE_HINT), NULL);
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
    while (millis() - shown < 3000) delay(10);   // long enough to read warning
    if (_display != NULL) _display->turnOff();
  } else if (_display != NULL) {
    // "Power off" stays up while the board buzzes, then the board turns the display off
    _display->turnOn();
    _display->startFrame();
    big(*_display);
    _display->setColor(UIColor::primary_txt);
    _display->drawTextCentered(_display->width() / 2, (_display->height() - LH) / 2 + TEXT_DY, T(S_POWER_OFF));
    _display->endFrame();
  }
  shutdown();
}

// ---- history kept over a power off or reset: written when it changed (at most every
// UI_HIST_SAVE_MILLIS, to spare the flash), at hibernate and before a firmware update; read at start

#ifndef UI_HIST_SAVE_MILLIS
  #define UI_HIST_SAVE_MILLIS  (10 * 60 * 1000UL)
#endif

#define HISTORY_FILE  "/ui_hist"
#define HISTORY_MAGIC 0x31484955UL   // "UIH1"

struct HistoryHeader {
  uint32_t magic;
  uint16_t entry_size;   // a build with another layout ignores the file
  uint8_t slots, count;
  int16_t head;
};
// the file is the header plus the ring buffer as it is in RAM (no copy, no extra buffer)

uint32_t UITask::historyHash() const {
  uint32_t h = 2166136261UL;   // FNV-1a over the count, the head and the ring
  const uint8_t* p = (const uint8_t*)_msgs;
  for (size_t i = 0; i < sizeof(_msgs); i++) h = (h ^ p[i]) * 16777619UL;
  return (h ^ (uint32_t)_msg_count ^ ((uint32_t)(uint16_t)_msg_head << 8)) * 16777619UL;
}

void UITask::saveHistory() {
  saveLearnedWords();   // only when a new word was learned
  uint32_t hash = historyHash();
  if (hash == _hist_hash) return;   // as on flash already
  if (_msg_count == 0) {
    the_mesh.removeUIData(HISTORY_FILE);
  } else {
    HistoryHeader h = { HISTORY_MAGIC, (uint16_t)sizeof(UIMsgEntry), UI_MSG_HISTORY, (uint8_t)_msg_count, (int16_t)_msg_head };
    if (!the_mesh.saveUIData(HISTORY_FILE, (const uint8_t*)&h, sizeof(h), (const uint8_t*)_msgs, sizeof(_msgs))) return;
  }
  _hist_hash = hash;
}

void UITask::loadHistory() {
  HistoryHeader h;
  int n = the_mesh.loadUIData(HISTORY_FILE, (uint8_t*)&h, sizeof(h));
  if (n < 0) return;
  bool ok = n == (int)sizeof(h) && h.magic == HISTORY_MAGIC && h.entry_size == sizeof(UIMsgEntry) &&
            h.slots == UI_MSG_HISTORY && h.count <= UI_MSG_HISTORY && h.head >= 0 && h.head < UI_MSG_HISTORY &&
            the_mesh.loadUIData(HISTORY_FILE, (uint8_t*)_msgs, sizeof(_msgs), sizeof(h)) == (int)sizeof(_msgs);
  if (!ok) {
    memset(_msgs, 0, sizeof(_msgs)); _msg_count = 0; _msg_head = -1;
    the_mesh.removeUIData(HISTORY_FILE);   // another layout: of no use
    _hist_hash = historyHash();
    return;
  }
  _msg_count = h.count;
  _msg_head = h.head;
  for (int i = _msg_count - 1; i >= 0; i--) {   // oldest first: fresh ids in this session
    UIMsgEntry* m = historyAt(i);
    m->seq = ++_msg_seq;
    m->from[sizeof(m->from) - 1] = 0;
    m->text[sizeof(m->text) - 1] = 0;
    if (m->state == MSG_OUT_PENDING) m->state = MSG_OUT_FAILED;   // its timer did not survive
    m->tries_left = 0;
  }
  _hist_hash = 0;   // the file has the old ids and states: written again at the next check
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
  saveLearnedWords();
  if (restart) _board->reboot();
  else _board->powerOff();
}

#ifndef UI_BATT_MIN_PLAUSIBLE_MV
  #define UI_BATT_MIN_PLAUSIBLE_MV  2500
#endif

#ifndef UI_BATT_LOW_CHECKS
  #define UI_BATT_LOW_CHECKS  3   // battery checks (8 s apart) below the limit before powering off
#endif

#ifndef UI_RESUME_MILLIS
  #define UI_RESUME_MILLIS  (5 * 60 * 1000UL)   // screen off for less than this: wake where you left off
#endif

void UITask::wakeDisplay() {
  _display->turnOn();
  applyContrast();
  // writing or reading something when the screen timed out: keep it, otherwise standby
  if (_locked || _depth <= 1 || current() == _splash || millis() - _off_at > UI_RESUME_MILLIS) home();
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

// box over the screen, up to three lines
void UITask::renderAlert() {
  big(*_display);
  uint8_t starts[3], lens[3];
  int n = ui::wrapLines(_alert, _display->width() / CW, starts, lens, 3);
  if (n < 1) n = 1;
  int h = n * LH + 6, y = (_display->height() - h) / 2;
  _display->setColor(UIColor::popup_bkg);
  _display->fillRect(0, y - 2, _display->width(), h + 4);
  _display->setColor(UIColor::popup_txt);
  _display->drawRect(0, y, _display->width(), h);
  drawLines(*_display, y + 3, n, _alert, 3);
}

// low battery: one warning below 10% (again after charging above 20%), power off below the limit
void UITask::checkBattery() {
  uint16_t raw = getBattMilliVolts();
  // below ~2.5 V no LiPo-powered nRF52 is still running: such a reading is a broken divider or
  // ADC, and acting on it would switch the device off right after every boot
  if (raw < UI_BATT_MIN_PLAUSIBLE_MV) return;
  // the shown value moves slowly (a transmit pulls the cell down for a moment)
  _batt_mv = _batt_mv == 0 ? raw : (uint16_t)((3UL * _batt_mv + raw) / 4);
  uint16_t mv = _batt_mv;
  bool external = board.isExternalPowered();
  int pct = battPercent(mv);
  if (external || pct >= LOW_BATT_REARM) _low_batt_warned = false;
  if (!external && pct < LOW_BATT_PERCENT && !_low_batt_warned) {
    _low_batt_warned = true;
    haptic(UI_HAPTIC_LONG_MS);
    if (_display != NULL && _display->isOn()) showAlert(T(S_BATT_LOW), 3000);
  }
#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  // power off only after UI_BATT_LOW_CHECKS low readings in a row (raw ones: the average lags)
  if (raw < AUTO_SHUTDOWN_MILLIVOLTS && !external) _low_count++;
  else _low_count = 0;
  if (_low_count >= UI_BATT_LOW_CHECKS) {
    if (_display != NULL) {
      _display->turnOn();
      _display->startFrame();
      big(*_display);
      _display->setColor(UIColor::warning_txt);
      drawLines(*_display, 0, 2, T(S_BATT_EMPTY), 2);
      _display->drawTextCentered(_display->width() / 2, 3 * LH + TEXT_DY, T(S_POWER_OFF));
      _display->endFrame();
      delay(2000);
    }
    hibernate(true);
  }
#endif
}

#ifndef UI_COMBO_OFF_MILLIS
  #define UI_COMBO_OFF_MILLIS   2500
#endif
#ifndef UI_COMBO_SHOW_MILLIS
  #define UI_COMBO_SHOW_MILLIS  500
#endif

// "Power off" + a bar filling up to the power off + "Let go to cancel"
void UITask::drawPowerOffCountdown(unsigned long held) {
  DisplayDriver& d = *_display;
  d.startFrame();
  big(d);
  d.setColor(UIColor::primary_txt);
  d.drawTextCentered(d.width() / 2, TEXT_DY, T(S_POWER_OFF));
  unsigned long span = UI_COMBO_OFF_MILLIS - UI_COMBO_SHOW_MILLIS;
  unsigned long el = held > UI_COMBO_SHOW_MILLIS ? held - UI_COMBO_SHOW_MILLIS : 0;
  if (el > span) el = span;
  int full = d.width() - 8;
  d.drawRect(4, LH + 4, full, 8);
  d.fillRect(6, LH + 6, (int)(el * (unsigned long)(full - 4) / span), 4);
  d.drawTextCentered(d.width() / 2, 2 * LH + TEXT_DY, T(S_LET_GO_1));
  d.drawTextCentered(d.width() / 2, 3 * LH + TEXT_DY, T(S_LET_GO_2));
  d.endFrame();
}

void UITask::loop() {
#if defined(UI_HAS_ROTARY_INPUT) && defined(PIN_ENCODER_BTN) && defined(PIN_USER_BTN)
  // Back + push held together: power off after UI_COMBO_OFF_MILLIS; from UI_COMBO_SHOW_MILLIS the
  // screen shows the countdown, letting go before the end cancels
  static unsigned long combo_sleep_at = 0;
  static bool combo_shown = false, combo_was_off = false;
  if (encoder_btn.isPressed() && user_btn.isPressed()) {
    if (_locked) {
      combo_sleep_at = 0;   // keys locked: the combo does nothing (no power off, no unlock)
    } else if (combo_sleep_at == 0) {
      combo_sleep_at = millis();
    } else if (millis() - combo_sleep_at >= UI_COMBO_OFF_MILLIS) {
      combo_sleep_at = 0;
      combo_shown = false;
      encoder_btn.cancelClick();
      user_btn.cancelClick();
      hibernate(false);
      return;
    } else if (_display != NULL && millis() - combo_sleep_at >= UI_COMBO_SHOW_MILLIS) {
      if (!combo_shown) {
        combo_shown = true;
        combo_was_off = !_display->isOn();
        if (combo_was_off) { _display->turnOn(); applyContrast(); }
      }
      drawPowerOffCountdown(millis() - combo_sleep_at);
    }
    // While both buttons are held together, consume and suppress individual events
    encoder_btn.check();
    user_btn.check();
    encoder_btn.cancelClick();
    user_btn.cancelClick();
    return;
  } else if (combo_sleep_at != 0) {
    // Both were held but released before the end: cancelled; no clicks from an uneven release
    encoder_btn.cancelClick();
    user_btn.cancelClick();
    combo_sleep_at = 0;
    if (combo_shown) {
      combo_shown = false;
      showAlert(T(S_CANCELLED), 800);
      _next_refresh = 0;
      // the screen was off before: back off once "Cancelled" has been read
      _auto_off = millis() + (combo_was_off ? 900 : autoOffMillis());
    }
  }
#endif

  char c = 0;
  int repeat = 1;

  // key lock: turning does nothing (not even waking the screen), a press only shows how to unlock,
  // hold Back unlocks
  if (_locked && !in_rescue) {
    while (rotary_input.poll() != RotaryInputEvent::None) { }
    int e1 = encoder_btn.check(), e2 = user_btn.check();
    if (e2 == BUTTON_EVENT_LONG_PRESS) {
      _locked = false;
      if (_display != NULL && !_display->isOn()) wakeDisplay();
      haptic(UI_HAPTIC_CLICK_MS);
      showAlert(T(S_UNLOCKED), 800);
      if (_display != NULL) _auto_off = millis() + autoOffMillis();
    } else if (e1 != BUTTON_EVENT_NONE || e2 != BUTTON_EVENT_NONE) {
      if (_display != NULL) {
        if (!_display->isOn()) wakeDisplay();
        _auto_off = millis() + autoOffMillis();
      }
      showAlert(T(S_LOCKED), 1500);
    }
  }

  // encoder push: click = Enter, hold = context / quick menu
  int ev = _locked ? BUTTON_EVENT_NONE : encoder_btn.check();
  if (ev == BUTTON_EVENT_CLICK) c = checkDisplayOn(KEY_ENTER);
  else if (ev == BUTTON_EVENT_LONG_PRESS) c = checkDisplayOn(KEY_CONTEXT_MENU);

  // Back: click = up one level, hold = standby; held while the splash shows = CLI rescue
  if (c == 0 && !_locked) {
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
    for (int guard = 0; guard < 16 && !_locked; guard++) {   // locked: drained above, turning does nothing
      RotaryInputEvent r = rotary_input.poll();
      if (r == RotaryInputEvent::None) break;
      net += (r == RotaryInputEvent::Next) ? 1 : -1;
    }
    _raw_detents += net;
    if (net != 0) {   // after a battery change the first turn or press shows what is fitted: keep it
      RotaryInputMode m = rotary_input.mode();
      uint8_t v = m == RotaryInputMode::Buttons ? 2 : m == RotaryInputMode::Encoder ? 1 : 0;
      if (v != 0 && _node_prefs->ui_input != v) {
        _node_prefs->ui_input = v;
        the_mesh.savePrefs();
      }
      int8_t dir = net > 0 ? 1 : -1;
      int n = net > 0 ? net : -net;
      unsigned long now = millis();
      unsigned long per_detent = (now - _last_turn) / n;
      // buttons repeat at their own pace while held; only a turning encoder speeds up
      int steps = buttonsFitted() ? 1 : ui::accelSteps(per_detent, dir == _last_dir);
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
      if (current() == standby && _depth == 1 && !in_rescue) {
        lock();   // hold Back on standby: key lock, screen off
      } else {
        if (current() == send_undo) send_undo->handleInput(KEY_CANCEL);   // the message does not go
        home();
      }
    } else {
      for (int k = 0; k < repeat; k++) {
        UIScreen* s = current();
        bool handled = s && s->handleInput(c);
        if (!handled && c == KEY_CANCEL) {
          if (_depth > 1) pop();
          else if (_display != NULL && !in_rescue) {   // Back on standby = screen off
            _display->turnOff();
            _off_at = millis();
            if (_node_prefs->ui_lock % 2 == LOCK_AUTO) _locked = true;
          }
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
  rangePoll(this);
  tracePoll(this);
  if (_air_next == 0 || timeReached(_air_next)) {   // the airtime of the last hour, a sample a minute
    _air_next = millis() + 60000UL;
    sampleAirtime();
  }

  // the antenna question is still open (receive only): a short reminder now and then
  if (_ask_antenna && timeReached(_ant_remind)) {
    _ant_remind = millis() + ANT_REMIND_MILLIS;
#ifdef PIN_VIBRATION
    if (!_node_prefs->vibe_quiet) vibration.playPattern(HAPTIC_REMIND, sizeof(HAPTIC_REMIND) / sizeof(HAPTIC_REMIND[0]));
#endif
  }

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
      if (_node_prefs->ui_lock % 2 == LOCK_AUTO) _locked = true;
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  if (next_batt_chck == 0 || timeReached(next_batt_chck)) {
    checkBattery();
    next_batt_chck = millis() + 8000;
  }

  // history changed since it was last written: keep it over a reset as well
  if (_hist_check == 0) _hist_check = millis() + UI_HIST_SAVE_MILLIS;
  if (timeReached(_hist_check)) {
    _hist_check = millis() + UI_HIST_SAVE_MILLIS;
    saveHistory();
  }
}
