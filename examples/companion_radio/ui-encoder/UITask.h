#pragma once

// Companion UI for a rotary encoder + Back button, modelled on the Sony Ericsson CMD-Z7 jog dial:
//   turn = move / next status card, press = open / confirm, hold = quick or pop-up menu,
//   Back = one level up (screen off on standby), hold Back = standby from anywhere.
// Fast turns move several steps (acceleration). Writing uses a jog-dial keyboard with word
// prediction: when a word is suggested the cursor sits on it, and a press accepts it.

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

#define UI_QUICK_COUNT 10   // quick reply slots
#define UI_QUICK_LEN   64   // bytes per quick reply, including the terminator

// direction / delivery state of a history entry
#define MSG_IN           0   // received
#define MSG_OUT_SENT     1   // sent to a channel: no delivery report exists
#define MSG_OUT_PENDING  2   // sent to a contact, waiting for its ACK
#define MSG_OUT_ACKED    3   // the contact confirmed it
#define MSG_OUT_FAILED   4   // no ACK in time

struct UIMsgEntry {
  uint32_t timestamp;
  uint8_t  path_len;
  bool     unread;
  uint8_t  state;       // MSG_IN / MSG_OUT_*
  char     from[32];    // conversation name: the contact or channel (also for sent messages)
  char     text[161];   // MAX_TEXT_LEN + 1
  uint8_t  src;         // MSG_SRC_*: who to answer
  uint8_t  channel_idx;
  uint8_t  pub_prefix[6];
  bool     mention;     // a channel message that names this node (@name)
  uint32_t seq;         // unique per message, so open screens follow it when the history shifts
  uint32_t sent_ts;     // timestamp the message was sent with: a retry repeats it
  uint8_t  attempt;     // attempt number of the last send of a direct message
  uint8_t  tries_left;  // automatic retries still to go before it counts as not delivered
  uint32_t acks[4];     // expected ACK hash of each recent attempt (a late ACK still counts)
  unsigned long ack_deadline;
};

#ifndef UI_SEND_ATTEMPTS
  #define UI_SEND_ATTEMPTS 3   // sends of a direct message before "not delivered"; the last one floods
#endif

#define MSG_SRC_UNKNOWN  0
#define MSG_SRC_CONTACT  1
#define MSG_SRC_CHANNEL  2

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
  unsigned long _next_refresh, _auto_off, _off_at;
  char _alert[64];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  bool _low_batt_warned;
  uint16_t _batt_mv;         // smoothed battery voltage, 0 = not measured yet
  uint8_t _low_count;        // checks in a row below the power-off voltage
  bool _locked;              // key lock: turning does nothing, hold Back unlocks
  unsigned long _ant_remind; // next vibration while the antenna question waits

  // encoder acceleration
  unsigned long _last_turn;
  int8_t _last_dir;

  // message history (ring buffer, newest at _msg_head)
  UIMsgEntry _msgs[UI_MSG_HISTORY];
  int _msg_head, _msg_count;
  uint32_t _msg_seq;
  unsigned long _last_new_msg;
  uint8_t _next_src, _next_channel, _next_prefix[6];   // from msgSource(), for the next newMsg()

  // screen stack, [0] is always the standby screen (after the splash)
  UIScreen* _stack[10];
  int _depth;
  UIScreen* _splash;

  // quick replies: built-in defaults (in the UI language) until the user edits one
  char _quick[UI_QUICK_COUNT][UI_QUICK_LEN];
  bool _quick_custom;
  void loadQuickReplies();
  bool _ask_antenna;   // antenna question still unanswered

  char checkDisplayOn(char c);
  void wakeDisplay();   // turn on: back to the open screen after a short pause, else to standby
  void renderAlert();
  UIMsgEntry* addHistory();
  void checkAckTimeouts();
  bool sendAttempt(UIMsgEntry* m);   // (re)sends a direct message from the history
  void checkBattery();
  void saveHistory();
  void loadHistory();
  bool isMention(const char* text) const;

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    _auto_off = _off_at = 0;
    ui_started_at = 0;
    _low_batt_warned = false;
    _batt_mv = 0;
    _low_count = 0;
    _locked = false;
    _ant_remind = 0;
    _last_turn = 0;
    _last_dir = 0;
    _depth = 0;
    _msg_head = -1;
    _msg_count = 0;
    _msg_seq = 0;
    _last_new_msg = 0;
    _msgcount = 0;
    _alert_expiry = 0;
    _quick_custom = false;
    _ask_antenna = false;
    _next_src = MSG_SRC_UNKNOWN;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // navigation
  void push(UIScreen* s);
  void pop();
  void home();
  UIScreen* current() const { return _depth > 0 ? _stack[_depth - 1] : _splash; }
  void refresh() { _next_refresh = 0; }

  void showAlert(const char* text, int duration_millis);
  bool alertShowing() const { return _alert_expiry && (long)(millis() - _alert_expiry) < 0; }
  void haptic(uint16_t millis_on);
  unsigned long autoOffMillis() const;
  void applyContrast();

  // state used by screens
  NodePrefs* prefs() { return _node_prefs; }
  SensorManager* sensors() { return _sensors; }
  int getMsgCount() const { return _msgcount; }
  int unreadCount() const;
  int historyCount() const { return _msg_count; }
  UIMsgEntry* historyAt(int i);          // 0 = newest
  int historyIndexOf(uint32_t seq);      // -1 once the message is gone
  void deleteHistory(int i);
  void clearHistory();
  void markAllRead();
  unsigned long lastNewMsgAt() const { return _last_new_msg; }
  // a message sent from the device: kept in the history; ack != 0 waits for the delivery report
  UIMsgEntry* addSent(const char* conv_name, uint8_t src, uint8_t channel_idx, const uint8_t* pub_prefix,
                      const char* text, uint32_t sent_ts, uint32_t ack, uint32_t timeout_millis);
  bool resend(uint32_t seq);   // a not delivered message: a new round of attempts, same timestamp
  const char* quickReply(int i) const;           // "" for an empty slot
  bool setQuickReply(int i, const char* text);   // saved to flash

  bool getGPSState();
  void toggleGPS();
  void hibernate(bool low_battery = false);
  void answerAntenna(bool fitted);   // true = transmit allowed
  void setTransmit(bool on);         // Quick menu / Settings: on or off, remembered over resets
  uint16_t battMilliVolts();         // smoothed, for display
  void resetBattery() { _batt_mv = 0; }
  void lock();                       // key lock on, screen off
  bool isLocked() const { return _locked; }
  void applyEncoderPrefs();   // turn direction and steps per detent from NodePrefs
  void enterRescue();         // CLI rescue on the USB serial port, until restart

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void msgSource(bool is_channel, uint8_t channel_idx, const uint8_t* pub_key) override;
  void notify(UIEventType t = UIEventType::none) override;
  void notifyMsg(UIEventType t, bool connected) override;
  void ackReceived(const uint8_t* ack) override;
  void loop() override;

  void shutdown(bool restart = false);
};
