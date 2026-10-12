#pragma once

// Desktop screen simulator: what UITask.cpp normally gets from MyMesh.h. A scripted mesh with a few
// contacts and channels; sending records the message and hands out ACK hashes.

#include <MeshCore.h>
#include <helpers/TxtDataHelpers.h>
#include "../../examples/companion_radio/NodePrefs.h"

#define FIRMWARE_VERSION     "v1.9.0-sim"
#define FIRMWARE_BUILD_DATE  "11 Oct 2026"
#define MAX_TEXT_LEN         (10*CIPHER_BLOCK_SIZE)
#define MSG_SEND_FAILED       0
#define MSG_SEND_SENT_FLOOD   1
#define MSG_SEND_SENT_DIRECT  2
#define MAX_ANON_CONTACTS     8
#define OUT_PATH_UNKNOWN      0xFF
#define MAX_LORA_TX_POWER     22
#define ADV_TYPE_NONE         0
#define ADV_TYPE_CHAT         1
#define ADV_TYPE_REPEATER     2
#define ADV_TYPE_ROOM         3
#define ADV_TYPE_SENSOR       4

namespace mesh {
  struct Identity { uint8_t pub_key[PUB_KEY_SIZE]; };
  struct LocalIdentity : Identity { };
  struct GroupChannel { uint8_t hash[PATH_HASH_SIZE]; uint8_t secret[PUB_KEY_SIZE]; };
  struct Packet {
    uint8_t header = 0, path_len = 0;
    int8_t _snr = 0;
    bool flood = true;
    bool isRouteFlood() const { return flood; }
    float getSNR() const { return _snr / 4.0f; }
  };
}

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;
  uint8_t flags;
  uint8_t out_path_len;
  uint32_t last_advert_timestamp;
  uint32_t lastmod;
  int32_t gps_lat, gps_lon;
};

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
};

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

// what UITask gets from MyMesh: the listener interface it implements
class MyMesh {
public:
  class Listener {
  public:
    virtual void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) = 0;
    virtual void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) = 0;
    virtual void onQueueSizeChanged(int msgcount) = 0;
    virtual void onACKRecv(uint32_t ack_crc) { }
    virtual void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                             const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) { }
    virtual bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) { return false; }
    virtual void onControlDataRecv(const mesh::Packet* pkt) { }
    virtual ~Listener() { }
  };
};

class SimMesh : public MyMesh {
public:
  mesh::LocalIdentity self_id;
  ContactInfo contacts[MAX_ANON_CONTACTS + 16];
  int num_contacts = 0;                     // without the anonymous slots
  ChannelDetails channels[MAX_GROUP_CHANNELS];
  AdvertPath recent[8];
  int num_recent = 0;
  bool tx_allowed = false;
  bool clock_approx = false;
  uint32_t ble_pin = 123456;
  uint32_t next_ack = 0x1000;
  uint32_t last_ack = 0;
  NodePrefs* prefs = NULL;
  char last_sent[200];

  ContactInfo* addContact(const char* name, uint8_t type, uint8_t key0, uint32_t lastmod, uint8_t path_len);
  void addChannel(int idx, const char* name);
  void addRecent(const char* name, uint32_t ts);

  bool isTxAllowed() const { return tx_allowed; }
  void setTxAllowed(bool on) { tx_allowed = on; }
  bool isClockApprox() const { return clock_approx; }
  bool getChannel(int idx, ChannelDetails& dest);
  int findChannelIdx(const mesh::GroupChannel& ch) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) if (memcmp(channels[i].channel.secret, ch.secret, sizeof(ch.secret)) == 0) return i;
    return -1;
  }
  ContactInfo* lookupContactByPubKey(const uint8_t* key, int prefix_len);
  bool getContactByIdx(uint32_t idx, ContactInfo& c);
  int getNumContacts() const { return num_contacts; }
  bool sendGroupMessage(uint32_t ts, mesh::GroupChannel& ch, const char* sender, const char* text, int len);
  int sendMessage(const ContactInfo& c, uint32_t ts, uint8_t attempt, const char* text, uint32_t& ack, uint32_t& timeout);
  int getRecentlyHeard(AdvertPath dest[], int max_num);
  bool advert(bool flood = false) { return tx_allowed; }
  void expectAck(uint32_t ack, ContactInfo* c) { }
  bool setRadioParams(float f, float bw, uint8_t sf, uint8_t cr) {
    if (prefs) { prefs->freq = f; prefs->bw = bw; prefs->sf = sf; prefs->cr = cr; }
    return true;
  }
  void savePrefs() { }
  int loadUIFile(const char*, uint8_t*, int) { return -1; }
  bool saveUIFile(const char*, const uint8_t*, int) { return true; }
  uint32_t getBLEPin() { return ble_pin; }
  void flushPendingWrites() { }
  bool saveUIData(const char*, const uint8_t*, int, const uint8_t*, int) { return true; }
  int loadUIData(const char*, uint8_t*, int, int = 0) { return -1; }
  void removeUIData(const char*) { }
  bool saveOfflineQueue() { return true; }
  void enterCLIRescue() { }
  void resetContactPath(ContactInfo& c) { c.out_path_len = OUT_PATH_UNKNOWN; }
  void setTxPower(int8_t dbm) { if (prefs) prefs->tx_power_dbm = dbm; }
};

extern SimMesh the_mesh;
