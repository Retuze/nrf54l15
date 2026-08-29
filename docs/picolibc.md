# picolibc 自建笔记（arm-none-eabi / Cortex-M33）

随仓库分发的 `vendor/picolibc/arm-none-eabi`（libc.a 等 5.5M）与
`vendor/compiler-rt/arm-none-eabi`（clang builtins，240K）是**自建**的，
不是 Debian 包。重建方式：`./scripts/build_toolchain_libs.sh`（参数已固化）。

## 1. 构建命令与选项（picolibc 1.8.12）

```sh
meson setup build-arm-cortex-m33 ~/picolibc/src \
    --cross-file scripts/picolibc-arm-cross.txt \
    --prefix=vendor/picolibc/arm-none-eabi \
    -Dmultilib=false -Dtests=false -Dpicocrt=false -Dspecsdir=none \
    -Dio-long-long=true
```

### 我们显式指定的选项

| 选项 | 值 | 为什么 |
|---|---|---|
| `multilib` | false | 只构建 cross file 里 `-mcpu=cortex-m33` 这一种变体，产物就在 `lib/` 根下；multilib=true 会为每个 `-mcpu/-march/-mfloat` 组合建一层子目录（Debian 包就是 multilib 布局） |
| `tests` | false | 不开单元测试（测试要 QEMU/semihost 运行环境，交叉构建用不上） |
| `picocrt` | false | 不用 picolibc 的 crt0/启动代码——工程有自己的 `startup.c`（向量表 + 数据段拷贝 + `_set_tls` + `__libc_init_array`） |
| `specsdir` | none | 不安装 gcc specs 文件（我们用 clang + 显式链接参数，不走 `-specs=picolibc.specs`） |
| `io-long-long` | true | printf/scanf 支持 `%llu` 等 64 位转换（grtc 的 52 位时间戳日志要用）；代价约 +1.5K text |
| `posix-console` | true | 库里预装 fd 0/1/2 的带缓冲 FILE（`FDEV_SETUP_POSIX`，512B 缓冲、行缓冲 `__BLBF`，换行即 flush），底层走 `write(fd)`；应用侧提供 `_write` 强实现（drivers/uart/uart.c）落地。`stdout/stdin/stderr` 是**弱符号**，应用想接管仍可给强定义 |

### 保留默认值的关键选项（meson_options.txt 全表在 `~/picolibc/src/meson_options.txt`）

**TLS 组**（errno/reent 放哪）：

| 选项 | 默认 | 说明 |
|---|---|---|
| `thread-local-storage` | `picolibc` | errno 等静态数据走 TLS。`picolibc` = 用 picolibc 自己的 tp 读取方案（ARMv8-M 读 TPIDRURO）；`auto` = 只要工具链支持就开；`false` = 退回全局 errno（多线程不安全但省 100 字节级） |
| `tls-model` | `local-exec` | 静态 TLS 模型：TLS 块地址由链接器直接解析，无需运行期重定位。**要求编译 App 也带 `-ftls-model=local-exec`**（见 targets/nrf54l15.cmake），且 link.ld 提供 `__tls_base/__tls_align/__arm32_tls_tcb_offset`，startup.c 里 `_set_tls()` 安装 |
| `newlib-global-errno` | false | 全局 errno 兼容开关（开了就是"新 libc 行为"；配合 TLS 关时用） |
| `stack-protector-guard` | auto | `-fstack-protector` 金丝雀放全局变量还是 TLS |

**stdio 组**（printf/scanf 的大小与功能取舍，tinystdio 路线）：

| 选项 | 默认 | 说明 |
|---|---|---|
| `format-default` | `d` | 默认 printf 变体：`double`/`float`/`long-long`/`integer`/`minimal`。`d` = double 完整版（支持 %f/%e/%g 双精度浮点）；`m` = minimal（无浮点，2K 级） |
| `io-long-long` | false | 64 位整型转换（我们开了） |
| `io-float-exact` | true | 浮点↔字符串往返精度（多几百字节，保证 printf 再 sscanf 无损） |
| `io-c99-formats` | true | C99 格式（%zu/%td 等） |
| `io-pos-args` | false | `%2$d` 位置参数（浮点版总是带上） |
| `io-long-double` | false | long double 支持（Cortex-M 上 long double 也 8 字节，一般不开） |
| `printf-small-ultoa` | true | 十进制转换避开软件除法（M33 有硬件除，省空间） |
| `posix-console` | **true** | 见上表：预接线 stdin/stdout/stderr 到 fd 0/1/2 的带缓冲 FILE；不开则三个流无定义，应用必须自备 `struct __file` 样板（我们以前的方案） |
| `atomic-ungetc` | true | ungetc 用原子操作保证可重入 |
| `fast-bufio` / `stdio-locking` | false | 缓冲 IO/文件锁——裸机单线程用不上 |

**malloc 组**：

| 选项 | 默认 | 说明 |
|---|---|---|
| `enable-malloc` | true | malloc 家族基于 sbrk 实现；sbrk 用 link.ld 的 `__heap_start/__heap_end` 符号（libos/fallback/sbrk.c） |
| `internal-heap` | 0 | >0 时 libc 内部直接留一块静态堆（`char __heap_start[N]`），不依赖链接脚本；我们走 0（链接脚本定义） |
| `malloc-small-bucket` | 0 | 小对象固定桶加速（0=关闭） |
| `malloc-clear-freed` | false | free 时清内存（调试用） |

