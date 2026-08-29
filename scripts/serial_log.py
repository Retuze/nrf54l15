"""Continuously log the XIAO nRF54L15 UART to a file (and stdout).

Usage:  python serial_log.py [COMxx] [baud]
Writes to scripts/serial.log (line-buffered), reconnecting if the port drops.
"""

import sys
import time
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

LOGFILE = Path(__file__).parent / "serial.log"


def pick_port(preferred: str | None) -> str:
    if preferred:
        return preferred
    for p in list_ports.comports():
        # XIAO's SAMD11 shows up as a generic USB serial device.
        if "CP210" not in p.description:
            return p.device
    ports = list_ports.comports()
    return ports[0].device if ports else "COM23"


def main() -> None:
    port = pick_port(sys.argv[1] if len(sys.argv) > 1 else None)
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    print(f"logging {port} @ {baud} -> {LOGFILE}")

    with open(LOGFILE, "a", buffering=1, encoding="utf-8", errors="replace") as f:
        f.write(f"\n---- session {datetime.now():%Y-%m-%d %H:%M:%S} on {port} ----\n")
        while True:
            try:
                with serial.Serial(port, baud, timeout=1) as ser:
                    while True:
                        line = ser.readline()
                        if not line:
                            continue
                        text = line.decode("utf-8", "replace").rstrip("\r\n")
                        stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        out = f"{stamp}  {text}"
                        print(out)
                        f.write(out + "\n")
            except serial.SerialException as e:
                msg = f"[serial_log] {port} error: {e}; retrying..."
                print(msg)
                f.write(msg + "\n")
                time.sleep(1.0)


if __name__ == "__main__":
    main()
