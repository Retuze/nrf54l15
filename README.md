# nRF54L15 bare-metal (Seeed XIAO nRF54L15)

Fully self-controlled bare-metal project for the nRF54L15 application core
(Cortex-M33), built with the **LLVM/Clang** toolchain and flashed over the
board's **onboard SAMD11 CMSIS-DAP** debugger using **pyOCD** — no SEGGER
J-Link and no vendor SDK required.

Goal: reimplement a BLE LE stack from scratch on the RADIO peripheral
(referencing Zephyr), and later experiment with the FLPR (RISC-V) core.

## Layout

```
cmake/arm-clang.cmake       LLVM toolchain file (clang + lld, from PATH)
linker/nrf54l15_cpuapp.ld   Memory map: RRAM @0x0 (1524K), SRAM @0x20000000 (256K)
src/startup_nrf54l15.c      Vector table + reset handler (FPU, .data/.bss init)
src/main.c                  Blinky: P2.00 (user LED, active low)
vendor/                     (later) Nordic MDK/CMSIS headers for RADIO etc.
```

## Board facts (verified from Zephyr DTS + Nordic MDK)

| Item | Value |
|---|---|
| Core | Arm Cortex-M33 @ up to 128 MHz + FLPR (RV32EMC) |
| Code memory | RRAM, 1524 KB @ 0x00000000 |
| RAM | 256 KB @ 0x20000000 |
| User LED | P2.00, **active low** |
| User button | P0.00, active low, pull-up |
| GPIO2 (P2) secure base | 0x50050400 |
| GPIO regs (nRF54L) | OUT 0x00, OUTSET 0x04, OUTCLR 0x08, IN 0x0C, DIR 0x10, DIRSET 0x14, DIRCLR 0x18, PIN_CNF[n] 0x80+4n |
| pyOCD target | `nrf54l` (builtin) |

> Note: the nRF54L GPIO register offsets differ from the classic nRF52 layout.

## Prerequisites (on PATH)

`clang`, `llvm-objcopy`, `llvm-size` (LLVM), `cmake`, `ninja`, `pyocd`,
and `arm-none-eabi-gdb` (for debugging). No libc is needed yet (`-nostdlib`).

## Build

```
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-clang.cmake
ninja -C build
```

Outputs: `build/app.elf`, `build/app.hex`, `build/app.bin`, `build/app.map`.

## Flash (onboard CMSIS-DAP via pyOCD)

```
pyocd flash -t nrf54l build/app.hex
```

## Debug

Terminal 1 — start the GDB server:

```
pyocd gdbserver -t nrf54l
```

Terminal 2 — attach GDB:

```
arm-none-eabi-gdb build/app.elf -ex "target remote :3333" -ex "load" -ex "monitor reset halt"
```

Quick register inspection without GDB:

```
pyocd cmd -t nrf54l -c "reset" -c "halt" -c "reg pc" -c "read32 0x50050410"
```
```
