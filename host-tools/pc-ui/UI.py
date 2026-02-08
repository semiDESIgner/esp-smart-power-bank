import customtkinter as ctk
import serial
import threading
import time
import json
from serial.tools import list_ports

# ----------------------------
# CONFIG
# ----------------------------
BAUD = 115200
READ_TIMEOUT = 0.05
RECONNECT_SEC = 1.0

UI_FPS = 8
UI_PERIOD_MS = int(1000 / UI_FPS)

def auto_find_bt_com(fallback="COM3"):
    try:
        ports = list(list_ports.comports())
        for p in ports:
            desc = (p.description or "").lower()
            if "standard serial over bluetooth" in desc or "bluetooth" in desc:
                return p.device
    except:
        pass
    return fallback

PORT = auto_find_bt_com("COM3")

# ----------------------------
# UI KEYS
# ----------------------------
TELE_KEYS = [
    "vbat", "soc", "iload", "ichg", "idsg",
    "temp", "stat", "inet", "fcc", "rem",
    "ms", "chg", "ui_pending", "ui_left_s"
]

PIN_KEYS = [
    "en_charge", "en_dcdc", "en_relay", "en_load_dsg", "en_bypass",
    "chg_done", "charging", "btn_sleep"
]

LABELS = {
    "vbat": "VBAT",
    "soc": "SOC",
    "iload": "Load",
    "ichg": "Chg",
    "idsg": "Dsg",
    "temp": "Temp",
    "stat": "Stat",
    "inet": "Inet",
    "fcc": "FCC",
    "rem": "REM",
    "ms": "Millis",
    "chg": "Charging",
    "ui_pending": "UI Pending",
    "ui_left_s": "UI Left (s)",

    "en_charge":   "EN_CHARGE",
    "en_dcdc":     "EN_DCDC",
    "en_relay":    "EN_RELAY",
    "en_load_dsg": "EN_LOAD_DSG",
    "en_bypass":   "EN_BYPASS",
    "chg_done":    "CHG_DONE",
    "charging":    "CHARGING",
    "btn_sleep":   "BTN_SLEEP",
}

# alias mapping (helps if firmware keys differ)
KEY_ALIAS = {
    "vbat": ["vbat", "vbat_v", "vbat_meas_sys_v"],
    "soc":  ["soc", "soc_pct"],
    "iload":["iload", "iload_a", "load"],
    "ichg": ["ichg", "ichg_a", "ibatt_chg_a", "chg_a"],
    "idsg": ["idsg", "idsg_a", "ibatt_dsg_a", "dsg_a"],
    "temp": ["temp", "temp_c"],
    "stat": ["stat"],
    "inet": ["inet", "inet_a"],
    "fcc":  ["fcc", "fcc_mah"],
    "rem":  ["rem", "rem_mah"],
    "ms":   ["ms"],
    "chg":  ["chg", "charging"],
    "ui_pending": ["ui_pending"],
    "ui_left_s": ["ui_left_s"]
}

PIN_ALIAS = {
    "en_charge":   ["en_charge"],
    "en_dcdc":     ["en_dcdc"],
    "en_relay":    ["en_relay"],
    "en_load_dsg": ["en_load_dsg"],
    "en_bypass":   ["en_bypass"],
    "chg_done":    ["chg_done"],
    "charging":    ["charging"],
    "btn_sleep":   ["btn_sleep"]
}

NA_LIMIT = {"iload": 0.120, "ichg": 0.218, "idsg": 0.218}

def get_any(pkt: dict, logical_key: str):
    for k in KEY_ALIAS.get(logical_key, [logical_key]):
        if k in pkt:
            return pkt.get(k)
    return None

def get_any_pin(pins: dict, logical_key: str):
    for k in PIN_ALIAS.get(logical_key, [logical_key]):
        if k in pins:
            return pins.get(k)
    return None

def fmt_value(key: str, raw) -> str:
    if raw is None:
        return "--"
    try:
        if key == "vbat":
            return f"{float(raw):.3f} V"
        if key == "soc":
            return f"{float(raw):.1f} %"
        if key in ("iload", "ichg", "idsg", "inet"):
            a = float(raw)
            if key in NA_LIMIT and abs(a) < NA_LIMIT[key]:
                return "NA"
            if key == "inet":
                return f"{a:+.3f} A"
            return f"{a:.3f} A"
        if key == "temp":
            # JSON should send null instead of nan; null -> raw None -> handled above
            return f"{float(raw):.1f} C"
        if key in ("fcc", "rem"):
            return f"{float(raw):.0f} mAh"
        if key == "ms":
            return str(int(float(raw)))
        if key in ("chg", "ui_pending"):
            return "1" if int(raw) else "0"
        if key == "ui_left_s":
            return str(int(float(raw)))
        if key == "stat":
            return str(raw)
    except:
        pass
    return str(raw)

