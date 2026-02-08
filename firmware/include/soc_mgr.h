#pragma once
#include <Arduino.h>
#include "adc_mgr.h"

namespace SocMgr {

  void begin(float batteryCapacity_mAh);
  void update(const AdcReadings& a, bool isCharging, bool isSleeping, bool isFull);

  // Call every time ADC updates
  void update(const AdcReadings& adc,
              bool isCharging,
              bool isSleeping);

  float soc();        // 0–100 %
  float remaining();  // mAh (REM = USED mAh, starts at 0 and increases with discharge)

  float fcc();        // ✅ ADD
  float inet();       // ✅ ADD
}
