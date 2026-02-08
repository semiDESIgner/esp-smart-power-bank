#pragma once
#include <Arduino.h>

namespace PowerMgr {
  void begin();

  void setRelay(bool on);     // relay enable (active-high)
  void setDcdc(bool on);      // DC-DC enable (active-high)

  // Convenience: apply your rule
  void applyChargingMode(bool charging);
}
