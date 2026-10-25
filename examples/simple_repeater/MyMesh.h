#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
#endif

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include "RateLimiter.h"
#ifndef NO_MAILBOX
  #define WITH_MAILBOX           // every repeater of this fork; off until 'mailbox learn' / 'mailbox on'
  #include <helpers/MailboxStore.h>
#endif

#include <helpers/PowerSaver.h>
#include <helpers/I2CHealth.h>

#if !defined(NO_TELEMETRY_HISTORY) && !defined(STM32_PLATFORM)
  #define WITH_TELEMETRY_HISTORY // 24 h every 10 min + a week every hour, in a file; read by the app
  #include <helpers/TelemetryHistory.h>
#endif

#if !defined(NO_LORA_OTA) && (defined(NRF52_PLATFORM) || defined(ESP32))
  #define WITH_LORA_OTA          // firmware delta over LoRa from an admin with no hop between
  #include <helpers/LoraOta.h>
#endif

#ifdef WITH_BRIDGE
extern AbstractBridge* bridge;
#endif

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;                // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "15 Oct 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.18.0"
#endif

#define FIRMWARE_ROLE "repeater"

#define PACKET_LOG_FILE  "/packet_log"

class MyMesh : public mesh::Mesh, public CommonCLICallbacks {
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  ClientACL  acl;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  uint8_t reply_path[MAX_PATH_SIZE];
  uint8_t reply_path_len;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#ifdef WITH_MAILBOX
  MailboxStore mailbox;
  int mailbox_deliver_owner;            // -1 = not delivering
  unsigned long mailbox_next_send;      // millis
  unsigned long mailbox_save_at;        // millis, 0 = nothing to save
  unsigned long mailbox_next_expire;    // millis
  unsigned long mailbox_learn_until;    // millis, 0 = not learning: the next companion advert with no hop is an owner
  bool mailbox_confirm;                 // the owner is not proven (only its 1-byte hash): each message waits for its ACK
  uint8_t mailbox_wait_hash[MAX_HASH_SIZE];   // the message handed over that waits for an ACK
  unsigned long mailbox_wait_until;     // millis, 0 = not waiting
  unsigned long mailbox_try_after[MAILBOX_MAX_OWNERS];   // millis: no new unproven delivery before
  void startMailboxDelivery(int owner, bool proven);
  void loadMailbox();
  void saveMailbox();
  void onMailboxPacket(mesh::Packet* pkt);
  void mailboxLoop();
  void handleMailboxCommand(char* args, char* reply);
#endif
#ifdef WITH_LORA_OTA
  LoraOta lora_ota;
  unsigned long ota_commit_at;          // millis, 0 = no new image waiting
  bool ota_on_side;                     // on the OTA side channel radio settings
  bool ota_radio_away;                  // the radio is not back on the normal settings yet (it waits for the queue)
  char ota_ident[64];
  bool ota_probe_sent;                  // an unconfirmed new firmware sent an advert to test its radio
  bool ota_verdict_done;                // ... and was kept or rolled back
  void applyOtaAction();
  void otaLoop();
#endif
  PowerSaver psave;
  unsigned long psave_next_check;       // millis
  bool psave_radio_off;
  unsigned long psave_next_wake_check;  // millis, in the 'off' step
  void loadPowerSave();
  bool savePowerSave();
  void powerSaveLoop();
  void applyPowerStage(uint8_t old_stage);
  void handlePowerSaveCommand(char* args, char* reply);
  void handleBatteryCommand(char* args, char* reply);
  void loadBatteryCapacity();
  void formatSelfTest(char* reply);
#ifdef WITH_TELEMETRY_HISTORY
  TelemetryHistory history;
  unsigned long history_next;           // millis: the next 10-minute sample
  unsigned long history_save_at;        // millis: the next save (hourly)
  uint32_t history_rx, history_tx, history_air;   // counters at the last sample
  void historyLoop();
  void loadHistory();
  void saveHistory();
#endif
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
  uint8_t handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood);
  uint8_t handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  mesh::Packet* createSelfAdvert();

  File openAppend(const char* fname);
  bool isLooped(const mesh::Packet* packet, const uint8_t max_counters[]);

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  bool allowPacketForward(const mesh::Packet* packet) override;
  const char* getLogDateTime() override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet* pkt, int len) override;
  void logTxFail(mesh::Packet* pkt, int len) override;
  int calcRxDelay(float score, uint32_t air_time) const override;

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled?"1":"0");
  }
#endif

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;

  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void sendNodeDiscoverReq();
  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }
  bool isHiddenNode() const {
    return _prefs.advert_interval == 0 && _prefs.flood_advert_interval == 0;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  // CommonCLICallbacks
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  bool formatFileSystem() override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override;
  void setTxPower(int8_t power_dbm) override;
  void formatNeighborsReply(char *reply) override;
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  bool isPowerOff() const { return psave.stage() == PSAVE_OFF; }   // battery empty: the radio is off, the node only sleeps
  void powerOffLoop();
  static void powerOffAtBoot(bool sensors_ready);   // ESP32: still empty after the 'off' step's sleep -> sleep again
  void loop();

#if defined(WITH_BRIDGE)
  void setBridgeState(bool enable) override {
    if (enable == bridge.isRunning()) return;
    if (enable)
    {
      bridge.begin();
    }
    else 
    {
      bridge.end();
    }
  }

  void restartBridge() override {
    if (!bridge.isRunning()) return;
    bridge.end();
    bridge.begin();
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) override;
  #endif

};
