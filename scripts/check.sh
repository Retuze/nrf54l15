#!/usr/bin/env bash
# 回归检查：构建 + 关键装配断言（向量表/符号契约/内存范围）。
# 用法：./scripts/check.sh              # 遍历全部 project/<NN_*>
#       ./scripts/check.sh 01_conn      # 只查单个
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ $# -eq 0 ]; then
  FAILED=0
  for dir in "$ROOT"/project/*; do
    n=$(basename "$dir")
    case "$n" in
      [0-9][0-9]_*) ;;
      *) continue ;;
    esac
    if "$0" "$n" >/tmp/check_"$n".log 2>&1; then
      echo "[PASS] $n"
    else
      echo "[FAIL] $n —— /tmp/check_$n.log"
      FAILED=1
    fi
  done
  exit $FAILED
fi

NAME="$1"
ELF="$ROOT/build/$NAME/$NAME"
HEX="$ROOT/build/$NAME/$NAME.hex"

echo "==> 构建 $NAME (--fresh)"
"$ROOT/scripts/build.sh" "$NAME" >/dev/null

[ -f "$ELF" ] || { echo "FAIL: 找不到 $ELF" >&2; exit 1; }
[ -f "$HEX" ] || { echo "FAIL: 找不到 $HEX" >&2; exit 1; }

python3 - "$ELF" "$HEX" <<'EOF'
import subprocess, sys

elf, hexf = sys.argv[1], sys.argv[2]
nm_out = subprocess.check_output(["llvm-nm", elf], text=True)
syms = {}
for line in nm_out.splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[1] in ("T", "W", "D", "R", "B", "A"):
        try: syms[parts[2]] = int(parts[0], 16)
        except ValueError: pass

# nRF54L15 符号契约（link.ld + startup.c + picolibc）：
# 数据段拷贝/TLS/堆符号 + picolibc 初始化入口（_set_tls/__libc_init_array）
required = ["_estack", "_sidata", "_sdata", "_edata", "_sbss", "_ebss",
            "__tdata_start", "__tdata_source", "__tdata_size", "__tls_base",
            "__tls_size", "__bss_start", "__heap_start", "__heap_end",
            "Reset_Handler", "HardFault_Handler", "main",
            "_set_tls", "__libc_init_array", "stdout"]
missing = [s for s in required if s not in syms]
if missing:
    print("FAIL: 缺少关键符号:", missing); sys.exit(1)

with open(hexf) as f:
    first = f.readline().strip()
assert first.startswith(":"), "HEX 首行格式异常"
words = []
for off in range(4):                      # 前 4 个字（SP + Reset + NMI + HardFault）
    start = 9 + off * 8
    try:
        words.append(int.from_bytes(bytes.fromhex(first[start:start+8]), 'little'))
    except ValueError:
        print(f"FAIL: HEX 第 {off} 项解析失败"); sys.exit(1)

fail = 0
def check(cond, msg):
    global fail
    print(("  ok  " if cond else "FAIL ") + msg)
    if not cond: fail += 1

check(words[0] == syms["_estack"],
      f"向量表[0]=SP: {words[0]:#010x} == _estack {syms['_estack']:#010x}")
check(words[1] == syms["Reset_Handler"] + 1,
      f"向量表[1]=Reset: {words[1]:#010x} == Reset_Handler|1 {syms['Reset_Handler']+1:#010x}")
check(words[1] & 1 == 1, "Reset 入口 Thumb 位置位")
check(0x20000000 <= syms["_estack"] <= 0x20000000 + 0x40000,
      f"_estack 在 RAM 范围: {syms['_estack']:#010x}")
check(0 <= syms["_sidata"] < 0x17D000,
      f"_sidata 在 RRAM 范围: {syms['_sidata']:#010x}")
check(0x20000000 <= syms["_sdata"] <= 0x20040000,
      f"_sdata 在 RAM 范围: {syms['_sdata']:#010x}")
check(0x20000000 <= syms["_sbss"] <= 0x20040000,
      f"_sbss 在 RAM 范围: {syms['_sbss']:#010x}")

print("PASS" if fail == 0 else "存在断言失败")
sys.exit(1 if fail else 0)
EOF

echo "==> check.sh 通过：$NAME"
