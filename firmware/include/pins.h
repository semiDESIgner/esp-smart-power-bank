#pragma once
#include <Arduino.h>

// ===== ENABLE / CONTROL PINS (ACTIVE-HIGH) =====
static constexpr int PIN_EN_CHARGE    = 4;   // Charge enable
static constexpr int PIN_EN_DCDC      = 14;  // DC-DC enable
static constexpr int PIN_EN_RELAY     = 27;  // Relay enable change 
static constexpr int PIN_EN_LOAD_DSG  = 13;  // Load discharge enable
static constexpr int PIN_EN_BYPASS    = 2;   // Bypass path enable
static constexpr int PIN_CHG_DONE = 22; 
// ===== INPUTS =====
static constexpr int PIN_CHARGING = 25;      // Charging status input (HIGH = charging)
static constexpr int PIN_BTN_SLEEP  = 26; 
// ===== ADC PINS =====
static constexpr int PIN_ADC_VOLT      = 34;
static constexpr int PIN_ADC_LOAD_DSG  = 33;
static constexpr int PIN_ADC_BATT_CHG  = 39;
static constexpr int PIN_ADC_BATT_DSG  = 36;
static constexpr int PIN_ADC_NTC       = 35; // placeholder until you confirm
// Measure Battery P- (battery negative before shunts) vs System GND
//static constexpr int PIN_ADC_PMINUS = 27;  // <-- example, change to your GPIO   


// ===== TFT =====
#ifndef TFT_BL
  #define TFT_BL 16
#endif
