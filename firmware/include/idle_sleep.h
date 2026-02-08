#pragma once
#include <Arduino.h>
#include "adc_mgr.h"

namespace IdleSleep {
  void begin();
  void update(const AdcReadings& d, bool chargingStable);

  // After wake: accept wake only if
  // - charging pin woke us, OR
  // - button is held LOW for 3 seconds
  bool handleWakeReasonOrSleep();

  void onButtonPressed(); // optional hook
}
