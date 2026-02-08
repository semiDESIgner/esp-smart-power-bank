#include <Arduino.h>
#include "idle_sleep.h"
#include "load_prot.h"
#include "pins.h"
#include "adc_mgr.h"
#include "power_mgr.h"
#include "charge_mgr.h"
#include "ui_mgr.h"
#include "soc_mgr.h"
#include "bt_mgr.h"

static constexpr uint32_t UI_PERIOD_MS        = 1000;
static constexpr uint32_t LCD_REINIT_DELAY_MS = 200;
static bool user_en_charge   = true;
static bool user_en_load_dsg = true;


ADCMgr adc;
AdcReadings adcData;

// ------------------------------------------------------------
// JSON helpers (BT only)  +  Pin status fields
// Newline-delimited JSON (one object per line) => easy app parsing
// ------------------------------------------------------------
static const char* statTextSimple(bool chargingStable) {
  return chargingStable ? "Charging" : "Idle";
}

void printJsonLineFull(const AdcReadings& d) {
  BtMgr::printf(
    "{"
      "\"ver\":1,"
      "\"ms\":%lu,"
      "\"vbat\":%.3f,"
      "\"soc\":%.1f,"
      "\"iload\":%.3f,"
      "\"ichg\":%.3f,"
      "\"idsg\":%.3f,"
      "\"temp\":%.2f,"
      "\"stat\":\"%s\","
      "\"chg\":%d,"
      "\"ui_pending\":%d,"
      "\"ui_left_s\":%lu,"
      "\"inet\":%.3f,"
      "\"fcc\":%d,"
      "\"rem\":%d,"
      "\"pins\":{"
        "\"en_charge\":%d,"
        "\"en_dcdc\":%d,"
        "\"en_relay\":%d,"
        "\"en_load_dsg\":%d,"
        "\"en_bypass\":%d,"
        "\"chg_done\":%d,"
        "\"charging\":%d,"
        "\"btn_sleep\":%d"
      "}"
    "}\n",
    (unsigned long)millis(),
    d.vbat_meas_sys_v,
    SocMgr::soc(),
    d.iload_a,
    d.ibatt_chg_a,
    d.ibatt_dsg_a,
    d.temp_c,
    ChargeMgr::isCharging() ? "Charging" : "Idle",
    ChargeMgr::isCharging() ? 1 : 0,
    ChargeMgr::uiReinitPending() ? 1 : 0,
    (unsigned long)ChargeMgr::uiReinitSecondsLeft(),
    SocMgr::inet(),
    (int)SocMgr::fcc(),
    (int)SocMgr::remaining(),
    digitalRead(PIN_EN_CHARGE),
    digitalRead(PIN_EN_DCDC),
    digitalRead(PIN_EN_RELAY),
    digitalRead(PIN_EN_LOAD_DSG),
    digitalRead(PIN_EN_BYPASS),
    digitalRead(PIN_CHG_DONE),
    digitalRead(PIN_CHARGING),
    digitalRead(PIN_BTN_SLEEP)
  );
}
static void handleBtCommandLine(const char* line) {
  if (!line || line[0] != '{') return;

  String s(line);
  s.replace(" ", "");
  s.replace("\t", "");
  s.replace("\r", "");

  if (s.indexOf("\"cmd\":\"set\"") < 0) return;

  int pinPos = s.indexOf("\"pin\":\"");
  int valPos = s.indexOf("\"val\":");
  if (pinPos < 0 || valPos < 0) return;

  pinPos += 7;
  int pinEnd = s.indexOf("\"", pinPos);
  if (pinEnd < 0) return;

  String pin = s.substring(pinPos, pinEnd);
  int val = s.substring(valPos + 6).toInt();

  // We'll NOT digitalWrite here for EN_LOAD_DSG (see Fix C below)
  if (pin == "en_charge") {
  user_en_charge = (val != 0);
}
else if (pin == "en_load_dsg") {
  user_en_load_dsg = (val != 0);
}

}


