#pragma once

// LoRa OTA on ESP32: the new image goes to the other OTA partition, the running one stays as it is.
// `ota rollback` can boot the previous image again.

#include <Arduino.h>
#include <helpers/LoraOta.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>

class ESP32OtaTarget : public OtaTarget {
  const esp_partition_t* _next;
  esp_ota_handle_t _handle;
  uint32_t _img_size, _new_size;
  bool _open;

public:
  ESP32OtaTarget() : _next(NULL), _handle(0), _img_size(0), _new_size(0), _open(false) { }

  uint8_t platform() const override { return OTA_PLATFORM_ESP32; }

  uint32_t imageSize() override {
    if (_img_size) return _img_size;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == NULL) return 0;
    esp_partition_pos_t pos = { running->address, running->size };
    esp_image_metadata_t md;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) != ESP_OK) return 0;
    _img_size = md.image_len;   // segments, padding, checksum and the appended SHA-256: the whole .bin
    return _img_size;
  }

  bool readImage(uint32_t offset, uint8_t* dest, size_t len) override {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running && esp_partition_read(running, offset, dest, len) == ESP_OK;
  }

  uint32_t maxNewSize() override {
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    return next ? next->size : 0;
  }

  bool beginNew(uint32_t size) override {
    abortNew();
    _next = esp_ota_get_next_update_partition(NULL);
    if (_next == NULL || size > _next->size) return false;
    // erase as the data comes in: erasing the whole partition up front would block for seconds
    if (esp_ota_begin(_next, OTA_WITH_SEQUENTIAL_WRITES, &_handle) != ESP_OK) return false;
    _new_size = size;
    _open = true;
    return true;
  }

  bool writeNew(const uint8_t* src, size_t len) override {
    return _open && esp_ota_write(_handle, src, len) == ESP_OK;
  }

  bool finishNew() override {
    if (!_open) return false;
    _open = false;
    return esp_ota_end(_handle) == ESP_OK;   // also checks the image is a valid app
  }

  bool readNew(uint32_t offset, uint8_t* dest, size_t len) override {
    return _next && esp_partition_read(_next, offset, dest, len) == ESP_OK;
  }

  void abortNew() override {
    if (_open) esp_ota_abort(_handle);
    _open = false;
  }

  void commit() override {
    if (_next && esp_ota_set_boot_partition(_next) == ESP_OK) {
      esp_restart();
    }
  }

  // boot the image in the other partition again (the one that ran before the last update)
  static bool rollback() {
    const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    if (other == NULL || esp_ota_get_partition_description(other, &desc) != ESP_OK) return false;
    if (esp_ota_set_boot_partition(other) != ESP_OK) return false;
    esp_restart();
    return true;
  }
};
