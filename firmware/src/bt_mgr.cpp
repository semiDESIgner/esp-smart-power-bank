#include "bt_mgr.h"
#include "BluetoothSerial.h"
#include <stdarg.h>

static BluetoothSerial SerialBT;
static bool btInitDone = false;

namespace BtMgr {

void begin(const char* deviceName) {
  if (btInitDone) return;
  SerialBT.enableSSP();
  SerialBT.setPin("1234"); // optional
  SerialBT.begin(deviceName);
  btInitDone = true;
}

bool connected() {
  return btInitDone && SerialBT.hasClient();
}

// -------- TX ----------
void print(const char* s) {
  if (connected()) SerialBT.print(s);
}

void println(const char* s) {
  if (connected()) SerialBT.println(s);
}

void printf(const char* fmt, ...) {
  if (!connected()) return;

  // IMPORTANT: big enough so JSON doesn't truncate
  char buf[1024];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  SerialBT.print(buf);
}

// -------- RX (NEW) ----------
int available() {
  if (!connected()) return 0;
  return SerialBT.available();
}

int read() {
  if (!connected()) return -1;
  return SerialBT.read();
}

// Reads one line ending with '\n'. Returns number of bytes copied (excluding '\0').
// If no full line yet, returns 0 (keeps partial data in internal buffer of BTSerial).
size_t readLine(char* out, size_t outLen) {
  if (!connected() || outLen < 2) return 0;

  size_t n = 0;
  while (SerialBT.available() > 0) {
    int c = SerialBT.read();
    if (c < 0) break;

    // ignore '\r'
    if (c == '\r') continue;

    if (c == '\n') {
      out[n] = '\0';
      return n; // got a full line
    }

    if (n < outLen - 1) {
      out[n++] = (char)c;
    } else {
      // buffer full -> terminate and return what we have
      out[n] = '\0';
      return n;
    }
  }

  // no newline received yet -> indicate "not ready"
  return 0;
}

} // namespace BtMgr