void setup() {
  BtMgr::begin("Prototype");
  delay(300);
  PowerMgr::begin();
  pinMode(PIN_CHARGING, INPUT);
  pinMode(PIN_CHG_DONE, INPUT_PULLUP);
  IdleSleep::begin();
  // During BT debugging you can keep this OFF; enable later if needed
  // IdleSleep::handleWakeReasonOrSleep();
  const bool startCharging = (digitalRead(PIN_CHARGING) == HIGH);
  ChargeMgr::begin(startCharging);
  // Relay/Power rules
  PowerMgr::applyChargingMode(ChargeMgr::isCharging());
  // ADC
  adc.begin();
  adc.setZeroOffsetsMv(0, 0, 0);
  // Load protection
  LoadProt::Config lp;
  lp.trip_A       = 0.600f;
  lp.tripDelayMs  = 150;
  lp.resetSafe_A  = 0.050f;
  lp.latch        = false;        // auto retry enabled
  lp.retryDelayMs = 10000;        // 10 seconds OFF then ON
  LoadProt::begin(lp);
  LoadProt::setResetButton(PIN_BTN_SLEEP, true, 2000); // activeLow, 2s
  // NTC params
  adc.setNtcParams(
    10000.0f,   // Rfixed
    10000.0f,   // R25
    4250.0f     // Beta
  );
  UIMgr::begin();
  SocMgr::begin(2000.0f);
  adc.startTimer(2000, 64);
  UIMgr::drawValues(adcData,
                    ChargeMgr::isCharging(),
                    ChargeMgr::uiReinitPending(),
                    ChargeMgr::uiReinitSecondsLeft(),
                    SocMgr::soc(),
                    SocMgr::inet(),
                    SocMgr::fcc(),
                    SocMgr::remaining());
}

void loop() {
  static char rxLine[256];

if (BtMgr::connected()) {
  while (BtMgr::available()) {
    size_t n = BtMgr::readLine(rxLine, sizeof(rxLine));
    if (n > 0) {
      handleBtCommandLine(rxLine);
    } else {
      // no full line yet
      break;
    }
  }
}
  static uint32_t lastUiMs = 0;
  // ---- Charge manager update ----
  const bool rawCharging = (digitalRead(PIN_CHARGING) == HIGH);
  const bool rawFull     = (digitalRead(PIN_CHG_DONE) == LOW);
  ChargeMgr::update(rawCharging);
  // Relay rule: ON only when charging (debounced stable)
  PowerMgr::applyChargingMode(ChargeMgr::isCharging());
  // UI re-init scheduling
  if (ChargeMgr::stableChanged()) {
    ChargeMgr::scheduleUiReinit(LCD_REINIT_DELAY_MS);
  }
  if (ChargeMgr::uiReinitDue()) {
    UIMgr::reinitLayout();
  }
  // ---- ADC sampling (timer scheduled, non-blocking) ----
  adc.service(3);
  if (adc.fetchLatest(adcData)) {
    // 1) Load protection
    LoadProt::update(adcData);
    LoadProt::serviceButton(adcData);
    // 2) SOC
    SocMgr::update(adcData, rawCharging, false, rawFull);
    // 3) Load enable decision

const bool allowLoad = (SocMgr::soc() > 0.0f) && LoadProt::allowLoad();

// EN_CHARGE = user control (simple)
digitalWrite(PIN_EN_CHARGE, user_en_charge ? HIGH : LOW);

// EN_LOAD_DSG = user control AND safety
const bool finalLoadEnable = user_en_load_dsg && allowLoad;
digitalWrite(PIN_EN_LOAD_DSG, finalLoadEnable ? HIGH : LOW);

    // 4) Sleep update
    IdleSleep::update(adcData, ChargeMgr::isCharging());
    // 5) UI + BT JSON output (1Hz)
    if (millis() - lastUiMs >= UI_PERIOD_MS) {
      lastUiMs = millis();
      UIMgr::drawValues(adcData,
                        ChargeMgr::isCharging(),
                        ChargeMgr::uiReinitPending(),
                        ChargeMgr::uiReinitSecondsLeft(),
                        SocMgr::soc(),
                        SocMgr::inet(),
                        SocMgr::fcc(),
                        SocMgr::remaining());

      if (BtMgr::connected()) {
        printJsonLineFull(adcData);
      }
    }
 
}

  }

