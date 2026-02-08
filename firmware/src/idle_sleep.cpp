#include "idle_sleep.h"
#include "pins.h"
#include "power_mgr.h"
#include "ui_mgr.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include <math.h>
#include "driver/gpio.h"


namespace IdleSleep {

static constexpr uint32_t IDLE_TIMEOUT_MS = 10*60UL * 1000UL;
static constexpr uint32_t WAKE_HOLD_MS    = 3000;
static constexpr float    LOAD_NA_A       = 0.112f; // 100mA = NA threshold

static uint32_t s_idleStartMs = 0;
static bool     s_idleCounting = false;

static bool loadIsNA(const AdcReadings& d) {
  if (isnan(d.iload_a)) return true;
  return fabsf(d.iload_a) < LOAD_NA_A;
}

static void prepareOutputsForSleep() {
  // REQUIRED sleep-state outputs
  pinMode(PIN_EN_BYPASS, OUTPUT);
  digitalWrite(PIN_EN_BYPASS, HIGH);     // PATH_EN HIGH

  pinMode(PIN_EN_RELAY, OUTPUT);
  digitalWrite(PIN_EN_RELAY, HIGH);      // RELAY_EN HIGH

  pinMode(PIN_EN_DCDC, OUTPUT);
  digitalWrite(PIN_EN_DCDC, LOW);        // DCDC_EN OFF

  pinMode(PIN_EN_LOAD_DSG, OUTPUT);
  digitalWrite(PIN_EN_LOAD_DSG, LOW);    // LOAD_OUT LOW
}

static void configureWakeSources() {
  // Wake if charging pin goes HIGH
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_CHARGING, 1);

  // Wake if button goes LOW (single-pin ext1, ALL_LOW == that pin LOW)
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN_SLEEP, ESP_EXT1_WAKEUP_ALL_LOW);

  // Button idle HIGH => keep pullup in RTC domain so it doesn't float in sleep
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_SLEEP);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_SLEEP);

  // Charge detect pin usually push-pull; keep pulls off unless you need them
  rtc_gpio_pullup_dis((gpio_num_t)PIN_CHARGING);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_CHARGING);
}
static void rtcHoldOut(int pin, int level) {
  gpio_num_t gp = (gpio_num_t)pin;

  // Only works for RTC-capable GPIOs
  if (!rtc_gpio_is_valid_gpio(gp)) return;

  // Make it RTC output, set level, then hold
  rtc_gpio_init(gp);
  rtc_gpio_set_direction(gp, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level(gp, level);
  rtc_gpio_hold_en(gp);
}

static void rtcUnhold(int pin) {
  gpio_num_t gp = (gpio_num_t)pin;
  if (!rtc_gpio_is_valid_gpio(gp)) return;
  rtc_gpio_hold_dis(gp);
}

static void enterDeepSleepNow() {
  UIMgr::shutdown();
  prepareOutputsForSleep();
  configureWakeSources();

  // LATCH output states during deep sleep (RTC HOLD)
  rtcHoldOut(PIN_EN_BYPASS,   1);  // PATH_EN HIGH
  rtcHoldOut(PIN_EN_RELAY,    1);  // RELAY_EN HIGH
  rtcHoldOut(PIN_EN_DCDC,     0);  // DCDC_EN LOW
  rtcHoldOut(PIN_EN_LOAD_DSG, 0);  // LOAD_OUT LOW

  // Global deep-sleep hold enable
  gpio_deep_sleep_hold_en();

  Serial.flush();
  delay(30);
  esp_deep_sleep_start();
}


void begin() {

  pinMode(PIN_BTN_SLEEP, INPUT_PULLUP); // idle HIGH, pressed LOW
  s_idleStartMs = 0;
  s_idleCounting = false;
    // Release any holds from previous sleep
  rtcUnhold(PIN_EN_BYPASS);
  rtcUnhold(PIN_EN_RELAY);
  rtcUnhold(PIN_EN_DCDC);
  rtcUnhold(PIN_EN_LOAD_DSG);
  gpio_deep_sleep_hold_dis();

}

void onButtonPressed() {
  s_idleCounting = false;
  s_idleStartMs = 0;
}

bool handleWakeReasonOrSleep() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // If woke due to charge detect -> accept wake
  if (cause == ESP_SLEEP_WAKEUP_EXT0) return true;

  // If woke due to button -> accept ONLY if held LOW for 3 seconds
  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t mask = esp_sleep_get_ext1_wakeup_status();
    if (mask & (1ULL << PIN_BTN_SLEEP)) {
      uint32_t t0 = millis();
      while (millis() - t0 < WAKE_HOLD_MS) {
        if (digitalRead(PIN_BTN_SLEEP) == HIGH) {
          // released too early -> go back to sleep
          enterDeepSleepNow();
          return false;
        }
        delay(10);
      }
      return true; // held LOW for full 3s
    }
  }

  return true; // power-on reset, etc.
}

void update(const AdcReadings& d, bool chargingStable) {
  const bool condIdle =
      loadIsNA(d) &&
      (chargingStable == false) &&
      (digitalRead(PIN_BTN_SLEEP) == HIGH);

  if (!condIdle) {
    s_idleCounting = false;
    s_idleStartMs = 0;
    return;
  }

  if (!s_idleCounting) {
    s_idleCounting = true;
    s_idleStartMs = millis();
    return;
  }

  if ((millis() - s_idleStartMs) >= IDLE_TIMEOUT_MS) {
    enterDeepSleepNow();
  }
}

} // namespace IdleSleep
