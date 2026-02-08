#pragma once
#include <Arduino.h>
#include <math.h>

struct AdcReadings {
  int mv_vmid_sys = 0;
  int mv_ntc_sys  = 0;

  float vbat_meas_sys_v = 0.0f;
  float temp_c = NAN;

  int mv_load = 0;
  int mv_bchg = 0;
  int mv_bdsg = 0;

  float iload_a     = 0.00f;
  float ibatt_chg_a = 0.00f;
  float ibatt_dsg_a = 0.00f;
};

class ADCMgr {
public:
  void begin();

  // OLD blocking API (keep it if you want, but you won’t use it in main loop anymore)
  void sample(AdcReadings &out, int samples = 16);

  void setZeroOffsetsMv(int load0_mv, int bchg0_mv, int bdsg0_mv);
  void setNtcParams(float r_fixed_ohm, float r0_ohm, float beta);

  void setAdcFloorMv(int floor_mv) { adc_floor_mv = floor_mv; }
  void setPminusClampMv(int mv) { pminus_clamp_mv = mv; }

  // -------- NEW: timer-driven non-blocking ADC --------
  bool startTimer(uint32_t tick_us = 2000, int samples_per_channel = 64);
  void stopTimer();

  // Call frequently from loop() - consumes scheduled ticks and does a few ADC reads
  void service(uint8_t max_reads_per_call = 3);

  // If a full averaged set is ready, copy into out and return true
  bool fetchLatest(AdcReadings &out);

private:
  int zero_load_mv = 0;
  int zero_bchg_mv = 0;
  int zero_bdsg_mv = 0;

  int adc_floor_mv = 0;
  int pminus_clamp_mv = 3;

  // ---- Hardware constants (FROM YOUR FILES) ----
  static constexpr float VIN_SCALE = 2.0f;
  static constexpr float SHUNT_SENSE_OHMS = 0.050f;
  static constexpr float GAIN_LOAD = 29.5f;
  static constexpr float GAIN_BATT = 16.0f;
float ntc_r_fixed = 10000.0f;   // Rfixed = 10k
float ntc_r0      = 10000.0f;   // R25 = 10k
float ntc_beta    = 4250.0f;    // datasheet beta


private:
  int readMilliVoltsAvg(int pin, int samples);
  float currentFromMv(int mv, float gain);
  float ntcTempFromMv(int mv_node);

  int applyZeroAndFloor(int raw_mv, int zero_mv) const {
    int mv = raw_mv - zero_mv - adc_floor_mv;
    if (mv < 0) mv = 0;
    return mv;
  }

  // ---- Timer-driven state ----
 public:
  void onTick();   // <--- move here

private:
  volatile uint32_t pending_ticks_ = 0;


  int samples_per_ch_ = 64;
  uint8_t ch_ = 0;
  int samp_ = 0;

  uint32_t sum_[5] = {0};
  int latest_mv_[5] = {0};

  volatile bool latest_ready_ = false;
};
