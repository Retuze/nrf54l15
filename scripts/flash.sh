#!/usr/bin/env bash
# 烧录：板载 SAMD11 CMSIS-DAP（pyocd）。
#
# 前提：
#   1) pip install pyocd
#   2) USB 接板（XIAO nRF54L15 板载调试器即 CMSIS-DAP，无需外接 DAPLink）
#
# 用法：./scripts/flash.sh [实验名]
set -euo pipefail

NAME="${1:-01_conn}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HEX="$ROOT/build/$NAME/$NAME.hex"

[ -f "$HEX" ] || { echo "找不到 $HEX，先 ./scripts/build.sh $NAME" >&2; exit 1; }

PYOCD=""
for c in pyocd "$HOME/.local/bin/pyocd" "$HOME/venv/pyocd/bin/pyocd"; do
  if command -v "$c" >/dev/null 2>&1; then PYOCD="$c"; break; fi
done
if [ -z "$PYOCD" ]; then
  echo "没找到 pyocd：pip install pyocd" >&2
  exit 1
fi

echo "==> $PYOCD flash -t nrf54l $HEX"
"$PYOCD" flash -t nrf54l "$HEX"
echo "==> 烧录完成（pyocd）"
