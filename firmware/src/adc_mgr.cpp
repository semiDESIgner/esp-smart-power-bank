#include "adc_mgr.h"
#include "pins.h"
#include <math.h>
#include "esp_timer.h"

static esp_timer_handle_t s_adc_timer = nullptr;

// Channel order MUST match your prints and logic
static constexpr int NUM_CH = 5;
static const int CH_PINS[NUM_CH] = {
  PIN_ADC_VOLT,
  PIN_ADC_NTC,
  PIN_ADC_LOAD_DSG,
  PIN_ADC_BATT_CHG,
  PIN_ADC_BATT_DSG
};

static void timerCb(void *arg) {
  // Timer context: DO NOT read ADC here. Just schedule.
  ADCMgr *self = reinterpret_cast<ADCMgr*>(arg);
  self->service(0); // no-op safety (optional)
  // We can’t call member safely here unless it's IRAM etc.
  // So we schedule via a static: easiest is to increment pending ticks via a friend,
  // but we’ll do it by calling a small method that only increments a volatile counter.
  // (We’ll expose onTick via a lambda-like wrapper below.)
}

static void timerCbThin(void *arg) {
  ADCMgr *self = reinterpret_cast<ADCMgr*>(arg);
  // only increments a counter
  self->startTimer(0,0); // placeholder (won't be used)
}

void ADCMgr::begin() {
  analogReadResolution(12);

  // ADC attenuation: 11dB gives range 0-3.9V with ~150mV sensitivity loss
  // ESP32 uses internal 1.1V reference. With VDD = 3V, readings should auto-scale correctly
  analogSetPinAttenuation(PIN_ADC_VOLT,     ADC_11db);
  analogSetPinAttenuation(PIN_ADC_NTC,      ADC_11db);
  analogSetPinAttenuation(PIN_ADC_LOAD_DSG, ADC_11db);
  analogSetPinAttenuation(PIN_ADC_BATT_CHG, ADC_11db);
  analogSetPinAttenuation(PIN_ADC_BATT_DSG, ADC_11db);
}

void ADCMgr::setZeroOffsetsMv(int load0_mv, int bchg0_mv, int bdsg0_mv) {
  zero_load_mv = load0_mv;
  zero_bchg_mv = bchg0_mv;
  zero_bdsg_mv = bdsg0_mv;
}

void ADCMgr::setNtcParams(float r_fixed_ohm, float r0_ohm, float beta) {
  ntc_r_fixed = r_fixed_ohm;
  ntc_r0      = r0_ohm;
  ntc_beta    = beta;
}

int ADCMgr::readMilliVoltsAvg(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(pin);
  }
  return (int)(sum / samples);
}

float ADCMgr::currentFromMv(int mv, float gain) {
  return (mv / 1000.0f) / (gain * SHUNT_SENSE_OHMS);
}


float ADCMgr::ntcTempFromMv(int mv_node)
{
    const float VREF = 3.0f;
    const float T0   = 298.15f;   // 25°C in Kelvin

    float v = mv_node / 1000.0f;

    // enforce ADC validity
    if (v <= 0.150f || v >= (VREF - 0.01f))
        return NULL;                                             /// changed the Nan to NULL

    // Divider: Rfixed on top, NTC to GND
    float r_ntc = ntc_r_fixed * (v / (VREF - v));

    // Beta equation
    float invT =
        (1.0f / T0) +
        (1.0f / ntc_beta) * logf(r_ntc / ntc_r0);

    return (1.0f / invT) - 273.15f;
}


void ADCMgr::sample(AdcReadings &out, int samples) {
  // OLD blocking method kept (unchanged style, but uses your constants)
  out.mv_vmid_sys = readMilliVoltsAvg(PIN_ADC_VOLT, samples);
  out.mv_ntc_sys  = readMilliVoltsAvg(PIN_ADC_NTC,  samples);

  out.vbat_meas_sys_v = (out.mv_vmid_sys / 1000.0f) * VIN_SCALE;
   out.vbat_meas_sys_v=out.vbat_meas_sys_v+0.037;                              //calibration offset  for the Vbattery is 37mV manually added

  out.temp_c = ntcTempFromMv(out.mv_ntc_sys);

  int raw_load = readMilliVoltsAvg(PIN_ADC_LOAD_DSG, samples);
  int raw_bchg = readMilliVoltsAvg(PIN_ADC_BATT_CHG, samples);
  int raw_bdsg = readMilliVoltsAvg(PIN_ADC_BATT_DSG, samples);

  // Use YOUR intended floor compensation (you had it but weren’t using it)
  out.mv_load = applyZeroAndFloor(raw_load, zero_load_mv);
  out.mv_bchg = applyZeroAndFloor(raw_bchg, zero_bchg_mv);
  out.mv_bdsg = applyZeroAndFloor(raw_bdsg, zero_bdsg_mv);

  out.iload_a     = max(0.0f, currentFromMv(out.mv_load, GAIN_LOAD) + 0.015f);
  out.ibatt_chg_a = max(0.0f, currentFromMv(out.mv_bchg, GAIN_BATT) + 0.020f);
  out.ibatt_dsg_a = max(0.0f, currentFromMv(out.mv_bdsg, GAIN_BATT) + 0.040f);
}

