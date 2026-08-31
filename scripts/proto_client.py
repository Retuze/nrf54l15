"""Host-side app-protocol client for the 05_proto experiment (docs/proto.md).

Connects to 54L-GATT over BLE, subscribes to 0xFFF1 notifications, then runs
the first-sync exchange plus SET semantics:

  1. GET_REQ{}            -> expect REPORT (ACK_REQ) with 5 TLVs -> reply ACK
  2. SET{TIME=now}        -> expect ACK err=0
  3. SET dup (same id)    -> expect re-ACK (idempotent, not re-executed)
  4. GET_REQ{}            -> expect REPORT whose TIME reflects the SET epoch

Usage:  python proto_client.py
"""

import asyncio
import struct
import sys
import time

from bleak import BleakScanner, BleakClient

TARGET_NAME = "54L-GATT"
TARGET_ADDR = "F0:E0:D0:C0:B0:A0"
CHAR_FFF1 = "0000fff1-0000-1000-8000-00805f9b34fb"

TYPE_GET_REQ = 0x0001
TYPE_REPORT = 0x0002
TYPE_SET = 0x0003
TYPE_ACK = 0x0004

FLAG_ACK_REQ = 0x20
FLAG_MORE = 0x10

T_NAMES = {1: "BATTERY", 2: "TIME", 3: "DEV_INFO", 4: "UPTIME", 5: "STATUS"}

failures = 0


def check(cond: bool, what: str) -> None:
    global failures
    print(("  ok  " if cond else "  FAIL") + f" {what}")
    if not cond:
        failures += 1


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


def frame(msg_type: int, msg_id: int, payload: bytes = b"", ack_req: bool = False) -> bytes:
    seqflags = FLAG_ACK_REQ if ack_req else 0
    hdr = struct.pack(">HBBH", msg_type, seqflags, msg_id, len(payload))
    body = hdr + payload
    return body + struct.pack(">H", crc16(body))


def parse(data: bytes):
    """-> (type, seqflags, msg_id, payload) or None on bad frame."""
    if len(data) < 8:
        return None
    msg_type, seqflags, msg_id, plen = struct.unpack(">HBBH", data[:6])
    if len(data) != 6 + plen + 2:
        return None
    if struct.unpack(">H", data[6 + plen:])[0] != crc16(data[:6 + plen]):
        return None
    return msg_type, seqflags, msg_id, data[6:6 + plen]


def parse_tlvs(payload: bytes):
    tlvs = {}
    i = 0
    while i + 2 <= len(payload):
        t, l = payload[i], payload[i + 1]
        if i + 2 + l > len(payload):
            return None
        tlvs[t] = payload[i + 2:i + 2 + l]
        i += 2 + l
    return tlvs if i == len(payload) else None


async def main() -> None:
    print(f"scanning for {TARGET_NAME} ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, a: (a.local_name == TARGET_NAME) or (d.address.upper() == TARGET_ADDR),
        timeout=10.0,
    )
    if dev is None:
        print("device not found")
        sys.exit(1)

    rx_q: asyncio.Queue[bytes] = asyncio.Queue()

    def on_notify(_h, data: bytearray) -> None:
        rx_q.put_nowait(bytes(data))

    async def expect(msg_type: int, timeout: float = 3.0):
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            try:
                raw = await asyncio.wait_for(rx_q.get(), remain)
            except asyncio.TimeoutError:
                return None
            f = parse(raw)
            if f is None:
                print(f"  (bad frame: {raw.hex()})")
                continue
            if f[0] == msg_type:
                return f
            print(f"  (skip type=0x{f[0]:04x})")

    print(f"connecting to {dev.address} ...")
    async with BleakClient(dev, timeout=15.0) as client:
        print(f"connected, mtu={client.mtu_size}")
        await client.start_notify(CHAR_FFF1, on_notify)
        app_id = 0

        # -- 1: GET -> REPORT(ACK_REQ) -> ACK --------------------------------
        print("[1] GET_REQ{} (full report)")
        app_id += 1
        await client.write_gatt_char(CHAR_FFF1, frame(TYPE_GET_REQ, app_id), response=True)
        rep = await expect(TYPE_REPORT)
        check(rep is not None, "REPORT received")
        if rep:
            _, flags, rep_id, payload = rep
            check(bool(flags & FLAG_ACK_REQ), "REPORT has ACK_REQ")
            tlvs = parse_tlvs(payload)
            check(tlvs is not None, "TLV payload parses")
            if tlvs:
                for t in (1, 2, 3, 4, 5):
                    check(t in tlvs, f"field {T_NAMES[t]} present")
                if 3 in tlvs:
                    fw = tlvs[3]
                    check(fw[4:] == b"54L", f"DEV_INFO fw={fw[0]}.{fw[1]}.{fw[2]} name={fw[4:]!r}")
                if 1 in tlvs:
                    lvl, mv = tlvs[1][0], struct.unpack(">H", tlvs[1][1:3])[0]
                    check(lvl == 85 and mv == 3600, f"BATTERY level={lvl}% {mv}mV")
            await client.write_gatt_char(
                CHAR_FFF1, frame(TYPE_ACK, app_id, struct.pack(">BB", 0, rep_id)), response=True)
            print(f"  ACK sent for report id={rep_id}")

        # -- 2: SET{TIME} -> ACK ---------------------------------------------
        print("[2] SET{TIME=now}")
        app_id += 1
        set_id = app_id
        epoch = int(time.time())
        set_payload = bytes([2, 6]) + struct.pack(">Ih", epoch, 480)
        await client.write_gatt_char(CHAR_FFF1, frame(TYPE_SET, set_id, set_payload), response=True)
        ack = await expect(TYPE_ACK)
        check(ack is not None, "ACK received")
        if ack:
            err, acked = ack[3][0], ack[3][1]
            check(err == 0, f"SET accepted (err={err})")
            check(acked == set_id, f"ACK id matches ({acked})")

        # -- 3: SET dup (idempotent re-ack) ----------------------------------
        print("[3] SET dup (same msg_id resent)")
        await client.write_gatt_char(CHAR_FFF1, frame(TYPE_SET, set_id, set_payload), response=True)
        ack = await expect(TYPE_ACK)
        check(ack is not None and ack[3][1] == set_id, "dup re-ACKed")

        # -- 4: GET again, TIME should reflect the SET -----------------------
        print("[4] GET_REQ{} again (TIME check)")
        app_id += 1
        await client.write_gatt_char(CHAR_FFF1, frame(TYPE_GET_REQ, app_id), response=True)
        rep = await expect(TYPE_REPORT)
        check(rep is not None, "REPORT received")
        if rep:
            _, _, rep_id, payload = rep
            tlvs = parse_tlvs(payload) or {}
            if 2 in tlvs:
                dev_epoch, tz = struct.unpack(">Ih", tlvs[2])
                drift = dev_epoch - int(time.time())
                check(abs(drift) <= 3, f"TIME synced (drift={drift}s, tz={tz}min)")
            if 4 in tlvs:
                up = struct.unpack(">I", tlvs[4])[0]
                # GRTC 不随软复位清零，uptime 是自上电以来的累计
                check(up < 30 * 86400, f"UPTIME sane ({up}s)")
            await client.write_gatt_char(
                CHAR_FFF1, frame(TYPE_ACK, app_id, struct.pack(">BB", 0, rep_id)), response=True)

        await client.stop_notify(CHAR_FFF1)

    print(f"\nRESULT: {'OK' if failures == 0 else f'{failures} FAILURES'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    assert crc16(b"123456789") == 0x29B1, "CRC16 self-test failed"
    asyncio.run(main())
