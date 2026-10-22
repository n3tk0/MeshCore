#include "MyMesh.h"
#include <algorithm>

#define RADIO_SWITCH_WAIT_MAX    10000   // a radio settings switch waits this long at most for sending to finish

#ifdef WITH_LORA_OTA
  #if defined(NRF52_PLATFORM)
    #include <helpers/nrf52/NRF52OtaTarget.h>
    static NRF52OtaTarget ota_target;
    #ifdef QSPI_STORAGE
      static QspiStorage qspi;   // the new image and the mailbox go to the board's external flash
    #endif
  #elif defined(ESP32)
    #include <helpers/esp32/ESP32OtaTarget.h>
    static ESP32OtaTarget ota_target;
    // the Arduino core leaves a new image 'pending verify' instead of keeping it at once (see otaLoop)
    extern "C" bool verifyRollbackLater() { return true; }
  #endif
  #define OTA_SIDE_RADIO_DELAY   2500                 // the BEGIN reply goes out on the normal radio first
  #define OTA_RESTORE_DELAY      1500                 // the last reply goes out on the side radio first
  #define OTA_COMMIT_DELAY       10000                // the END reply goes out (and is repeated if lost) before the reboot
  #define OTA_MAX_SIDE_MILLIS    (60 * 60 * 1000UL)   // hard limit on the side radio, whatever happens
  #define OTA_CONFIRM_MILLIS     (10 * 60 * 1000UL)   // a new image that ran this long is kept
  #define OTA_ON_FILE            "/ota_on"            // `set ota on`: OTA is off until then
  #ifndef OTA_MIN_BATT_MV
    #define OTA_MIN_BATT_MV      3700                 // below this (on battery) the copy at the end waits
  #endif
#endif

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
  #define LORA_BW 250
#endif
#ifndef LORA_SF
  #define LORA_SF 10
#endif
#ifndef LORA_CR
  #define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

#define LAZY_CONTACTS_WRITE_DELAY    5000
#ifdef WITH_MAILBOX
  #define MAILBOX_FIRST_DELAY        2000   // after the owner's advert
  #define MAILBOX_SEND_GAP           2500   // between kept messages
  #define MAILBOX_EXPIRE_EVERY     600000
  #define MAILBOX_LEARN_TIME       120000
#endif

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
#endif
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
#ifdef WITH_LORA_OTA
  if (client->isAdmin()) ota_target.confirm();   // an admin got in: the new firmware works
#endif
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

// the battery: from a sensor that measures it (INA219) when there is one, else the board's own reading
static uint16_t battMilliVolts() {
  uint16_t mv = sensors.getBatteryMilliVolts();
  return mv ? mv : board.getBattMilliVolts();
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = battMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)battMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[neighbours_count] = neighbour;
          neighbours_count++;
        }
      }

      // sort neighbours based on order
      if (order_by == 0) {
        // sort by newest to oldest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting newest to oldest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp > b->heard_timestamp; // desc
        });
      } else if (order_by == 1) {
        // sort by oldest to newest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting oldest to newest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp < b->heard_timestamp; // asc
        });
      } else if (order_by == 2) {
        // sort by strongest to weakest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting strongest to weakest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr > b->snr; // desc
        });
      } else if (order_by == 3) {
        // sort by weakest to strongest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting weakest to strongest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr < b->snr; // asc
        });
      }
#endif

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){

        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  TransportKey req_scope;
  bool is_wildcard = recv_pkt_region != NULL && recv_pkt_region->isWildcard();
  bool req_scope_known = recv_pkt_region != NULL && !is_wildcard
                      && region_map.getTransportKeysFor(*recv_pkt_region, &req_scope, 1) > 0;

  switch (mesh::chooseReplyScope(req_scope_known, is_wildcard, !default_scope.isNull())) {
    case mesh::REPLY_SCOPE_REQUEST:
      sendFloodScoped(req_scope, packet, delay_millis, path_hash_size);   // reply with same scope as request
      break;
    case mesh::REPLY_SCOPE_DEFAULT:
      // requester's scope is unknown: DIRECT request (no transport codes), or code matched no Region.
      // un-scoped would be dropped at hop 0 by repeaters running flood.max.unscoped=0
      sendFloodScoped(default_scope, packet, delay_millis, path_hash_size);
      break;
    case mesh::REPLY_SCOPE_NONE:
      sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
      break;
  }
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
    return false;
  }
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  Serial.print(getLogDateTime());
  Serial.print(" RAW: ");
  mesh::Utils::printHex(Serial, raw, len);
  Serial.println();
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 1) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 0) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
#ifdef WITH_MAILBOX
  onMailboxPacket(pkt);
