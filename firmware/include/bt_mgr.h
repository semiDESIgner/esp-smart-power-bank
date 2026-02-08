#pragma once
#include <Arduino.h>

namespace BtMgr {
  void begin(const char* deviceName);
  bool connected();

  void print(const char* s);
  void println(const char* s);
  void printf(const char* fmt, ...);

  // ---- NEW: RX helpers ----
  int  available();
  int  read();  // returns -1 if none
  size_t readLine(char* out, size_t outLen); // reads up to '\n', null-terminated
}
