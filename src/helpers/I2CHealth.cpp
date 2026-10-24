#include "I2CHealth.h"

I2CProbeResult i2cProbe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t e = wire.endTransmission();
#ifdef WIRE_MESHCORE_TIMEOUT
  if (e == 5) return I2C_PROBE_TIMEOUT;
#endif
  return e == 0 ? I2C_PROBE_ACK : I2C_PROBE_NACK;
}

uint32_t i2cErrorCount(TwoWire& wire) {
#ifdef WIRE_MESHCORE_TIMEOUT
  return wire.errorCount();
#else
  (void)wire;
  return 0;
#endif
}

uint32_t i2cTimeoutCount(TwoWire& wire) {
#ifdef WIRE_MESHCORE_TIMEOUT
  return wire.timeoutCount();
#else
  (void)wire;
  return 0;
#endif
}

bool i2cBusStuck(TwoWire& wire) {
#ifdef WIRE_MESHCORE_TIMEOUT
  return wire.busStuck();
#else
  (void)wire;
  return false;
#endif
}

#define I2C_LIST_MAX 16

struct I2CListEntry {
  uint8_t addr;
  I2CDeviceState state;
  const char* name;
  uint32_t at_min;  // uptime when it was turned off
};

static I2CListEntry i2c_list[I2C_LIST_MAX];
static uint8_t i2c_list_count = 0;
static bool i2c_bus_stuck = false;

void i2cListDevice(uint8_t addr, const char* name, I2CDeviceState state) {
  I2CListEntry* e = NULL;
  for (int i = 0; i < i2c_list_count; i++) {
    if (i2c_list[i].addr == addr) { e = &i2c_list[i]; break; }
  }
  if (e == NULL) {
    if (i2c_list_count >= I2C_LIST_MAX) return;
    e = &i2c_list[i2c_list_count++];
    e->addr = addr;
    e->name = NULL;
  }
  if (name) e->name = name;
  e->state = state;
  e->at_min = millis() / 60000;
  if (state != I2C_DEV_OK) {
    char line[48];
    snprintf(line, sizeof(line), "I2C 0x%02X %s turned off", addr, name ? name : "?");
    Serial.println(line);
  }
}

void i2cSetBusStuck() {
  if (!i2c_bus_stuck) Serial.println("I2C bus stuck: a line stays low");
  i2c_bus_stuck = true;
}

void i2cFormatList(char* reply, size_t size) {
  size_t n = 0;
  reply[0] = 0;
  if (i2c_bus_stuck) n += snprintf(reply + n, size - n, "bus STUCK; ");
  if (i2c_list_count == 0 && n < size) n += snprintf(reply + n, size - n, "no I2C devices");
  for (int i = 0; i < i2c_list_count && n < size; i++) {
    const I2CListEntry& e = i2c_list[i];
    const char* sep = i ? "; " : "";
    const char* name = e.name ? e.name : "?";
    switch (e.state) {
      case I2C_DEV_OK:
        n += snprintf(reply + n, size - n, "%s%02X %s ok", sep, e.addr, name); break;
      case I2C_DEV_OFF_BOOT_TIMEOUT:
        n += snprintf(reply + n, size - n, "%s%02X %s off: timeout at boot", sep, e.addr, name); break;
      case I2C_DEV_OFF_INIT_FAILED:
        n += snprintf(reply + n, size - n, "%s%02X %s off: init failed", sep, e.addr, name); break;
      case I2C_DEV_OFF_ERRORS:
        n += snprintf(reply + n, size - n, "%s%02X %s off: 3 errors at %lum", sep, e.addr, name, (unsigned long)e.at_min); break;
    }
  }
  if (n >= size && size > 4) strcpy(reply + size - 4, "...");
}