#endif
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = 0xFF;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    // a DIRECT login can reply via the stored out_path, as onPeerDataRecv() does for REQ
    ClientInfo* client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    bool have_out_path = client != NULL && client->out_path_len != OUT_PATH_UNKNOWN;

    auto route = mesh::chooseReplyRoute(packet->isRouteFlood(), reply_path_len != 0xFF, have_out_path);

    if (route == mesh::REPLY_ROUTE_PATH_RETURN) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      return;
    }

    mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
    if (reply == NULL) return;

    if (route == mesh::REPLY_ROUTE_DIRECT_SUPPLIED) {
      sendDirect(reply, reply_path, reply_path_len, SERVER_RESPONSE_DELAY);
    } else if (route == mesh::REPLY_ROUTE_DIRECT_OUT_PATH) {
      sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
    } else {
      sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
  for (int i = 0; i < acl.getNumClients(); i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
#ifdef WITH_MAILBOX
    if (mailbox_learn_until && parser.isValid() && parser.getType() == ADV_TYPE_CHAT) {
      if (!millisHasNowPassed(mailbox_learn_until) && mailbox.addOwner(id.pub_key)) {
        mailbox.config().enabled = 1;
        saveMailbox();
      }
      mailbox_learn_until = 0;
    }
    // a mailbox owner heard with no hop between: hand over what was kept for it
    int owner = mailbox.isEnabled() ? mailbox.findOwner(id.pub_key, PUB_KEY_SIZE) : -1;
    if (owner >= 0) {
      mailbox.markHeard(owner, getRTCClock()->getCurrentTime());
      if (mailbox_deliver_owner < 0 && mailbox.countFor(owner) > 0) {
        mailbox_deliver_owner = owner;
        mailbox_next_send = futureMillis(MAILBOX_FIRST_DELAY);
      }
    }
#endif
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
#ifdef WITH_LORA_OTA
      if (len > 5 && data[4] == OTA_REQ_TYPE) {
        if (!client->isAdmin()) return;
        client->last_timestamp = timestamp;
        client->last_activity = getRTCClock()->getCurrentTime();
        // a direct packet has no path left when it gets here, however many hops it took: the
        // route back to this admin (learned at login, with no hop) tells whether it is next to us
        bool zero_hop = packet->isRouteDirect() && packet->getPathHashCount() == 0
                        && client->out_path_len != OUT_PATH_UNKNOWN && (client->out_path_len & 63) == 0;
        memcpy(reply_data, &timestamp, 4);
        int n = lora_ota.handleRequest(client->id.pub_key, zero_hop, &data[4], len - 4, &reply_data[4], millis());
        ota_target.confirm();   // after the reply: INFO still tells the app it was not confirmed before
        applyOtaAction();
        if (ota_on_side && revert_radio_at) revert_radio_at = futureMillis(OTA_MAX_SIDE_MILLIS);   // still busy
        if (n == 0) return;   // DATA: no reply, the admin asks for STATUS now and then
        if (zero_hop) {
          mesh::Packet *reply = createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, 4 + n);
          if (reply) sendZeroHop(reply, SERVER_RESPONSE_DELAY);
          return;
        }
        // not direct: say so, by the usual route
        int reply_len = 4 + n;
        if (packet->isRouteFlood()) {
          mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                                PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
          if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
        } else {
          mesh::Packet *reply = createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
          if (reply && client->out_path_len != OUT_PATH_UNKNOWN) {
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else if (reply) {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
        return;
      }
#endif
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
#ifdef WITH_LORA_OTA
    ota_target.confirm();   // an admin reaches the console: the new firmware works (also before a 'reboot')
#endif
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA || flags == TXT_TYPE_CLI_COMMAND)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2); // NOTE: legacy was: TXT_TYPE_PLAIN

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6
      && !_prefs.disable_fwd && discover_limiter.allow(rtc_clock.getCurrentTime())
      && !isHiddenNode()   // this node wishes to NOT be discoverable
  ) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#endif
#ifdef WITH_LORA_OTA
      , lora_ota(&ota_target)
#endif
#if defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  _logging = false;
  region_load_active = false;
  recv_pkt_region = NULL;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.rx_delay_base = 0.0f;   // turn off by default, was 10.0;
  _prefs.tx_delay_factor = 0.5f; // was 0.25f
  _prefs.direct_tx_delay_factor = 0.3f; // was 0.2
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 47; // 47 hours
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')
  _prefs.loop_detect = LOOP_DETECT_MINIMAL;

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 0;    // logTx
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;
  _prefs.radio_fem_txgain = 0;

  pending_discover_tag = 0;
  pending_discover_until = 0;

  memset(default_scope.key, 0, sizeof(default_scope.key));
#ifdef WITH_MAILBOX
  mailbox_deliver_owner = -1;
  mailbox_next_send = mailbox_save_at = mailbox_next_expire = mailbox_learn_until = 0;
#endif
#ifdef WITH_LORA_OTA
  ota_commit_at = 0;
  ota_on_side = false;
  ota_radio_away = false;
  ota_ident[0] = 0;
#endif
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
#if defined(WITH_LORA_OTA) && defined(QSPI_STORAGE)
  if (qspi.begin()) ota_target.setQspi(&qspi);
#endif
#ifdef WITH_MAILBOX
  loadMailbox();
#endif
#ifdef WITH_LORA_OTA
  snprintf(ota_ident, sizeof(ota_ident), "%s %s|%s", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE, board.getManufacturerName());
  lora_ota.setIdent(ota_ident);
  lora_ota.setEnabled(_fs->exists(OTA_ON_FILE));
  lora_ota.setRadioCheck([](void*, const OtaRadioParams& p) {
    return radio_driver.paramsSupported(p.freq, p.bw, p.sf, p.cr);
  }, NULL);
  lora_ota.setPowerCheck([](void*) {
    uint16_t mv = battMilliVolts();   // 0 or very low: the board cannot measure it
    return board.isExternalPowered() || mv < 1000 || mv >= OTA_MIN_BATT_MV;
  }, NULL);
#endif
  // TODO: key_store.begin();
  region_map.load(_fs);

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
    bridge.begin();
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);

  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");

  board.attachDynamicPrefs(_prefs.getCustom());

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis(((uint32_t)_prefs.advert_interval) * 2 * 60 * 1000);
  } else {
    next_local_advert = 0; // stop the timer
  }
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