// ---- Timer-driven part ----
void ADCMgr::onTick() {
  pending_ticks_++;
}

void adcTickCb(void *arg) {
  reinterpret_cast<ADCMgr*>(arg)->onTick();
}


bool ADCMgr::startTimer(uint32_t tick_us, int samples_per_channel) {
  stopTimer();

  if (samples_per_channel <= 0) samples_per_channel = 1;
  samples_per_ch_ = samples_per_channel;

  // reset state
  pending_ticks_ = 0;
  ch_ = 0;
  samp_ = 0;
  for (int i = 0; i < NUM_CH; i++) {
    sum_[i] = 0;
    latest_mv_[i] = 0;
  }
  latest_ready_ = false;

  if (tick_us == 0) tick_us = 2000; // default safe

  esp_timer_create_args_t args = {};
  args.callback = &adcTickCb;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "adc_sched";

  if (esp_timer_create(&args, &s_adc_timer) != ESP_OK) {
    s_adc_timer = nullptr;
    return false;
  }
  if (esp_timer_start_periodic(s_adc_timer, tick_us) != ESP_OK) {
    esp_timer_delete(s_adc_timer);
    s_adc_timer = nullptr;
    return false;
  }
  return true;
}

void ADCMgr::stopTimer() {
  if (s_adc_timer) {
    esp_timer_stop(s_adc_timer);
    esp_timer_delete(s_adc_timer);
    s_adc_timer = nullptr;
  }
}

void ADCMgr::service(uint8_t max_reads_per_call) {
  if (max_reads_per_call == 0) return;

  // Consume a limited number of scheduled ticks to keep loop responsive
  while (pending_ticks_ > 0 && max_reads_per_call--) {
    pending_ticks_--;

    // One ADC read per tick
    delayMicroseconds(esp_random() & 0x3FF); // jitter
int mv = analogReadMilliVolts(CH_PINS[ch_]);

    sum_[ch_] += (uint32_t)mv;

    samp_++;
    if (samp_ >= samples_per_ch_) {
      // finalize this channel
      latest_mv_[ch_] = (int)(sum_[ch_] / (uint32_t)samples_per_ch_);
      sum_[ch_] = 0;

      samp_ = 0;
      ch_++;

      if (ch_ >= NUM_CH) {
        ch_ = 0;
        latest_ready_ = true;
      }
    }
  }
}

bool ADCMgr::fetchLatest(AdcReadings &out) {
  if (!latest_ready_) return false;
  latest_ready_ = false;

  // Map channels
  const int mv_vmid = latest_mv_[0];
  const int mv_ntc  = latest_mv_[1];
  const int raw_load = latest_mv_[2];
  const int raw_bchg = latest_mv_[3];
  const int raw_bdsg = latest_mv_[4];

  out.mv_vmid_sys = mv_vmid;
  out.mv_ntc_sys  = mv_ntc;

  out.vbat_meas_sys_v = (out.mv_vmid_sys / 1000.0f) * VIN_SCALE;
  out.temp_c = ntcTempFromMv(out.mv_ntc_sys);

  // Use YOUR intended floor compensation
  out.mv_load = applyZeroAndFloor(raw_load, zero_load_mv);
  out.mv_bchg = applyZeroAndFloor(raw_bchg, zero_bchg_mv);
  out.mv_bdsg = applyZeroAndFloor(raw_bdsg, zero_bdsg_mv);
int mv = analogReadMilliVolts(CH_PINS[ch_]);  // ← This uses 3.3V calibration
  out.iload_a     = max(0.0f, currentFromMv(out.mv_load, GAIN_LOAD) + 0.015f);
  out.ibatt_chg_a = max(0.0f, currentFromMv(out.mv_bchg, GAIN_BATT) + 0.020f);
  out.ibatt_dsg_a = max(0.0f, currentFromMv(out.mv_bdsg, GAIN_BATT) + 0.040f);

  return true;
}
