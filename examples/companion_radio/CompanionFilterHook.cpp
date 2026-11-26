// Companion receive filter and block list: the MyMesh side. Lives in its own file so the shared MyMesh.cpp
// only carries one-line calls into it. The filter itself is src/helpers/filter/CompanionFilter.h. The idea
// of the hook (verdicts decided before the mesh handles the packet) is from Dutch-MeshCore (dmc-dev, MIT).

#include "MyMesh.h"

#ifdef WITH_PACKET_FILTER

#include <helpers/filter/FilterFileIO.h>

void MyMesh::loadCFilter() {
  FILESYSTEM* fs = _store->getPrimaryFS();
  if (fs != nullptr && fs->exists(CFILTER_PREFS_FILE)) {
    File file = FilterFileIO::openRead(fs, CFILTER_PREFS_FILE);
    if (file) {
      FilterFileIO::Src r(file);
      _cfilter.read(r);   // a damaged file leaves the defaults (off)
      file.close();
    }
  }
  _cfilter.markSaved();
}

// Written only when something changed (a signature of what would be written is compared), through a temp
// file renamed over the old one.
bool MyMesh::saveFilter() {
  if (!_cfilter.changed()) return true;
  FILESYSTEM* fs = _store->getPrimaryFS();
  if (fs == nullptr) return false;
  File file = FilterFileIO::openWrite(fs, CFILTER_PREFS_FILE ".tmp");
  if (!file) return false;
  FilterFileIO::Sink s(file);
  _cfilter.write(s);
  s.flush();
  file.close();
  if (!s.ok) {
    fs->remove(CFILTER_PREFS_FILE ".tmp");
    return false;
  }
  fs->remove(CFILTER_PREFS_FILE);
  if (!fs->rename(CFILTER_PREFS_FILE ".tmp", CFILTER_PREFS_FILE)) return false;
  _cfilter.markSaved();
  return true;
}

// From filterRecvFloodPacket(): true drops the packet before the mesh handles it, so it is neither shown
// nor repeated.
bool MyMesh::cfilterFlood(mesh::Packet* packet) {
  _cfilter_hidden_set = false;
  if (!_cfilter.isEnabled()) return false;

  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  // A relayed copy of a packet already judged: the mesh's duplicate check handles it (a dropped one was
  // marked seen below).
  if (_cfilter.alreadyJudged(hash)) return false;

  CFilterVerdict verdict = _cfilter.checkFlood(packet);

  // Group text is decrypted here with this node's own channel keys, the same way Mesh::onRecvPacket() is
  // about to. Only channels the user holds can be read, so only those are filtered on content.
  if (verdict == CFilterVerdict::PASS && packet->getPayloadType() == PAYLOAD_TYPE_GRP_TXT &&
      _cfilter.wantsGroupText() && packet->payload_len > PATH_HASH_SIZE + CIPHER_MAC_SIZE) {
    mesh::GroupChannel channels[4];
    int num = searchChannelsByHash(packet->payload, channels, 4);
    for (int i = 0; i < num; i++) {
      uint8_t data[MAX_PACKET_PAYLOAD + 1];
      int len = mesh::Utils::MACThenDecrypt(channels[i].secret, data, &packet->payload[PATH_HASH_SIZE],
                                            packet->payload_len - PATH_HASH_SIZE);
      if (len > 0) {
        uint32_t key;
        memcpy(&key, channels[i].secret, sizeof(key));
        verdict = _cfilter.checkGroupText(data, len, getRTCClock()->getCurrentTime(), millis(), (uint8_t)random(100), key);
        break;
      }
    }
  }

  if (verdict == CFilterVerdict::PASS) return false;

  if (verdict == CFilterVerdict::HIDE && _prefs.isRepeatEn()) {
    // A personal choice: still repeated for everyone else, only not shown here.
    memcpy(_cfilter_hidden, hash, sizeof(_cfilter_hidden));
    _cfilter_hidden_set = true;
    return false;
  }

  getTables()->markSeen(packet);   // later copies then die in the mesh's duplicate check
  return true;
}

// From onChannelMessageRecv(): true when this is the message cfilterFlood() let through only so it could be
// repeated.
bool MyMesh::cfilterHidden(const mesh::Packet* packet) {
  if (!_cfilter_hidden_set) return false;

  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  if (memcmp(hash, _cfilter_hidden, sizeof(_cfilter_hidden)) != 0) return false;
  _cfilter_hidden_set = false;
  return true;
}

// The UI's "Block" on a contact: its key goes on the list, and it leaves the contacts.
bool MyMesh::blockContact(const ContactInfo& contact) {
  uint8_t key[PUB_KEY_SIZE];
  memcpy(key, contact.id.pub_key, PUB_KEY_SIZE);
  if (!_cfilter.blockKey(key) && !_cfilter.c.hasBlock()) return false;   // list full
  ContactInfo* c = lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (c && removeContact(*c)) {
    _store->deleteBlobByKey(key, PUB_KEY_SIZE);
    dirty_contacts_expiry = futureMillis(5000);   // as LAZY_CONTACTS_WRITE_DELAY in MyMesh.cpp
  }
  return saveFilter();
}

bool MyMesh::unblockSlot(int slot) {
  if (!_cfilter.c.removeBlockSlot(slot)) return false;
  return saveFilter();
}

bool MyMesh::muteSender(const char* name) {
  if (name == nullptr || name[0] == '\0' || strlen(name) >= FILTER_SENDER_LEN) return false;
  if (!_cfilter.c.addSenderName(name)) {   // already there counts as done
    bool there = false;
    for (int i = 0; i < FILTER_RULE_COUNT; i++) there = there || strcmp(_cfilter.c.sender_rules[i].name, name) == 0;
    if (!there) return false;
  }
  _cfilter.enabled = true;
  return saveFilter();
}

bool MyMesh::removeSenderSlot(int slot) {
  if (slot < 0 || slot >= FILTER_RULE_COUNT || _cfilter.c.sender_rules[slot].name[0] == '\0') return false;
  memset(&_cfilter.c.sender_rules[slot], 0, sizeof(SenderRule));
  _cfilter.c.sender_last[slot] = 0;
  return saveFilter();
}

bool MyMesh::hideChannel(int channel_idx) {
  ChannelDetails ch;
  if (!getChannel(channel_idx, ch) || ch.name[0] == '\0') return false;
  uint32_t key;
  memcpy(&key, ch.channel.secret, sizeof(key));
  if (!_cfilter.hideChannel(key, ch.name)) return false;
  return saveFilter();
}

bool MyMesh::unhideSlot(int slot) {
  if (!_cfilter.unhideSlot(slot)) return false;
  return saveFilter();
}

#endif
