#!/usr/bin/env bash
# common/ 模块宿主测试的代码覆盖率（clang --coverage + llvm-cov gcov，零外部依赖）。
#
# 用法：./scripts/coverage.sh [最小行覆盖率%]   （默认 80）
#   每模块独立目录编译+运行（.gcda 互不污染），llvm-cov gcov 生成 .gcov
#   后按"可执行行"聚合行覆盖（#####/===== 计未执行）。低于门槛退出非 0
#   （check.sh 据此判失败）。报告落在 build/coverage/<module>/。
#   用 llvm-cov 而非 gcc 的 gcov：与 ch32x035 仓库一致（gcc 的 gcov 不认
#   clang 的格式版本 B11*），且全工程编译器统一 clang。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MIN="${1:-80}"
COV="$ROOT/build/coverage"
mkdir -p "$COV"

run_one() {   # $1=模块名  $2=测试源(tests/ 下)  $3=被测源(common/ 下)
    local m t s dir gcno
    m="$1"; t="$2"; s="$3"; dir="$COV/$m"
    rm -rf "$dir"; mkdir -p "$dir"       # 清旧 gcda/gcno/gcov（checksum 冲突）
    ( cd "$dir" && clang -std=c11 -Wall -Wextra --coverage \
        -I"$ROOT/tests" -I"$ROOT/common/$(dirname "$s")" \
        "$ROOT/tests/$t" "$ROOT/common/$s" -o test_$m )
    ( cd "$dir" && ./test_$m >/dev/null )
    # coverage 文件带产物前缀（test_gatt-gatt.gcno），直接喂 llvm-cov gcov
    gcno="$(cd "$dir" && ls ./*-$(basename "$s" .c).gcno)"
    ( cd "$dir" && llvm-cov gcov -c "$gcno" >/dev/null )
}

run_one gatt gatt_test.c bluetooth/gatt/gatt.c
run_one ll   ll_test.c   bluetooth/ll/ll.c
run_one ring ring_test.c ring/ring.c
run_one log  log_test.c  log/log.c
run_one cobs cobs_test.c cobs/cobs.c
run_one proto proto_test.c proto/proto.c

# 聚合：解析各模块目录里的 *.gcov（gcov 产物 = 源文件基名 + .gcov）
python3 - "$COV" "$MIN" <<'EOF'
import sys, glob, os
cov_root, min_pct = sys.argv[1], int(sys.argv[2])

rows, tot, done = [], 0, 0
for f in sorted(glob.glob(os.path.join(cov_root, "*", "*.gcov"))):
    if f.endswith("test_") or "test_" in os.path.basename(f):
        continue                          # 只看被测模块，不看测试文件本身
    e = t = 0
    for line in open(f, errors="replace"):
        if ':' not in line:
            continue
        cnt = line.split(':', 1)[0].strip()
        if cnt == '-':
            continue                      # 非代码行（注释/空行/预处理）
        t += 1
        if cnt.isdigit() and int(cnt) > 0:
            e += 1                        # #####/===== 计未执行
    pct = e * 100 // t if t else 100
    rows.append((os.path.basename(f)[:-5], e, t, pct))
    tot += t; done += e

bad = []
for name, e, t, pct in rows:
    mark = ""
    if pct < min_pct:
        mark = "  <-- BELOW MIN"
        bad.append(name)
    print(f"{pct:3d}%  {e:4d}/{t:4d}  {name}{mark}")
overall = done * 100 // tot if tot else 100
print(f"{overall:3d}%  {done:4d}/{tot:4d}  TOTAL (min {min_pct}% per-module)")
sys.exit(1 if bad or overall < min_pct else 0)
EOF
