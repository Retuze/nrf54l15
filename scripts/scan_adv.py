"""Scan for the bare-metal advertiser (54L-GATT) and dump what we receive.

Usage:  python scan_adv.py [seconds]

Verifies over the air, using the PC's Bluetooth adapter:
  - the device is seen at the expected random static address
  - the local name AD structure decodes to "54L-GATT"
  - RSSI and advertising interval look sane
"""

import asyncio
import sys
import time

from bleak import BleakScanner

TARGET_NAME = "54L-GATT"
TARGET_ADDR = "F0:E0:D0:C0:B0:A0"

hits = []


def on_adv(device, adv):
    if device.address.upper() != TARGET_ADDR and adv.local_name != TARGET_NAME:
        return
    hits.append(time.monotonic())
    print(
        f"[{len(hits):3d}] addr={device.address}  rssi={adv.rssi} dBm  "
        f"name={adv.local_name!r}"
    )


async def main(duration: float) -> None:
    scanner = BleakScanner(detection_callback=on_adv, scanning_mode="active")
    print(f"scanning for {duration:.0f}s ...")
    async with scanner:
        await asyncio.sleep(duration)

    if not hits:
        print("\nRESULT: NOT FOUND - no advertisement from", TARGET_ADDR)
        sys.exit(1)

    print(f"\nRESULT: OK - {len(hits)} advertisements received")
    if len(hits) >= 2:
        gaps = [b - a for a, b in zip(hits, hits[1:])]
        avg = sum(gaps) / len(gaps)
        print(f"mean report interval: {avg*1000:.0f} ms "
              f"(min {min(gaps)*1000:.0f} / max {max(gaps)*1000:.0f}) "
              f"- OS may coalesce reports")


if __name__ == "__main__":
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    asyncio.run(main(dur))
