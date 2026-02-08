#include "soc_mgr.h"
#include <Preferences.h>

namespace SocMgr {

// ===============================
// USER SYSTEM CONSTANTS
// ===============================
static constexpr float I_ACTIVE_A    = 0.180f;   // LCD + MCU
static constexpr float I_CHG_SELF_A  = 0.000f;   // MCU during charging CCCCChanged
static constexpr float I_SLEEP_A     = 0.003f;

static constexpr float ADC_MIN_A     = 0.200f;

static constexpr float VBAT_FULL     = 4.100f;    ///CCCChanged
static constexpr float VBAT_EMPTY    = 3.20f;

static constexpr uint32_t EMPTY_TIME_MS = 15000;
static constexpr uint32_t SAVE_MS       = 30000;

// ===============================

static Preferences prefs;

static float FCC_mAh  = 2000.0f;
static float used_mAh = 0.0f;     // REM display: used/consumed since full (starts at 0)
static float soc_pct  = 100.0f;
static float I_net_A  = 0.0f;     // ✅ store net current for debug

static uint32_t last_ms   = 0;
static uint32_t last_save = 0;

static bool prevCharging     = false;
static uint32_t emptyTimer   = 0;

static float clamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void recalc() {
  used_mAh = clamp(used_mAh, 0, FCC_mAh);

  // SOC is what remains: 100% when used=0, 0% when used=FCC
  soc_pct = 100.0f * (FCC_mAh - used_mAh) / FCC_mAh;
  soc_pct = clamp(soc_pct, 0, 100);
}

// ===============================

void begin(float capacity_mAh) {

  FCC_mAh = capacity_mAh;

  prefs.begin("soc", false);

  float storedFcc = prefs.getFloat("fcc", FCC_mAh);
  if (storedFcc >= 100.0f && storedFcc <= 10000.0f) FCC_mAh = storedFcc;

  // Migration:
  // - Old firmware stored "rem" = remaining mAh.
  // - New firmware stores "used" = consumed mAh (REM display).
  if (prefs.isKey("used")) {
    used_mAh = prefs.getFloat("used", 0.0f);
  } else {
    float oldRem = prefs.getFloat("rem", FCC_mAh); // assume full if missing
    used_mAh = clamp(FCC_mAh - oldRem, 0, FCC_mAh);
  }

  last_ms   = millis();
  last_save = last_ms;

  recalc();

  // ensure keys exist
  prefs.putFloat("fcc",  FCC_mAh);
  prefs.putFloat("used", used_mAh);
  prefs.putFloat("soc",  soc_pct);
}

float soc() {
  return soc_pct;
}

float remaining() {
  // NOTE: This is "REM" on the LCD: USED/CONSUMED mAh from last full.
  return used_mAh;
}

float fcc() {
  return FCC_mAh;
}

float inet() {
  return I_net_A;
}

// ===============================

void update(const AdcReadings& a, bool isCharging, bool isSleeping, bool isFull)
{
  uint32_t now = millis();
  float dt = (now - last_ms) / 1000.0f;
  last_ms = now;

  if (dt < 0) dt = 0;
  if (dt > 5) dt = 5;

  // --- FULL latch (debounced) ---
  static bool fullLatched = false;
  static uint32_t fullMs = 0;
  static constexpr uint32_t FULL_HOLD_MS = 3000; // 3s stable

  if (!isCharging) {
    fullLatched = false;
    fullMs = 0;
  } else {
    if (isFull && a.vbat_meas_sys_v >= VBAT_FULL) {
      if (fullMs == 0) fullMs = now;
      if (!fullLatched && (now - fullMs) >= FULL_HOLD_MS) {
        used_mAh = 0.0f;   // full => nothing used
        recalc();
        fullLatched = true;
      }
    } else {
      fullMs = 0;
    }
  }

  // -----------------------------
  // CHARGE CURRENT
  // -----------------------------
  float I_chg = 0;

  if (isCharging && a.ibatt_chg_a >= ADC_MIN_A)
    I_chg = a.ibatt_chg_a;

  // -----------------------------
  // DISCHARGE CURRENT
  // -----------------------------
  float I_dsg = 0;

  if (isSleeping) {
    I_dsg = I_SLEEP_A;
  }
  else if (isCharging) {
    // If discharge isn't measurable, assume battery is NOT discharging
    I_dsg = 0.0f;  ///// changeddddd
  }
  else {
    I_dsg = (a.ibatt_dsg_a >= ADC_MIN_A)
              ? a.ibatt_dsg_a
              : I_ACTIVE_A;
  }

  // -----------------------------
  // COULOMB COUNT (USED mAh)
  // -----------------------------
  float I_net = I_chg - I_dsg;
  I_net_A = I_net;   // ✅ save for LCD/CSV debug

  // Net discharge increases used_mAh; net charge decreases it
  float d_used_mAh = (I_dsg - I_chg) * dt * (1000.0f / 3600.0f);
  used_mAh += d_used_mAh;

  // -----------------------------
  // DYNAMIC FCC based on discharge current
  // y = -280x + 2130
  // -----------------------------
  if (!isCharging && !isSleeping) {
    float I_for_fcc = (a.ibatt_dsg_a >= ADC_MIN_A) ? a.ibatt_dsg_a : I_ACTIVE_A;

    float newFCC = (-280.0f * I_for_fcc) + 2130.0f;

    // Clamp to sane range so FCC doesn't go crazy
    newFCC = clamp(newFCC, 1200.0f, 2130.0f);

    FCC_mAh = newFCC;

    // Keep used mAh within new FCC
    used_mAh = clamp(used_mAh, 0, FCC_mAh);
  }

  recalc();

  // -----------------------------
  // EMPTY CALIBRATION
  // -----------------------------
  if (!isCharging && a.vbat_meas_sys_v <= VBAT_EMPTY) {
    if (emptyTimer == 0)
      emptyTimer = now;

    if (now - emptyTimer > EMPTY_TIME_MS) {
      used_mAh = FCC_mAh; // empty => all used
      recalc();
    }
  }
  else {
    emptyTimer = 0;
  }

  // -----------------------------
  // SAVE TO FLASH
  // -----------------------------
  if (now - last_save > SAVE_MS) {
    last_save = now;
    prefs.putFloat("fcc",  FCC_mAh);
    prefs.putFloat("used", used_mAh);
    prefs.putFloat("soc",  soc_pct);
  }
}

} // namespace
