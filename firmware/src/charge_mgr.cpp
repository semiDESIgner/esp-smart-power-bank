#include "charge_mgr.h"

namespace ChargeMgr {

static constexpr uint32_t DEBOUNCE_MS = 200;

static bool s_stable = false;
static bool s_lastRead = false;
static uint32_t s_lastChangeMs = 0;
static bool s_changedFlag = false;

// UI re-init scheduling
static bool s_uiPending = false;
static uint32_t s_uiRequestMs = 0;
static uint32_t s_uiDelayMs = 100;

void begin(bool initialCharging) {
  s_stable = initialCharging;
  s_lastRead = initialCharging;
  s_lastChangeMs = millis();
  s_changedFlag = false;

  s_uiPending = false;
  s_uiRequestMs = 0;
}

void update(bool rawCharging) {
  s_changedFlag = false;

  if (rawCharging != s_lastRead) {
    s_lastRead = rawCharging;
    s_lastChangeMs = millis();
  }

  if ((millis() - s_lastChangeMs) > DEBOUNCE_MS && s_stable != s_lastRead) {
    s_stable = s_lastRead;
    s_changedFlag = true;
  }
}

bool isCharging() {
  return s_stable;
}

bool stableChanged() {
  return s_changedFlag;
}

void scheduleUiReinit(uint32_t delayMs) {
  s_uiDelayMs = delayMs;
  s_uiPending = true;
  s_uiRequestMs = millis();
}

bool uiReinitDue() {
  if (!s_uiPending) return false;
  if (millis() - s_uiRequestMs >= s_uiDelayMs) {
    s_uiPending = false;
    return true;
  }
  return false;
}

bool uiReinitPending() {
  return s_uiPending;
}

uint32_t uiReinitSecondsLeft() {
  if (!s_uiPending) return 0;
  uint32_t elapsed = millis() - s_uiRequestMs;
  if (elapsed >= s_uiDelayMs) return 0;
  return (s_uiDelayMs - elapsed + 999) / 1000; // ceil seconds
}

} // namespace ChargeMgr
