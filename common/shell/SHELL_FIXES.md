# Shell Fixes Summary

## Files Modified

- `common/shell/shell.h` — struct changes (remove telnet_negotiated, add esc_param)
- `common/shell/shell.c` — logic fixes and ANSI CSI support

## Fix 1: Empty Enter no longer calls execute_line

**Before**: Pressing Enter on an empty line called `execute_line()`, which unconditionally printed `\r\n`, then returned on `argc==0`. The next poll cycle re-showed the prompt. Net effect: an unnecessary blank line appeared.

**After**: `shell_poll()` checks `line_len > 0` before calling `execute_line()`. On empty input, it just prints `\r\n` and resets `prompt_visible`. The prompt appears on the next clean line without a blank gap.

**Change**: `shell_poll()` lines 342-351.

## Fix 2: cmd_help always prints trailing newline

**Before**: The `cmd_help` loop printed `\r\n` only when `c->next != NULL`, so the last command's help text had no trailing newline. The next shell prompt appeared on the same line as the last help entry.

**After**: `\r\n` is printed unconditionally after every entry. Each help line is properly terminated.

**Change**: `cmd_help()` lines 58-68.

## Fix 3: Command-not-found error uses consistent \r\n

**Before**: `": command not found\r"` — used bare `\r` without `\n`, inconsistent with all other output which uses `\r\n`.

**After**: `": command not found\r\n"`.

**Change**: `execute_line()` line 162.

## Fix 4: Telnet negotiation bytes removed

**Before**: `show_prompt()` sent `\xff\xfb\x01` (IAC WILL ECHO) on first call to negotiate telnet echo. For an RTT-backed shell, these bytes are meaningless noise on the debug channel.

**After**: Telnet negotiation code removed entirely. The `telnet_negotiated` field removed from `struct shell`. `show_prompt()` now only handles the `> ` prompt.

**Changes**:
- `shell.h` line 42: removed `bool telnet_negotiated;`
- `shell.h` line 40: added `int esc_param;` (needed by Fix 5)
- `shell.c` lines 124-129: simplified `show_prompt()`

## Fix 5: ANSI CSI escape sequence handling added

**Before**: Only Windows-style extended key codes (`0xE0`/`0x00` prefix bytes) were recognized. Terminals sending standard ANSI CSI sequences (`\033[`) could not navigate the line — arrow keys, Home, End, Delete were non-functional.

**After**: Full CSI state machine added alongside existing Windows extended key handling.

**Supported ANSI sequences**:
| Sequence | Action |
|---|---|
| `ESC [ A` | Up (ignored, no history) |
| `ESC [ B` | Down (ignored, no history) |
| `ESC [ C` | Right |
| `ESC [ D` | Left |
| `ESC [ H` | Home |
| `ESC [ F` | End |
| `ESC [ 1 ~` | Home |
| `ESC [ 3 ~` | Delete |
| `ESC [ 4 ~` | End |
| `ESC [ 7 ~` | Home (alternate) |
| `ESC [ 8 ~` | End (alternate) |
| `ESC O ...` | SS3 prefix (e.g., ESC O H for Home on some terminals) |

**State machine**: `esc_prefix` encodes the current escape state (0 = normal, 0x1B = saw ESC, '[' or 'O' = in CSI sequence). `esc_param` accumulates CSI numeric parameters. Unknown sequences are silently discarded with state reset.

**Changes**:
- `shell.h` line 40: added `int esc_param;`
- `shell.c` lines 253-340: extended escape handling in `shell_poll()`