def fmt_pin(raw) -> str:
    if raw is None:
        return "--"
    try:
        return "1" if int(raw) else "0"
    except:
        return str(raw)

# ----------------------------
# Stream JSON extractor (brace matching)
# ----------------------------
def extract_json_objects(buffer: str):
    objs = []
    i = 0
    n = len(buffer)

    while True:
        start = buffer.find("{", i)
        if start == -1:
            return objs, buffer[-4000:]

        depth = 0
        in_str = False
        esc = False

        for j in range(start, n):
            ch = buffer[j]

            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
                continue
            else:
                if ch == '"':
                    in_str = True
                    continue
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        objs.append(buffer[start:j+1])
                        i = j + 1
                        break
        else:
            return objs, buffer[start:]

# ----------------------------
# UI
# ----------------------------
ctk.set_appearance_mode("light")
ctk.set_default_color_theme("green")

app = ctk.CTk()
app.title("ESP32 Bluetooth Telemetry + Control (JSON)")
app.geometry("1080x740")

top = ctk.CTkFrame(app)
top.pack(fill="x", padx=12, pady=(12, 6))

status_dot = ctk.CTkLabel(top, text="⚪", font=("Arial", 26))
status_dot.pack(side="left", padx=(10, 6), pady=10)

status_text = ctk.CTkLabel(top, text=f"Disconnected: {PORT}", font=("Arial", 16), text_color="red")
status_text.pack(side="left", padx=6)

main = ctk.CTkFrame(app)
main.pack(fill="both", expand=True, padx=12, pady=(6, 12))

left = ctk.CTkFrame(main)
left.pack(side="left", fill="both", expand=False, padx=(12, 6), pady=12)

right = ctk.CTkFrame(main)
right.pack(side="left", fill="both", expand=True, padx=(6, 12), pady=12)

# Telemetry panel
ctk.CTkLabel(left, text="Telemetry", font=("Arial", 18, "bold")).pack(pady=(12, 8))
tele_frame = ctk.CTkFrame(left)
tele_frame.pack(fill="both", expand=True, padx=12, pady=(0, 12))

value_vars = {k: ctk.StringVar(value="--") for k in TELE_KEYS}
for r, k in enumerate(TELE_KEYS):
    ctk.CTkLabel(tele_frame, text=LABELS.get(k, k), font=("Arial", 14)).grid(
        row=r, column=0, sticky="w", padx=10, pady=7
    )
    ctk.CTkLabel(tele_frame, textvariable=value_vars[k], font=("Consolas", 16, "bold")).grid(
        row=r, column=1, sticky="e", padx=10, pady=7
    )

# Pins panel
ctk.CTkLabel(right, text="Pins", font=("Arial", 18, "bold")).pack(pady=(12, 8))
pins_frame = ctk.CTkFrame(right)
pins_frame.pack(fill="x", padx=12, pady=(0, 10))

pin_vars = {k: ctk.StringVar(value="--") for k in PIN_KEYS}
pin_dots = {}

def set_dot(dot_label, on: bool):
    dot_label.configure(text="🟢" if on else "⚪")

for k in PIN_KEYS:
    row = ctk.CTkFrame(pins_frame)
    row.pack(fill="x", padx=10, pady=6)
    ctk.CTkLabel(row, text=LABELS.get(k, k), font=("Arial", 14)).pack(side="left")
    dot = ctk.CTkLabel(row, text="⚪", font=("Arial", 20))
    dot.pack(side="right", padx=(8, 0))
    pin_dots[k] = dot
    ctk.CTkLabel(row, textvariable=pin_vars[k], font=("Consolas", 14, "bold")).pack(side="right")

# Controls panel (NEW)
control_frame = ctk.CTkFrame(right)
control_frame.pack(fill="x", padx=12, pady=(0, 10))

ctk.CTkLabel(control_frame, text="Controls", font=("Arial", 18, "bold")).pack(pady=(10, 8))

en_charge_var = ctk.IntVar(value=0)
en_load_dsg_var = ctk.IntVar(value=0)

# Debug / raw
ctk.CTkLabel(right, text="Raw incoming (last chunk)", font=("Arial", 14)).pack(pady=(6, 0))
last_raw = ctk.CTkLabel(right, text="--", font=("Consolas", 12), wraplength=520, justify="left")
last_raw.pack(pady=(0, 8))

ctk.CTkLabel(right, text="Parse status", font=("Arial", 14)).pack(pady=(6, 0))
parse_status = ctk.CTkLabel(right, text="--", font=("Consolas", 12), wraplength=520, justify="left")
parse_status.pack(pady=(0, 10))

# ----------------------------
# Shared state + serial
# ----------------------------
stop_event = threading.Event()
ser = None

latest_pkt = None
latest_raw = ""
latest_parse_msg = "Waiting..."
connected_flag = False
state_lock = threading.Lock()

