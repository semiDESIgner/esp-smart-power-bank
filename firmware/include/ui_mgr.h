#pragma once
#include <Arduino.h>
#include "adc_mgr.h"

namespace UIMgr {
  void begin();         // init TFT + draw static layout
  void reinitLayout();  // redraw static layout (after mode change)
  void shutdown();     // turn off display/backlight before deep sleep
  void setBacklight(bool on);

  // Update only dynamic fields (partial redraw)
  void drawValues(const AdcReadings& d,
                bool chargingStable,
                bool uiReinitPending,
                uint32_t uiSecondsLeft,
                float socPct,
                float inetA,
                float fccmAh,
                float remmAh);

}