#if defined(USE_LR2021)
bool MyMesh::configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
  return radio_driver.configSideDetectors(sideDetSFs, num, bw);
}
#endif

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(),
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
#ifdef WITH_MAILBOX
  } else if (memcmp(command, "mailbox", 7) == 0 && (command[7] == 0 || command[7] == ' ')) {
    handleMailboxCommand(&command[7], reply);
#endif
#ifdef WITH_LORA_OTA
  } else if (strcmp(command, "ota status") == 0) {
    static const char* states[] = { "idle", "receiving", "ready", "failed", "paused" };
    uint8_t st = lora_ota.getState();
    sprintf(reply, "> %s, %s%s, %s", lora_ota.isEnabled() ? "on" : "off", st < 5 ? states[st] : "?",
            ota_target.isUnconfirmed() ? ", new firmware not confirmed yet" : "", ota_ident);
  } else if (strcmp(command, "set ota on") == 0 || strcmp(command, "set ota off") == 0) {
    bool on = command[9] == 'n';
    if (on) {
      File f = openAppend(OTA_ON_FILE);
      if (f) {
        f.write((const uint8_t*)"1", 1);
        f.close();
      }
    } else {
      _fs->remove(OTA_ON_FILE);
    }
    lora_ota.setEnabled(_fs->exists(OTA_ON_FILE));   // off also stops an update under way
    applyOtaAction();
    strcpy(reply, lora_ota.isEnabled() == on ? "OK" : "Err - could not save");
  } else if (strcmp(command, "ota rollback") == 0) {   // nRF52: needs our bootloader and the QSPI flash
    if (lora_ota.isActive() || !ota_target.rollback()) strcpy(reply, "Err - no previous firmware");
#endif
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();

#ifdef WITH_LORA_OTA
  bool hold_adverts = ota_radio_away;   // they would go out on the OTA side channel; sent once back
#else
  bool hold_adverts = false;
#endif
  if (hold_adverts) {
    // later
  } else if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  // a radio switch waits for what is being sent (e.g. the reply that announced it), up to a limit
  bool radio_idle = !isSending() && _mgr->getOutboundTotal() == 0;
  if (set_radio_at && millisHasNowPassed(set_radio_at) && !radio_idle && !millisHasNowPassed(set_radio_at + RADIO_SWITCH_WAIT_MAX)) {
    // not yet
  } else if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at) && !radio_idle
      && !millisHasNowPassed(revert_radio_at + RADIO_SWITCH_WAIT_MAX)) {
    // not yet
  } else if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
