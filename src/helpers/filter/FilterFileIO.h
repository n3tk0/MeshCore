#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <helpers/IdentityStore.h>   // FILESYSTEM

// Small stream helpers over a firmware File, shared by the filters' files. Written for this fork.
// Not built on `native`.

namespace FilterFileIO {

  struct Sink {
    File& f;
    uint8_t buf[64];
    size_t n;
    bool ok;
    explicit Sink(File& file) : f(file), n(0), ok(true) {}
    void flush() {
      if (n && f.write(buf, n) != n) ok = false;
      n = 0;
    }
    void put(const void* d, size_t len) {
      const uint8_t* p = (const uint8_t*)d;
      while (len) {
        size_t k = sizeof(buf) - n;
        if (k > len) k = len;
        memcpy(buf + n, p, k);
        n += k; p += k; len -= k;
        if (n == sizeof(buf)) flush();
      }
    }
  };

  struct Src {
    File& f;
    explicit Src(File& file) : f(file) {}
    bool get(void* d, size_t len) { return f.read((uint8_t*)d, len) == (int)len; }
  };

  template <typename FS>
  File openRead(FS* fs, const char* path) {
#if defined(RP2040_PLATFORM)
    return fs->open(path, "r");
#else
    return fs->open(path);
#endif
  }

  template <typename FS>
  File openWrite(FS* fs, const char* path) {
    fs->remove(path);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    return fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    return fs->open(path, "w");
#else
    return fs->open(path, "w", true);
#endif
  }

}
