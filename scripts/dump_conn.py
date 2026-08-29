"""Dump the connectable-advertiser state and decode a captured CONNECT_IND.

Reads g_stats and g_last from the target (03a_conn_capture firmware).
If the last captured packet is a CONNECT_IND, decodes its LLData:
  AA, CRCInit, WinSize, WinOffset, Interval, Latency, Timeout, ChM, Hop, SCA.

Usage:  python dump_conn.py [path/to/app.elf]
"""

import re
import subprocess
import sys
from pathlib import Path

ELF = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "build" / "app.elf"
STATS_MAGIC = 0x03A0C0DE
HERE = Path(__file__).parent

PDU_TYPES = {0x0: "ADV_IND", 0x3: "SCAN_REQ", 0x5: "CONNECT_IND"}
SCA_US = ["251-500", "151-250", "101-150", "76-100", "51-75", "31-50", "21-30", "0-20"]


def sym(elf: Path, name: str) -> int:
    out = subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+\S\s+(\S+)$", line.strip())
        if m and m.group(2) == name:
            return int(m.group(1), 16)
    sys.exit(f"symbol {name} not found")


def read_mem(addr: int, length: int) -> bytes:
    tmp = HERE / "_conndump.bin"
    subprocess.run(
        ["pyocd", "cmd", "-t", "nrf54l", "-O", "connect_mode=attach",
         "-c", f"savemem 0x{addr:08x} {length} _conndump.bin"],
        capture_output=True, text=True, check=True, cwd=HERE)
    data = tmp.read_bytes()
    tmp.unlink()
    return data


def u(b: bytes) -> int:
    return int.from_bytes(b, "little")


def decode_connect_ind(pdu: bytes) -> None:
    # header(2) + InitA(6) + AdvA(6) + LLData(22)
    inita = ":".join(f"{x:02X}" for x in reversed(pdu[2:8]))
    adva = ":".join(f"{x:02X}" for x in reversed(pdu[8:14]))
    d = pdu[14:36]
    aa = u(d[0:4]); crcinit = u(d[4:7])
    winsize = d[7]; winoffset = u(d[8:10])
    interval = u(d[10:12]); latency = u(d[12:14]); timeout = u(d[14:16])
    chm = d[16:21]; hopsca = d[21]
    hop = hopsca & 0x1F; sca = hopsca >> 5
    nch = sum(bin(x).count("1") for x in chm)
    print("  CONNECT_IND:")
    print(f"    InitA (central) = {inita}")
    print(f"    AdvA  (us)      = {adva}")
    print(f"    AccessAddr      = 0x{aa:08X}")
    print(f"    CRCInit         = 0x{crcinit:06X}")
    print(f"    WinSize/Offset  = {winsize*1.25:.2f} ms / {winoffset*1.25:.2f} ms")
    print(f"    Interval        = {interval*1.25:.2f} ms")
    print(f"    Latency         = {latency}")
    print(f"    Timeout         = {timeout*10} ms")
    print(f"    ChM             = {chm.hex()}  ({nch} data channels)")
    print(f"    Hop             = {hop}")
    print(f"    SCA             = {sca} ({SCA_US[sca]} ppm)")


def main() -> None:
    s = read_mem(sym(ELF, "g_stats"), 20)
    magic, adv, ok, err, conn = (u(s[i:i+4]) for i in range(0, 20, 4))
    if magic != STATS_MAGIC:
        sys.exit(f"bad magic 0x{magic:08x} - is 03a firmware running?")
    print(f"adv_events={adv}  rx_crcok={ok}  rx_crcerr={err}  CONNECT_IND={conn}")

    last = read_mem(sym(ELF, "g_last"), 64)
    chan, rssi, ln = last[0], last[1], last[2]
    pdu = last[3:3 + ln]
    if ln == 0:
        print("no RX packet captured yet — tap Connect on your phone, then re-run")
        return
    ptype = PDU_TYPES.get(pdu[0] & 0xF, f"0x{pdu[0] & 0xF:x}")
    print(f"\nlast RX: ch{chan}  -{rssi} dBm  {ptype}  len={ln}  raw={pdu.hex()}")
    if (pdu[0] & 0xF) == 0x5 and ln >= 36:
        decode_connect_ind(pdu)


if __name__ == "__main__":
    main()