#ifdef WITH_LORA_OTA
    ota_radio_away = false;
#endif
  }

#ifdef WITH_LORA_OTA
  otaLoop();
  #ifdef QSPI_STORAGE
  qspi.loop();
  #endif
#endif
#ifdef WITH_MAILBOX
  mailboxLoop();
#endif

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs);
    dirty_contacts_expiry = 0;
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;
}

#ifdef WITH_MAILBOX
#define MAILBOX_FILE  "/mailbox"
#define MAILBOX_TMP   "/mailbox.tmp"

struct MailboxFile {
  File& f;
  bool ok = true;
  MailboxFile(File& file) : f(file) { }
  void write(const uint8_t* p, int n) { if (f.write(p, n) != (size_t)n) ok = false; }
  int read(uint8_t* p, int n) { return f.read(p, n); }
};

#if defined(WITH_LORA_OTA) && defined(QSPI_STORAGE)
// the mailbox in the external flash: a ring of slots, [magic][seq][len][crc] then the data; the header
// is written last, so a save cut short is not seen and the slot before stays the newest
#define MAILBOX_QSPI_MAGIC  0x3158424D   // "MBX1"
#define MAILBOX_QSPI_HDR    16
static uint32_t mailbox_qspi_seq = 0, mailbox_qspi_slot = QSPI_MAILBOX_SLOTS - 1;

static uint32_t crc32Update(uint32_t crc, const uint8_t* p, int n) {
  crc = ~crc;
  while (n-- > 0) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
  }
  return ~crc;
}

struct MailboxQspiWriter {
  uint32_t base, pos = 0, erased = 0, crc = 0;
  bool ok = true;
  MailboxQspiWriter(uint32_t b) : base(b) { }
  void write(const uint8_t* p, int n) {
    if (!ok) return;
    uint32_t at = MAILBOX_QSPI_HDR + pos;
    if (at + n > QSPI_MAILBOX_SLOT) { ok = false; return; }
    while (erased < at + n) {
      if (!qspi.erase(base + erased)) { ok = false; return; }
      erased += QSPI_SECTOR;
    }
    if (!qspi.write(base + at, p, n)) ok = false;
    crc = crc32Update(crc, p, n);
    pos += n;
  }
};

struct MailboxQspiReader {
  uint32_t base, pos = 0, len;
  MailboxQspiReader(uint32_t b, uint32_t l) : base(b), len(l) { }
  int read(uint8_t* p, int n) {
    if (pos + n > len) n = len - pos;
    if (n <= 0 || !qspi.read(base + MAILBOX_QSPI_HDR + pos, p, n)) return 0;
    pos += n;
    return n;
  }
};

static bool mailboxSlotValid(uint32_t slot, uint32_t* seq, uint32_t* len) {
  uint32_t base = QSPI_MAILBOX_ADDR + slot * QSPI_MAILBOX_SLOT;
  uint32_t h[4];
  if (!qspi.read(base, (uint8_t*)h, sizeof(h)) || h[0] != MAILBOX_QSPI_MAGIC) return false;
  if (h[2] > QSPI_MAILBOX_SLOT - MAILBOX_QSPI_HDR) return false;
  uint32_t crc = 0;
  uint8_t buf[256];
  for (uint32_t off = 0; off < h[2]; off += sizeof(buf)) {
    uint32_t n = h[2] - off < sizeof(buf) ? h[2] - off : sizeof(buf);
    if (!qspi.read(base + MAILBOX_QSPI_HDR + off, buf, n)) return false;
    crc = crc32Update(crc, buf, n);
  }
  if (crc != h[3]) return false;
  *seq = h[1];
  *len = h[2];
  return true;
}

