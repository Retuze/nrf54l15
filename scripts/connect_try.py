"""Trigger a CONNECT_IND from the PC by attempting a BLE connection.

Finds 54L-GATT and tries to connect. Our firmware does not yet complete the
connection, so this will very likely fail/time out -- that's fine: the point
is that the PC's Bluetooth transmits a CONNECT_IND during the attempt, which
the device captures. Run scripts/dump_conn.py afterwards to decode it.

Usage:  python connect_try.py [attempts]
"""

import asyncio
import sys

from bleak import BleakScanner, BleakClient

TARGET_NAME = "54L-GATT"
TARGET_ADDR = "F2:E0:D0:C0:B0:A0"


async def one_attempt(i: int) -> bool:
    print(f"[attempt {i}] scanning for {TARGET_NAME} ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, a: (a.local_name == TARGET_NAME) or (d.address.upper() == TARGET_ADDR),
        timeout=6.0,
    )
    if dev is None:
        print("  not found in scan")
        return False

    print(f"  found {dev.address} — sending connection request ...")
    try:
        async with BleakClient(dev, timeout=8.0) as client:
            print(f"  CONNECTED! (services: {len(client.services.services)})")
            return True
    except Exception as e:
        print(f"  connect ended: {type(e).__name__}: {e}")
        print("  (expected — CONNECT_IND was still transmitted)")
        return False


async def main(n: int) -> None:
    for i in range(1, n + 1):
        ok = await one_attempt(i)
        if ok:
            break
        await asyncio.sleep(1.0)


if __name__ == "__main__":
    attempts = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    asyncio.run(main(attempts))
