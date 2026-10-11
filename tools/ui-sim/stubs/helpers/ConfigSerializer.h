#pragma once
// Simulator: NodePrefs only needs the def() calls to compile; nothing is saved.
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
class ConfigSerializer {
protected:
  template<class T> void def(const char*, T&) { }
  void def(const char*, char*, size_t) { }
  void def(const char*, void*, size_t) { }
  virtual void structure() { }
public:
  virtual ~ConfigSerializer() { }
};