// true when the external flash holds a mailbox (then it is loaded)
static bool loadMailboxQspi(MailboxStore& mailbox) {
  bool found = false;
  uint32_t best_len = 0;
  for (uint32_t i = 0; i < QSPI_MAILBOX_SLOTS; i++) {
    uint32_t seq, len;
    if (mailboxSlotValid(i, &seq, &len) && (!found || (int32_t)(seq - mailbox_qspi_seq) > 0)) {
      found = true;
      mailbox_qspi_seq = seq;
      mailbox_qspi_slot = i;
      best_len = len;
    }
  }
  if (!found) return false;
  MailboxQspiReader r(QSPI_MAILBOX_ADDR + mailbox_qspi_slot * QSPI_MAILBOX_SLOT, best_len);
  if (!mailbox.load(r)) MESH_DEBUG_PRINTLN("loadMailbox: damaged slot, mailbox reset");
  return true;
}

static bool saveMailboxQspi(const MailboxStore& mailbox) {
  uint32_t slot = (mailbox_qspi_slot + 1) % QSPI_MAILBOX_SLOTS;
  uint32_t base = QSPI_MAILBOX_ADDR + slot * QSPI_MAILBOX_SLOT;
  MailboxQspiWriter w(base);
  mailbox.save(w);
  if (!w.ok) return false;
  uint32_t h[4] = { MAILBOX_QSPI_MAGIC, mailbox_qspi_seq + 1, w.pos, w.crc };
  if (!qspi.write(base, (const uint8_t*)h, sizeof(h))) return false;
  mailbox_qspi_seq++;
  mailbox_qspi_slot = slot;
  return true;
}
#endif

void MyMesh::loadMailbox() {
#if defined(WITH_LORA_OTA) && defined(QSPI_STORAGE)
  if (qspi.ok()) {
    if (loadMailboxQspi(mailbox)) {
      mailbox.expire(getRTCClock()->getCurrentTime());
      return;
    }
    // none there yet: the one in the internal flash (moved over with the next save)
  }
#endif
  if (!_fs->exists(MAILBOX_FILE)) return;
#if defined(RP2040_PLATFORM)
  File f = _fs->open(MAILBOX_FILE, "r");
#else
  File f = _fs->open(MAILBOX_FILE);
#endif
  if (!f) return;
  MailboxFile mf(f);
  if (!mailbox.load(mf)) {
    MESH_DEBUG_PRINTLN("loadMailbox: damaged file, mailbox reset");
  }
  f.close();
  mailbox.expire(getRTCClock()->getCurrentTime());   // the clock may have started over
}

