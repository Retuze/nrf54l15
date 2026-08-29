#!/usr/bin/env bash
# 重建随仓库分发的工具链库：vendor/picolibc + vendor/compiler-rt（clang builtins）。
#
# 平时不需要跑：产物已随仓库提交，clone 即开箱即用。
# 升级宿主 clang / 改动构建参数后才用它重新生成（然后提交 vendor/ 变更）。
#
# 依赖：
#   - 宿主 clang + llvm 工具链（llvm-ar/nm/ranlib/objcopy）
#   - meson + ninja
#   - picolibc 源码（默认 ~/picolibc/src；版本 1.8.12 验证过）
#   - llvm-project 源码（默认 ~/llvm-project；稀疏克隆即可，
#     版本 tag 建议与本机 clang 匹配）
#
# 获取源码（一次性）：
#   git clone https://github.com/picolibc/picolibc.git ~/picolibc/src
#   git clone --depth 1 --filter=blob:none --sparse \
#       --branch llvmorg-22.1.8 https://github.com/llvm/llvm-project.git ~/llvm-project
#   cd ~/llvm-project && git sparse-checkout set --cone runtimes compiler-rt llvm/cmake cmake
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PICOLIBC_SRC="${PICOLIBC_SRC:-$HOME/picolibc/src}"
LLVM_SRC="${LLVM_SRC:-$HOME/llvm-project}"

[ -d "$PICOLIBC_SRC" ] || { echo "缺 picolibc 源码: $PICOLIBC_SRC（看本文件头部注释）" >&2; exit 1; }
[ -d "$LLVM_SRC/compiler-rt" ] || { echo "缺 llvm-project 源码: $LLVM_SRC（看本文件头部注释）" >&2; exit 1; }

# ---- picolibc：meson cross 构建，装进仓库 vendor/picolibc/arm-none-eabi ----
# 选项含义见 docs/picolibc.md；cross file 参数必须与 targets/nrf54l15.cmake 一致。
PICO_BUILD="$PICOLIBC_SRC/../build-arm-cortex-m33"
meson setup "$PICO_BUILD" "$PICOLIBC_SRC" \
    --cross-file "$ROOT/scripts/picolibc-arm-cross.txt" \
    --prefix="$ROOT/vendor/picolibc/arm-none-eabi" \
    -Dmultilib=false -Dtests=false -Dpicocrt=false -Dspecsdir=none \
    -Dio-long-long=true
ninja -C "$PICO_BUILD"
ninja -C "$PICO_BUILD" install

# ---- compiler-rt builtins：armv8m.main 裸机（clang 按三元组命名为 armhf） ----
# 注意：LLVM 的 AddCompilerRT 会把 eabihf 三元组的库规范化为 "armhf"（clang 驱动
# 只认 arm/armhf 两个名字），所以产物叫 libclang_rt.builtins-armhf.a，内容仍是
# -mcpu=cortex-m33 的代码（见 targets/nrf54l15.cmake 的 EMBED_COMPILER_RT_LIB）。
RT_BUILD="$HOME/build/compiler-rt-arm"
rm -rf "$RT_BUILD"
cmake -S "$LLVM_SRC/runtimes" -B "$RT_BUILD" -G Ninja \
    -DLLVM_ENABLE_RUNTIMES=compiler-rt \
    -DCOMPILER_RT_BAREMETAL_BUILD=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_COMPILER_TARGET=armv8m.main-none-eabihf \
    -DCMAKE_ASM_COMPILER_TARGET=armv8m.main-none-eabihf \
    -DCMAKE_C_FLAGS="-mcpu=cortex-m33 -ffreestanding" \
    -DCMAKE_ASM_FLAGS="-mcpu=cortex-m33" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_AR=/usr/bin/llvm-ar \
    -DCMAKE_NM=/usr/bin/llvm-nm \
    -DCMAKE_RANLIB=/usr/bin/llvm-ranlib \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF
ninja -C "$RT_BUILD" builtins
mkdir -p "$ROOT/vendor/compiler-rt/arm-none-eabi/lib"
cp "$RT_BUILD/compiler-rt/lib/linux/libclang_rt.builtins-armhf.a" \
   "$ROOT/vendor/compiler-rt/arm-none-eabi/lib/"

echo "--> vendor/picolibc 与 vendor/compiler-rt 已重建；提交 vendor/ 变更即可分发"
