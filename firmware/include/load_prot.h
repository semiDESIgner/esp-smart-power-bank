#pragma once
#include <Arduino.h>
#include "adc_mgr.h"

namespace LoadProt {

struct Config {
  float trip_A = 0.600f;          // 600 mA
  uint32_t tripDelayMs = 150;     // must be above threshold this long
  float resetSafe_A = 0.050f;     // only allow reset if load is below this
  bool latch = false;            // <--- change default to non-latch for auto-retry
  uint32_t retryDelayMs = 10000; // <--- NEW: 10 seconds OFF then try ON again
};


void begin(const Config& cfg = Config{});

// Call this whenever you have a fresh AdcReadings update
void update(const AdcReadings& adc);

// ---- Button reset support ----
void setResetButton(uint8_t pin, bool activeLow = true, uint32_t holdMs = 2000);
void serviceButton(const AdcReadings& adc);  // call often (every loop)

// Status / control
bool allowLoad();
bool tripped();

void forceTrip();
bool tryReset(const AdcReadings& adc);
void resetForce();

float lastLoadA();
uint32_t lastTripMillis();

} // namespace LoadProt
