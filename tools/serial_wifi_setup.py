#!/usr/bin/env python3
"""Set WiFi credentials on M32-NG via the serial JSON bridge.

Usage:
    python3 tools/serial_wifi_setup.py <ssid> <password> [/dev/ttyACM0]

The call blocks while the device connects (~10s max).  Returns the actual
connection result: {"ok":true} on success, {"ok":false} on failure.
On success, credentials are saved and the web server is started.

Requires: pyserial  (pip install pyserial)
"""

import json
import serial
import sys
import time

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} <ssid> <password> [port]")
    sys.exit(1)

SSID = sys.argv[1]
PASS = sys.argv[2]
PORT = sys.argv[3] if len(sys.argv) > 3 else "/dev/ttyACM0"
BAUD = 115200

def read_line(ser):
    """Read one non-empty line."""
    while True:
        line = ser.readline().decode(errors="replace").strip()
        if line:
            return line

def main():
    ser = serial.Serial(PORT, BAUD, timeout=15)
    time.sleep(1.5)  # wait for boot
    ser.reset_input_buffer()

    # Send WiFi credentials (blocks ~10s on device while connecting)
    body = json.dumps({"ssid": SSID, "pass": PASS})
    cmd = f"POST /api/wifi {body}\n"
    print(f"> {cmd.strip()}")
    ser.write(cmd.encode())
    print(f"Connecting to '{SSID}'... (up to 10s)")

    # Read all output until we get the JSON response
    # (log lines may arrive before the result)
    while True:
        line = read_line(ser)
        if line.startswith("{"):
            result = json.loads(line)
            break
        print(f"  [{line}]")  # log output

    if result.get("ok"):
        print(f"Connected!  Credentials saved.")
        # Query version to confirm bridge is alive
        ser.reset_input_buffer()
        ser.write(b"GET /api/version\n")
        time.sleep(0.3)
        line = read_line(ser)
        while not line.startswith("{"):
            line = read_line(ser)
        print(f"Device: {line}")
    else:
        print(f"Connection FAILED.  Check SSID/password and that the")
        print(f"network is 2.4 GHz (ESP32 does not support 5 GHz).")
        print(f"Use 'GET /api/scan' to list visible networks.")

    ser.close()
    sys.exit(0 if result.get("ok") else 1)

if __name__ == "__main__":
    main()
