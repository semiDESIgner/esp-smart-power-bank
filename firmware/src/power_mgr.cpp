#include "power_mgr.h"
#include "pins.h"

namespace PowerMgr {

// Keep these always ON (as your original design)
static const int ALWAYS_ON_PINS[] = {
  PIN_EN_CHARGE,
  PIN_EN_LOAD_DSG,
  PIN_EN_BYPASS
  // NOTE: PIN_EN_DCDC removed from here (we will control it dynamically)
};

void begin() {
  for (int pin : ALWAYS_ON_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  pinMode(PIN_EN_DCDC, OUTPUT);
  digitalWrite(PIN_EN_DCDC, HIGH);   // default ON (until we apply mode)

  pinMode(PIN_EN_RELAY, OUTPUT);
  digitalWrite(PIN_EN_RELAY, LOW);   // default OFF
}

void setRelay(bool on) {
  digitalWrite(PIN_EN_RELAY, on ? HIGH : LOW);
}

void setDcdc(bool on) {
  digitalWrite(PIN_EN_DCDC, on ? HIGH : LOW);
}

void applyChargingMode(bool charging) {
  if (charging) {
    // Charging: relay ON, DC-DC OFF
    setRelay(true);
    setDcdc(false);
  } else {
    // Not charging: relay OFF, DC-DC ON
    setDcdc(true);
    setRelay(false);
    
  }
}

} // namespace PowerMgr
