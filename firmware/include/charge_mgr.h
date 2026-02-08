#pragma once
#include <Arduino.h>

namespace ChargeMgr {
  // Call once at boot
  void begin(bool initialCharging);

  // Call every loop with raw pin value (HIGH = charging)
  void update(bool rawCharging);

  // Debounced stable state
  bool isCharging();

  // True if stable state changed (debounced)
  bool stableChanged();

  // Schedule LCD re-init for N ms after stable change
  void scheduleUiReinit(uint32_t delayMs);

  // Returns true once when it’s time to re-init LCD
  bool uiReinitDue();

  // For UI display (optional)
  bool uiReinitPending();
  uint32_t uiReinitSecondsLeft();
}
