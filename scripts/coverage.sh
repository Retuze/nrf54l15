#!/usr/bin/env bash
# common/ 模块宿主测试的代码覆盖率（clang --coverage + llvm-cov gcov，零外部依赖）。
#
# 用法：./scripts/coverage.sh [最小行覆盖率%]   （默认 80）
#   每模块独立目录编译+运行（.gcda 互不污染），llvm-cov gcov 生成 .gcov
#   后按"可执行行"聚合行覆盖（#####/===== 计未执行）。低于门槛退出非 0
#   （check.sh 据此判失败）。报告落在 build/coverage/<module>/。
#   用 llvm-cov 而非 gcc 的 gcov：与 ch32x035 仓库一致（gcc 的 gcov 不认
#   clang 的格式版本 B11*），且全工程编译器统一 clang。
#
# E-SafeNet 兼容（仅个别机器需要，其余环境行为不变）：
#   部分机器装有 E-SafeNet（亿赛通）透明加密，.c/.h 磁盘密文且按进程白名单
#   解密——clang/git/cmd 读到明文，llvm-cov/python 读到密文，gcov 标注被
#   截断到密文行数，覆盖率统计崩坏。对策：检测到密文头时用白名单里的
#   `cmd /c type` 把被测源导出成明文影子副本再编译（-x c）。副本后缀必须
#   避开 .c/.h——写时加密同样按扩展名，任何进程写 .c 落盘都会被再加密。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MIN="${1:-80}"
COV="$ROOT/build/coverage"
mkdir -p "$COV"

export_plain() {   # $1=源文件  $2=目标（后缀须避开 .c/.h，防写时加密）
    if head -c 32 "$1" | grep -q "E-SafeNet"; then
        cmd //c type "$(cygpath -w "$1")" > "$2"
    else
        cp "$1" "$2"
    fi
}

run_one() {   # $1=模块名  $2=测试源(tests/ 下)  $3=被测源(common/ 下)
    local m t s dir gcno src
    m="$1"; t="$2"; s="$3"; dir="$COV/$m"
    rm -rf "$dir"; mkdir -p "$dir"       # 清旧 gcda/gcno/gcov（checksum 冲突）
    # 被测源的明文影子副本（字节同源，行号一致）；gcov 只需标注这一个文件，
    # 测试源/头文件仍走原路径（clang 在白名单，编译不受加密影响）
    src="$(basename "$s" .c).src"
    export_plain "$ROOT/common/$s" "$dir/$src"
    ( cd "$dir" && clang -std=c11 -Wall -Wextra --coverage \
        -I"$ROOT/tests" -I"$ROOT/common/$(dirname "$s")" \
        "$ROOT/tests/$t" -x c "$src" -o test_$m )
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

# 聚合：只解析影子副本的 *.src.gcov（被测模块本体）；测试源、模块头和
# Windows 下带内联函数的系统头（stdio.h 等）产生的 .gcov 一概不计入
python3 - "$COV" "$MIN" <<'EOF'
import sys, glob, os
cov_root, min_pct = sys.argv[1], int(sys.argv[2])

rows, tot, done = [], 0, 0
for f in sorted(glob.glob(os.path.join(cov_root, "*", "*.src.gcov"))):
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
    name = os.path.basename(f)[:-len(".src.gcov")] + ".c"
    rows.append((name, e, t, pct))
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