**启动/系统调用组**：

| 选项 | 默认 | 说明 |
|---|---|---|
| `semihost` | true | 包含 semihost 版系统调用（调试器下 IO 用）。链入 libsemihost.a 时才生效，我们不用 |
| `initfini-array` | true | 用 `.init_array` 段（startup.c 调 `__libc_init_array` 的前提） |
| `single-thread` | false | 关掉后才有锁结构；裸机单线程可开以省体积（暂未开） |
| `want-math-errno` | false | 数学函数是否设 errno（开销大，默认关） |

**国际化组**（mb-capable 等）：默认全关——不支持多字节/宽字符，省几 KB。

## 2. cross file（scripts/picolibc-arm-cross.txt）

```ini
[binaries]
c = ['clang', '--target=arm-none-eabi', '-mcpu=cortex-m33',
     '-mfloat-abi=hard', '-mfpu=fpv5-sp-d16',
     '-ftls-model=local-exec', '-fno-pic']
ar = 'llvm-ar'; nm = 'llvm-nm'; strip = 'llvm-strip'
[built-in options]
c_link_args = ['-fuse-ld=lld']
[host_machine]
system = 'none'; cpu_family = 'arm'; cpu = 'cortex-m33'; endian = 'little'
```

- **关键约定**：编译参数必须与 `cmake/targets/nrf54l15.cmake` 的 `EMBED_CPU_FLAGS`
  完全一致——尤其 `-mfloat-abi=hard` 和 `-ftls-model=local-exec`。ABI 不一致在链接期
  表现为 `uses VFP register arguments` 一类错误。
- `[host_machine]` 的 `system='none'` 让 meson 按裸机构建（不做 libc 探测）。
- picolibc 自身编译**不需要** libgcc/builtins（那是链接期的事）。

## 3. clang builtins（compiler-rt）代替 libgcc

gcc-arm-none-eabi 的 libgcc 提供 `__aeabi_*`（除法、64 位、软浮点辅助函数）。
为了"只有 clang 也能开箱即用"，我们自建 compiler-rt builtins：

```sh
cmake -S llvm-project/runtimes -B build/compiler-rt-arm -G Ninja \
    -DLLVM_ENABLE_RUNTIMES=compiler-rt -DCOMPILER_RT_BAREMETAL_BUILD=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_COMPILER_TARGET=armv8m.main-none-eabihf \
    -DCMAKE_ASM_COMPILER_TARGET=armv8m.main-none-eabihf \
    -DCMAKE_C_FLAGS="-mcpu=cortex-m33 -ffreestanding" \
    -DCMAKE_ASM_FLAGS="-mcpu=cortex-m33" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_AR=/usr/bin/llvm-ar ...
ninja -C build/compiler-rt-arm builtins
```

踩过的坑（都固进 build_toolchain_libs.sh 了）：

1. **llvm-project 要稀疏克隆**：`--filter=blob:none --sparse` 后
   `git sparse-checkout set --cone runtimes compiler-rt llvm/cmake cmake`——
   runtimes 构建需要根 `cmake/Modules`（CMakePolicy/SortSubset）和 `llvm/cmake/modules`，
   缺了报 "Unknown CMake command sort_subset"。
2. **必须指定 ASM 编译器 target**：`CMAKE_ASM_COMPILER_TARGET` 不给的话，
   arm/*.S 用宿主（aarch64）clang 汇编，报 `.syntax unified` 未知。
3. **`COMPILER_RT_DEFAULT_TARGET_ONLY=ON`**：否则会为 aarch64 等全部架构构建。
4. **产物名字是 armhf**：LLVM 的 AddCompilerRT 按"三元组带 eabihf"把库名规范化为
   `libclang_rt.builtins-armhf.a`（clang 驱动只认 arm/armhf），内容仍是 m33 代码。
5. 版本 tag 与本机 clang 对齐（本机 clang 22.1.8 → `llvmorg-22.1.8`）。

链接顺序（embedded.cmake 已固化）：`libc.a → libm.a → libclang_rt.builtins-armhf.a`，
辅助库必须在 libc 之后，`__aeabi_*` 才能在静态链接时被解析。

## 4. 工程侧怎么接（链路全貌）

```
编译期   targets/nrf54l15.cmake: -ftls-model=local-exec
        embedded.cmake: -nostdlib -nostartfiles -lc -lm -lclang_rt.builtins
链接期   drivers/core/nrf54l15.ld: .tdata/.tbss + __tls_base/__tls_align/
                 __arm32_tls_tcb_offset + __heap_start/__heap_end（sbrk 用）
运行期   drivers/core/startup.c: 拷 .data/.tdata → 清零 tbss+bss →
                 _set_tls(__tls_base) → __libc_init_array() → main()
stdio    posix-console：库里的 stdout（弱符号，带 512B 缓冲、行缓冲）——
        printf 每行（\n）flush 到 write(1) → drivers/uart/uart.c 的强 write
        → uart_write → UARTE20
系统调用 picolibc 直接用 POSIX 名（write/read/lseek/close/...，只有 _exit
        保留 newlib 下划线惯例）：drivers/core/syscalls.c 给弱桩（-1 兜底），
        write 的强实现见 uart.c；应用可随时强定义覆盖任何一个
```

对照 nrf52840 仓库的同一套做法（Debian picolibc + libgcc），差异只在：
libc 与辅助库都换成自建 + 随仓库 vendor。
