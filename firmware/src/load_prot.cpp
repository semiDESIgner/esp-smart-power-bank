#include "load_prot.h"

namespace LoadProt {

static Config gCfg;

static bool gTripped = false;
static uint32_t gOverStartMs = 0;
static float gLastLoadA = 0.0f;
static uint32_t gLastTripMs = 0;

// Button reset config/state
static bool gBtnEnabled = false;
static uint8_t gBtnPin = 255;
static bool gBtnActiveLow = true;
static uint32_t gBtnHoldMs = 2000;
static uint32_t gBtnHoldStartMs = 0;

static inline float absf_fast(float x) { return x < 0 ? -x : x; }

void begin(const Config& cfg) {
  gCfg = cfg;
  gTripped = false;
  gOverStartMs = 0;
  gLastLoadA = 0.0f;
  gLastTripMs = 0;

  gBtnEnabled = false;
  gBtnPin = 255;
  gBtnActiveLow = true;
  gBtnHoldMs = 2000;
  gBtnHoldStartMs = 0;
}

void setResetButton(uint8_t pin, bool activeLow, uint32_t holdMs) {
  gBtnEnabled = true;
  gBtnPin = pin;
  gBtnActiveLow = activeLow;
  gBtnHoldMs = holdMs;
  gBtnHoldStartMs = 0;

  // If using active-low, internal pull-up is usually desired.
  // If your hardware already has pullups/pulldowns, you can change this.
  pinMode(gBtnPin, activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

static bool isButtonPressed() {
  if (!gBtnEnabled || gBtnPin == 255) return false;
  int v = digitalRead(gBtnPin);
  return gBtnActiveLow ? (v == LOW) : (v == HIGH);
}

void update(const AdcReadings& adc) {
  gLastLoadA = absf_fast(adc.iload_a);

  // ---------- AUTO RETRY MODE ----------
  // If tripped and NOT latched, wait retryDelayMs then clear trip
  if (gTripped && !gCfg.latch) {
    if (gCfg.retryDelayMs > 0 && (millis() - gLastTripMs) >= gCfg.retryDelayMs) {
      gTripped = false;     // allow load again
      gOverStartMs = 0;     // restart overcurrent timing cleanly
    } else {
      return;               // still in cooldown period, keep load OFF
    }
  }

  // ---------- LATCH MODE ----------
  if (gTripped && gCfg.latch) {
    // Latched: wait for manual reset
    return;
  }

  // ---------- TRIP DETECTION ----------
  if (gLastLoadA > gCfg.trip_A) {
    if (gOverStartMs == 0) gOverStartMs = millis();
    if ((millis() - gOverStartMs) >= gCfg.tripDelayMs) {
      gTripped = true;
      gLastTripMs = millis();
    }
  } else {
    gOverStartMs = 0;
    if (!gCfg.latch) gTripped = false;
  }
}

void serviceButton(const AdcReadings& adc) {
  if (!gBtnEnabled) return;
  if (!gTripped) { gBtnHoldStartMs = 0; return; }

  const bool pressed = isButtonPressed();

  if (pressed) {
    if (gBtnHoldStartMs == 0) gBtnHoldStartMs = millis();
    if (millis() - gBtnHoldStartMs >= gBtnHoldMs) {
      // Safe reset: only if load current is near zero
      (void)tryReset(adc);
      gBtnHoldStartMs = 0;
    }
  } else {
    gBtnHoldStartMs = 0;
  }
}

bool allowLoad() { return !gTripped; }
bool tripped() { return gTripped; }

void forceTrip() {
  gTripped = true;
  gLastTripMs = millis();
  gOverStartMs = 0;
}

bool tryReset(const AdcReadings& adc) {
  float a = absf_fast(adc.iload_a);
  if (a <= gCfg.resetSafe_A) {
    gTripped = false;
    gOverStartMs = 0;
    return true;
  }
  return false;
}

void resetForce() {
  gTripped = false;
  gOverStartMs = 0;
}

float lastLoadA() { return gLastLoadA; }
uint32_t lastTripMillis() { return gLastTripMs; }

} // namespace LoadProt
