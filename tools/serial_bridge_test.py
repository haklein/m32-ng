#!/usr/bin/env python3
"""Quick smoke test for the M32-NG serial JSON bridge.

Usage:
    python3 tools/serial_bridge_test.py [/dev/ttyACM0]

Requires: pyserial  (pip install pyserial)
"""

import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = 115200

def read_json(ser):
    """Read lines until we get one starting with { or [."""
    for _ in range(20):
        line = ser.readline().decode(errors="replace").strip()
        if line and (line[0] in "{["):
            return line
    return "(no JSON response)"

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    commands = [
        "GET /api/version",
        "GET /api/status",
        "GET /api/battery",
        "GET /api/config",
        "GET /api/meta",
        "GET /api/slots",
        "GET /api/text",
        'POST /api/send {"text":"hi"}',
        "GET /api/mode?m=keyer",
        "GET /api/pause",
    ]

    passed = 0
    failed = 0

    for cmd in commands:
        ser.write((cmd + "\n").encode())
        time.sleep(0.3)
        resp = read_json(ser)
        ok = resp.startswith("{") or resp.startswith("[")
        status = "OK" if ok else "FAIL"
        if ok:
            passed += 1
        else:
            failed += 1
        truncated = (resp[:100] + "...") if len(resp) > 100 else resp
        print(f"[{status}] {cmd}")
        print(f"       {truncated}\n")

    ser.close()
    print(f"--- {passed} passed, {failed} failed ---")
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