void MyMesh::saveMailbox() {
#if defined(WITH_LORA_OTA) && defined(QSPI_STORAGE)
  if (qspi.ok()) {
    if (saveMailboxQspi(mailbox)) {
      if (_fs->exists(MAILBOX_FILE)) _fs->remove(MAILBOX_FILE);
      return;
    }
    MESH_DEBUG_PRINTLN("saveMailbox: external flash failed, internal file instead");
  }
#endif
  // write a temp file and rename it over the old one, so a reset mid-write keeps the old mailbox
  _fs->remove(MAILBOX_TMP);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = _fs->open(MAILBOX_TMP, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(MAILBOX_TMP, "w");
#else
  File f = _fs->open(MAILBOX_TMP, "w", true);
#endif
  if (!f) return;
  MailboxFile mf(f);
  mailbox.save(mf);
  f.close();
  if (mf.ok) {
    _fs->remove(MAILBOX_FILE);
    _fs->rename(MAILBOX_TMP, MAILBOX_FILE);
  }
}

void MyMesh::onMailboxPacket(mesh::Packet* pkt) {
  if (!mailbox.isEnabled() || pkt->payload_len < 2) return;
  uint32_t now = getRTCClock()->getCurrentTime();
  uint8_t type = pkt->getPayloadType();

  // an owner's own flood packet with no hop yet: it is here and gets messages the normal way
  if (pkt->isRouteFlood() && pkt->getPathHashCount() == 0
      && (type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_PATH)) {
    for (int i = 0; i < mailbox.config().num_owners; i++) {
      if (mailbox.config().owners[i][0] == pkt->payload[1]) mailbox.markHeard(i, now);
    }
  }

  bool changed = false;
  if (type == PAYLOAD_TYPE_TXT_MSG && pkt->isRouteDirect() && pkt->getPathHashCount() == 0) {
    // no hop: from a neighbour of the owner, or another mailbox handing over; keeping it would
    // pass copies back and forth between mailboxes
  } else if (type == PAYLOAD_TYPE_TXT_MSG) {
    uint8_t hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(hash);
    changed = mailbox.onPacket(type, pkt->payload, pkt->payload_len, hash, now);
  } else if (type == PAYLOAD_TYPE_PATH) {
    changed = mailbox.onPathReply(pkt->payload, pkt->payload_len, now);
  }
  if (changed) mailbox_save_at = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // a sender's retries come in a burst
}

void MyMesh::mailboxLoop() {
  bool on_side_radio = false;
#ifdef WITH_LORA_OTA
  on_side_radio = ota_radio_away;   // the owner would not hear it there
#endif
  if (mailbox_deliver_owner >= 0 && !on_side_radio && millisHasNowPassed(mailbox_next_send)) {
    int i = mailbox.nextFor(mailbox_deliver_owner);
    mesh::Packet* pkt = i >= 0 ? obtainNewPacket() : NULL;
    if (i < 0) {
      mailbox_deliver_owner = -1;   // all handed over
    } else if (pkt) {
      const MailboxEntry& e = mailbox.at(i);
      pkt->header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);   // ver 1, route set by sendZeroHop()
      pkt->payload_len = e.len;
      memcpy(pkt->payload, e.payload, e.len);
      sendZeroHop(pkt);             // no hop: only the owner next to us hears it, nobody repeats it
      mailbox.remove(i);
      mailbox_save_at = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      mailbox_next_send = futureMillis(MAILBOX_SEND_GAP);
    } else {
      mailbox_next_send = futureMillis(MAILBOX_SEND_GAP);   // no free packet, try again later
    }
  }

  if (millisHasNowPassed(mailbox_next_expire)) {
    mailbox_next_expire = futureMillis(MAILBOX_EXPIRE_EVERY);
    if (mailbox.expire(getRTCClock()->getCurrentTime())) mailbox_save_at = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  }

  if (mailbox_save_at && millisHasNowPassed(mailbox_save_at)) {
    mailbox_save_at = 0;
    saveMailbox();
  }
}

// mailbox                    state
// mailbox on|off
// mailbox add <pubkey-hex>   owner, the full 64-hex public key
// mailbox learn              the next companion advert heard with no hop (in 2 minutes) becomes an owner
// mailbox del <hex prefix>   owner and its messages
// mailbox list               owners: key prefix, kept messages, heard with no hop
// mailbox ttl <hours>        keep messages this long (1..720)
// mailbox away <minutes>     owner heard this recently = present, nothing kept (0..1440)
// mailbox clear              drop all kept messages
void MyMesh::handleMailboxCommand(char* args, char* reply) {
  while (*args == ' ') args++;
  MailboxConfig& c = mailbox.config();
  uint32_t now = getRTCClock()->getCurrentTime();
  bool save = true;

  if (*args == 0) {
    sprintf(reply, "%s, owners %d, kept %d/%d, ttl %dh, away %dmin", c.enabled ? "on" : "off",
            c.num_owners, mailbox.count(), MAILBOX_MAX, c.ttl_hours, c.away_mins);
    save = false;
  } else if (strcmp(args, "on") == 0 || strcmp(args, "off") == 0) {
    c.enabled = args[1] == 'n';
    if (!c.enabled) mailbox_deliver_owner = -1;
    strcpy(reply, c.enabled && c.num_owners == 0 ? "OK - add an owner too" : "OK");
  } else if (memcmp(args, "add ", 4) == 0) {
    char* hex = &args[4];
    uint8_t key[PUB_KEY_SIZE];
    if (strlen(hex) != PUB_KEY_SIZE * 2 || !mesh::Utils::fromHex(key, PUB_KEY_SIZE, hex)) {
      strcpy(reply, "Err - need the full 64-hex public key");
      save = false;
    } else if (!mailbox.addOwner(key)) {
      sprintf(reply, "Err - max %d owners", MAILBOX_MAX_OWNERS);
      save = false;
    } else {
      strcpy(reply, "OK");
    }
  } else if (strcmp(args, "learn") == 0) {
    if (c.num_owners >= MAILBOX_MAX_OWNERS) {
      sprintf(reply, "Err - max %d owners", MAILBOX_MAX_OWNERS);
    } else {
      mailbox_learn_until = futureMillis(MAILBOX_LEARN_TIME);
      strcpy(reply, "OK - send an advert (no hop) from the companion within 2 min");
    }
    save = false;
  } else if (memcmp(args, "del ", 4) == 0) {
    char* hex = &args[4];
    uint8_t key[PUB_KEY_SIZE];
    int n = strlen(hex) / 2;
    if (n < 1 || n > PUB_KEY_SIZE || !mesh::Utils::fromHex(key, n, hex)) {
      strcpy(reply, "Err - bad key");
      save = false;
    } else {
      int owner = mailbox.findOwner(key, n);
      if (owner >= 0 && mailbox.removeOwner(owner)) {
        mailbox_deliver_owner = -1;
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - not an owner");
        save = false;
      }
    }
  } else if (strcmp(args, "list") == 0) {
    char* dp = reply;
    *dp = 0;
    for (int i = 0; i < c.num_owners; i++) {
      char hex[9];
      mesh::Utils::toHex(hex, c.owners[i], 4);
      dp += sprintf(dp, "%s%s: %d%s", i ? ", " : "", hex, mailbox.countFor(i), mailbox.isPresent(i, now) ? " here" : "");
    }
    if (c.num_owners == 0) strcpy(reply, "no owners");
    save = false;
  } else if (memcmp(args, "ttl ", 4) == 0) {
    int h = atoi(&args[4]);
    if (h < 1 || h > 720) { strcpy(reply, "Err - 1..720 hours"); save = false; }
    else { c.ttl_hours = h; strcpy(reply, "OK"); }
  } else if (memcmp(args, "away ", 5) == 0) {
    int m = atoi(&args[5]);
    if (m < 0 || m > 1440) { strcpy(reply, "Err - 0..1440 minutes"); save = false; }
    else { c.away_mins = m; strcpy(reply, "OK"); }
  } else if (strcmp(args, "clear") == 0) {
    mailbox.clear();
    mailbox_deliver_owner = -1;
    strcpy(reply, "OK");
  } else {
    strcpy(reply, "Err - mailbox [on|off|learn|add|del|list|ttl|away|clear]");
    save = false;
  }
  if (save) saveMailbox();
}
#endif

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge.isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
#ifdef WITH_MAILBOX
  if (mailbox_deliver_owner >= 0 || mailbox_save_at) return true;
