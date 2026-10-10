#pragma once

// Companion UI for a rotary encoder + Back button, modelled on the Sony Ericsson CMD-Z7 jog dial:
//   turn = move / next status card, press = open / confirm, hold = quick or pop-up menu,
//   Back = one level up (screen off on standby), hold Back = standby from anywhere.
// Writing uses a jog-dial keyboard with word prediction: two quick presses accept the suggestion.

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#ifndef UI_MSG_HISTORY
  #define UI_MSG_HISTORY  24
#endif

struct UIMsgEntry {
  uint32_t timestamp;
  uint8_t  path_len;
  bool     unread;
  char     from[32];
  char     text[161];   // MAX_TEXT_LEN + 1
};

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  char _alert[48];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;

  // message history (ring buffer, newest at _msg_head)
  UIMsgEntry _msgs[UI_MSG_HISTORY];
  int _msg_head, _msg_count;
  unsigned long _last_new_msg;

  // screen stack, [0] is always the standby screen (after the splash)
  UIScreen* _stack[8];
  int _depth;
  UIScreen* _splash;

  char checkDisplayOn(char c);
  void renderAlert();

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    _depth = 0;
    _msg_head = -1;
    _msg_count = 0;
    _last_new_msg = 0;
    _msgcount = 0;
    _alert_expiry = 0;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // navigation
  void push(UIScreen* s);
  void pop();
  void home();
  UIScreen* current() const { return _depth > 0 ? _stack[_depth - 1] : _splash; }
  void refresh() { _next_refresh = 0; }

  void showAlert(const char* text, int duration_millis);
  void haptic(uint16_t millis_on);
  unsigned long autoOffMillis() const;

  // state used by screens
  NodePrefs* prefs() { return _node_prefs; }
  SensorManager* sensors() { return _sensors; }
  int getMsgCount() const { return _msgcount; }
  int unreadCount() const;
  int historyCount() const { return _msg_count; }
  UIMsgEntry* historyAt(int i);          // 0 = newest
  void deleteHistory(int i);
  void clearHistory();
  void markAllRead();
  unsigned long lastNewMsgAt() const { return _last_new_msg; }

  bool getGPSState();
  void toggleGPS();
  void hibernate();
  void applyEncoderPrefs();   // turn direction and steps per detent from NodePrefs

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
