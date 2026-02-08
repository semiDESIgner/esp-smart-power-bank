#include "ui_mgr.h"
#include <TFT_eSPI.h>
#include <math.h>
#include "bt_mgr.h"


#ifndef TFT_BL
  #define TFT_BL 16
#endif

namespace UIMgr {

static TFT_eSPI tft;

// NA thresholds for what you show on screen (not for SOC math)
static constexpr float CHG_NA_LIMIT_A  = 0.218f; // ~200mA
static constexpr float DSG_NA_LIMIT_A  = 0.218f; // ~200mA
static constexpr float LOAD_NA_LIMIT_A = 0.120f; // ~120mA

// Layout constants
static constexpr int LABEL_X = 10;
static constexpr int VAL_X   = 90;
static constexpr int VAL_W   = 220;
static constexpr int LINE_H  = 20;

// Row Y positions
static constexpr int Y_VIN   = 10;
static constexpr int Y_SOC   = 35;
static constexpr int Y_LOAD  = 60;
static constexpr int Y_CHG   = 85;
static constexpr int Y_DSG   = 110;
static constexpr int Y_TEMP  = 135;
static constexpr int Y_STAT  = 160;

// Debug lines (optional)
static constexpr int Y_DBG1  = 185;
static constexpr int Y_DBG2  = 205;

void setBacklight(bool on) {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

void shutdown() {
  // 1) Turn off backlight (biggest saver)
  setBacklight(false);

  // 2) Put LCD controller to sleep
  // NOTE: this works only if the same TFT instance is used
  tft.writecommand(0x28); // display OFF
  tft.writecommand(0x10); // sleep in
  delay(120);
}

static void drawStaticLayout() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.setCursor(LABEL_X, Y_VIN);  tft.print("VBAT:");
  tft.setCursor(LABEL_X, Y_SOC);  tft.print("SOC :");
  tft.setCursor(LABEL_X, Y_LOAD); tft.print("Load:");
  tft.setCursor(LABEL_X, Y_CHG);  tft.print("Chg :");
  tft.setCursor(LABEL_X, Y_DSG);  tft.print("Dsg :");
  tft.setCursor(LABEL_X, Y_TEMP); tft.print("Temp:");
  tft.setCursor(LABEL_X, Y_STAT); tft.print("Stat:");

  // Debug labels
  tft.setTextSize(1);
  tft.setCursor(LABEL_X, Y_DBG1); tft.print("Inet:");
  tft.setCursor(LABEL_X, Y_DBG2); tft.print("FCC/REM:");

  tft.setTextSize(2);
}

static void drawValueField(int y, const char* text) {
  tft.fillRect(VAL_X, y, VAL_W, LINE_H, TFT_BLACK);
  tft.setCursor(VAL_X, y);
  tft.print(text);
}

void begin() {
  setBacklight(true);
  tft.init();
  tft.setRotation(1);
  drawStaticLayout();
}

void reinitLayout() {
  tft.init();
  tft.setRotation(1);
  drawStaticLayout();
}

static void drawCurrentOrNA(int y, float current_a, float na_limit_a) {
  char buf[24];
  if (fabsf(current_a) < na_limit_a) {
    drawValueField(y, "NA");
  } else {
    snprintf(buf, sizeof(buf), "%.3f A", current_a);
    drawValueField(y, buf);
  }
}
static void fmtCurrentOrNA(char* out, size_t n, float current_a, float na_limit_a) {
  if (fabsf(current_a) < na_limit_a) {
    snprintf(out, n, "NA");
  } else {
    snprintf(out, n, "%.3f A", current_a);
  }
}

void drawValues(const AdcReadings& d,
                bool chargingStable,
                bool pending,
                uint32_t secondsLeft,
                float socPct,
                float inetA,
                float fccmAh,
                float remmAh)
{
  char buf[40];

  // VBAT
  snprintf(buf, sizeof(buf), "%.3f V", d.vbat_meas_sys_v);
  drawValueField(Y_VIN, buf);

  // SOC
  snprintf(buf, sizeof(buf), "%.1f %%", socPct);
  drawValueField(Y_SOC, buf);

  // Currents (show NA for low readings on UI only)
  drawCurrentOrNA(Y_LOAD, d.iload_a,     LOAD_NA_LIMIT_A);
  drawCurrentOrNA(Y_CHG,  d.ibatt_chg_a, CHG_NA_LIMIT_A);
  drawCurrentOrNA(Y_DSG,  d.ibatt_dsg_a, DSG_NA_LIMIT_A);

  // Temperature
  if (isnan(d.temp_c)) {
    drawValueField(Y_TEMP, "---");
  } else {
    snprintf(buf, sizeof(buf), "%.1f C", d.temp_c);
    drawValueField(Y_TEMP, buf);
  }

  // Status
  if (pending) {
    snprintf(buf, sizeof(buf), "%s (%lus)",
             chargingStable ? "Charging" : "Idle",
             (unsigned long)secondsLeft);
  } else {
    snprintf(buf, sizeof(buf), "%s", chargingStable ? "Charging" : "Idle");
  }
  drawValueField(Y_STAT, buf);

  // Debug values
  tft.setTextSize(1);

  snprintf(buf, sizeof(buf), "%+.3f A", inetA);
  drawValueField(Y_DBG1, buf);

  snprintf(buf, sizeof(buf), "%.0f/%.0f", fccmAh, remmAh);
  drawValueField(Y_DBG2, buf);
  /*if (BtMgr::connected()) {
    char line[64];
    char tmp[24];

    // VBAT
    snprintf(line, sizeof(line), "VBAT: %.3f V", d.vbat_meas_sys_v);
    BtMgr::println(line);

    // SOC
    snprintf(line, sizeof(line), "SOC : %.1f %%", socPct);
    BtMgr::println(line);

    // Load/Chg/Dsg
    fmtCurrentOrNA(tmp, sizeof(tmp), d.iload_a, LOAD_NA_LIMIT_A);
    snprintf(line, sizeof(line), "Load: %s", tmp);
    BtMgr::println(line);

    fmtCurrentOrNA(tmp, sizeof(tmp), d.ibatt_chg_a, CHG_NA_LIMIT_A);
    snprintf(line, sizeof(line), "Chg : %s", tmp);
    BtMgr::println(line);

    fmtCurrentOrNA(tmp, sizeof(tmp), d.ibatt_dsg_a, DSG_NA_LIMIT_A);
    snprintf(line, sizeof(line), "Dsg : %s", tmp);
    BtMgr::println(line);

    // Temp
    if (isnan(d.temp_c)) snprintf(line, sizeof(line), "Temp: ---");
    else                snprintf(line, sizeof(line), "Temp: %.1f C", d.temp_c);
    BtMgr::println(line);

    // Stat
    if (pending) {
      snprintf(line, sizeof(line), "Stat: %s (%lus)",
               chargingStable ? "Charging" : "Idle",
               (unsigned long)secondsLeft);
    } else {
      snprintf(line, sizeof(line), "Stat: %s", chargingStable ? "Charging" : "Idle");
    }
    BtMgr::println(line);

    // Inet
    snprintf(line, sizeof(line), "Inet: %+.3f A", inetA);
    BtMgr::println(line);

    // FCC/REM
    snprintf(line, sizeof(line), "FCC/REM: %.0f/%.0f", fccmAh, remmAh);
    BtMgr::println(line);

    BtMgr::println("---");
  }*/

  tft.setTextSize(2);
}

} // namespace UIMgr