def ui_connected(msg):
    status_dot.configure(text="🟢")
    status_text.configure(text=msg, text_color="green")

def ui_disconnected(msg):
    status_dot.configure(text="⚪")
    status_text.configure(text=msg, text_color="red")

def send_cmd(pin: str, val: int):
    """Send command JSON to ESP32 (UI -> ESP32)."""
    global ser
    if ser is None or (not ser.is_open):
        return
    cmd = {"cmd": "set", "pin": pin, "val": int(val)}
    try:
        ser.write((json.dumps(cmd) + "\n").encode())
    except Exception as e:
        # keep UI stable even if send fails
        with state_lock:
            global latest_parse_msg
            latest_parse_msg = f"Send error ❌ {e}"

# Switch callbacks
def on_toggle_en_charge():
    send_cmd("en_charge", en_charge_var.get())

def on_toggle_en_load_dsg():
    send_cmd("en_load_dsg", en_load_dsg_var.get())

# Create switches
ctk.CTkSwitch(
    control_frame,
    text="EN_CHARGE",
    variable=en_charge_var,
    command=on_toggle_en_charge
).pack(anchor="w", padx=14, pady=6)

ctk.CTkSwitch(
    control_frame,
    text="EN_LOAD_DSG",
    variable=en_load_dsg_var,
    command=on_toggle_en_load_dsg
).pack(anchor="w", padx=14, pady=6)

# Prevent switch callbacks firing when we programmatically sync
suppress_switch_events = False

def ui_tick():
    """Update UI at fixed FPS (smooth)."""
    global latest_pkt, latest_raw, latest_parse_msg, suppress_switch_events

    with state_lock:
        pkt = latest_pkt
        raw = latest_raw
        msg = latest_parse_msg

    if raw:
        last_raw.configure(text=raw[-300:] if len(raw) > 300 else raw)
    parse_status.configure(text=msg)

    if isinstance(pkt, dict):
        # Telemetry
        for k in TELE_KEYS:
            value_vars[k].set(fmt_value(k, get_any(pkt, k)))

        # Pins
        pins = pkt.get("pins", {})
        if not isinstance(pins, dict):
            pins = {}

        for k in PIN_KEYS:
            v = get_any_pin(pins, k)
            pin_vars[k].set(fmt_pin(v))
            try:
                set_dot(pin_dots[k], int(v) == 1)
            except:
                set_dot(pin_dots[k], False)

        # Sync control switches with telemetry
        # (so UI stays truthful even if firmware overrides)
        try:
            suppress_switch_events = True
            en_charge_var.set(int(get_any_pin(pins, "en_charge") or 0))
            en_load_dsg_var.set(int(get_any_pin(pins, "en_load_dsg") or 0))
        finally:
            suppress_switch_events = False

    app.after(UI_PERIOD_MS, ui_tick)

app.after(UI_PERIOD_MS, ui_tick)

def worker():
    global ser, latest_pkt, latest_raw, latest_parse_msg
    buf = ""

    while not stop_event.is_set():
        if ser is None or not getattr(ser, "is_open", False):
            try:
                ser = serial.Serial(PORT, BAUD, timeout=READ_TIMEOUT)
                app.after(0, lambda: ui_connected(f"Connected: {PORT}"))
                with state_lock:
                    latest_parse_msg = "Connected ✅ Waiting for JSON..."
                buf = ""
            except Exception as e:
                app.after(0, lambda e=e: ui_disconnected(f"Disconnected: {PORT} ({e})"))
                time.sleep(RECONNECT_SEC)
                continue

        try:
            data = ser.read(512)
            if not data:
                continue

            chunk = data.decode(errors="ignore")
            buf += chunk

            with state_lock:
                latest_raw = chunk

            objs, buf = extract_json_objects(buf)

            if not objs:
                with state_lock:
                    latest_parse_msg = "Waiting for complete JSON {...}"
                continue

            for s in objs:
                try:
                    pkt = json.loads(s)
                    if isinstance(pkt, dict):
                        with state_lock:
                            latest_pkt = pkt
                            latest_parse_msg = f"Parsed OK ✅ keys={len(pkt.keys())}"
                except Exception as e:
                    with state_lock:
                        latest_parse_msg = f"JSON error ❌ {e}"

        except Exception as e:
            try:
                if ser:
                    ser.close()
            except:
                pass
            ser = None
            app.after(0, lambda e=e: ui_disconnected(f"Disconnected: {PORT} ({e})"))
            time.sleep(RECONNECT_SEC)

threading.Thread(target=worker, daemon=True).start()

def on_close():
    stop_event.set()
    try:
        time.sleep(0.2)
        if ser and ser.is_open:
            ser.close()
    except:
        pass
    app.destroy()

app.protocol("WM_DELETE_WINDOW", on_close)
app.mainloop()
