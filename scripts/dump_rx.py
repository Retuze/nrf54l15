"""Dump and decode the RX sniffer's RAM state (02_rx_scan firmware).

Reads g_stats / g_ring from the target over pyOCD (no UART needed),
then decodes each stored advertising PDU: type, AdvA, AD structures.

Usage:  python dump_rx.py [path/to/app.elf]
"""

import re
import subprocess
import sys
from pathlib import Path

ELF = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "build" / "app.elf"
RING_ENTRIES, RING_ESIZE = 16, 48
STATS_MAGIC = 0x54AD5CA0

PDU_TYPES = {
    0x0: "ADV_IND", 0x1: "ADV_DIRECT_IND", 0x2: "ADV_NONCONN_IND",
    0x3: "SCAN_REQ", 0x4: "SCAN_RSP", 0x5: "CONNECT_IND",
    0x6: "ADV_SCAN_IND", 0x7: "ADV_EXT_IND",
}


def symbol_addrs(elf: Path) -> dict:
    out = subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True, check=True).stdout
    addrs = {}
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+\S\s+(g_stats|g_ring)$", line.strip())
        if m:
            addrs[m.group(2)] = int(m.group(1), 16)
    if len(addrs) != 2:
        sys.exit(f"could not find g_stats/g_ring in {elf}")
    return addrs


def read_mem(addr: int, length: int) -> bytes:
    here = Path(__file__).parent
    tmp = here / "_rxdump.bin"
    subprocess.run(
        ["pyocd", "cmd", "-t", "nrf54l", "-O", "connect_mode=attach",
         "-c", f"savemem 0x{addr:08x} {length} _rxdump.bin"],
        capture_output=True, text=True, check=True, cwd=here)
    data = tmp.read_bytes()
    tmp.unlink()
    return data


def decode_ad(data: bytes) -> str:
    out, i = [], 0
    while i < len(data):
        ln = data[i]
        if ln == 0 or i + 1 + ln > len(data):
            break
        t, v = data[i + 1], data[i + 2:i + 1 + ln]
        if t in (0x08, 0x09):
            out.append(f"name={v.decode(errors='replace')!r}")
        elif t == 0x01:
            out.append(f"flags=0x{v[0]:02x}")
        elif t == 0xFF:
            out.append(f"mfg={v[:2].hex()}:{v[2:].hex()}")
        elif t in (0x02, 0x03):
            out.append(f"uuid16={v.hex()}")
        elif t == 0x16:
            out.append(f"svc16={v.hex()}")
        else:
            out.append(f"ad{t:02x}={v.hex()}")
        i += 1 + ln
    return "  ".join(out)


def main() -> None:
    a = symbol_addrs(ELF)
    stats = read_mem(a["g_stats"], 16)
    magic, ok, err, widx = (int.from_bytes(stats[i:i+4], "little") for i in range(0, 16, 4))
    if magic != STATS_MAGIC:
        sys.exit(f"bad magic 0x{magic:08x} - is the rx firmware running?")

    total = ok + err
    print(f"crc_ok={ok}  crc_err={err}  "
          f"({100*err/total:.1f}% collisions/noise)" if total else "no packets yet")

    ring = read_mem(a["g_ring"], RING_ENTRIES * RING_ESIZE)
    n = min(widx, RING_ENTRIES)
    print(f"\nlast {n} packets (newest last):")
    order = [(widx - n + k) % RING_ENTRIES for k in range(n)]
    for slot in order:
        e = ring[slot * RING_ESIZE:(slot + 1) * RING_ESIZE]
        ln, rssi = e[0], e[1]
        pdu = e[2:2 + ln]
        if ln < 8:
            continue
        hdr, plen = pdu[0], pdu[1]
        ptype = PDU_TYPES.get(hdr & 0xF, f"0x{hdr & 0xF:x}")
        adva = ":".join(f"{b:02X}" for b in reversed(pdu[2:8]))
        ads = decode_ad(pdu[8:2 + plen]) if plen > 6 else ""
        print(f"  -{rssi:3d} dBm  {ptype:<16} {adva}  {ads}")


if __name__ == "__main__":
    main()