#endif
#ifdef WITH_LORA_OTA
  if (lora_ota.isActive()) return true;
#endif
  return _mgr->getOutboundTotal() > 0;
}

#ifdef WITH_LORA_OTA
void MyMesh::applyOtaAction() {
  switch (lora_ota.takeAction()) {
  case LoraOta::ACT_SIDE_RADIO: {
    const OtaRadioParams& side = lora_ota.sideRadio();
    ota_on_side = true;
    ota_radio_away = true;
    pending_freq = side.freq;
    pending_bw = side.bw;
    pending_sf = side.sf;
    pending_cr = side.cr;
    set_radio_at = futureMillis(OTA_SIDE_RADIO_DELAY);
    revert_radio_at = futureMillis(OTA_SIDE_RADIO_DELAY + OTA_MAX_SIDE_MILLIS);
    break;
  }
  case LoraOta::ACT_RESTORE_RADIO:
    ota_commit_at = 0;   // an ABORT after END: no reboot
    if (ota_on_side) {
      set_radio_at = 0;
      revert_radio_at = futureMillis(OTA_RESTORE_DELAY);
      ota_on_side = false;
    }
    break;
  case LoraOta::ACT_COMMIT:
    ota_commit_at = futureMillis(OTA_COMMIT_DELAY);
    break;
  default:
    break;
  }
}

void MyMesh::otaLoop() {
  if (millis() > OTA_CONFIRM_MILLIS) ota_target.confirm();   // cheap when there is nothing to confirm
  if (lora_ota.checkTimeout(millis())) {
    MESH_DEBUG_PRINTLN("LoRa OTA: session timed out");
    applyOtaAction();
  }
  if (ota_commit_at && millisHasNowPassed(ota_commit_at)) {
    ota_commit_at = 0;
    acl.save(_fs);   // nothing is lost by the reboot, also the route back to the admin (not always marked dirty)
  #ifdef WITH_MAILBOX
    if (mailbox_save_at) saveMailbox();
  #endif
    lora_ota.commit();   // does not return when it works
    MESH_DEBUG_PRINTLN("LoRa OTA: could not boot the new image");
    applyOtaAction();
  }
}
#endif
