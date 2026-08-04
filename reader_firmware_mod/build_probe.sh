#!/usr/bin/env bash
# build_probe.sh — compile + link the read-session state probe (stack_canary.c + probe_entry.S)
# into a raw blob for splice_probe.py. Mirrors build.sh; separate outputs so it can never
# overwrite the production hook artifacts.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

# Toolchain lookup. NOTE: $HOME is /w/ in some shells on this machine, which is why build.sh's
# bare $HOME fallback fails here -- try the real user profile too, and allow an explicit override.
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  TCP=""
else
  for cand in "${TOOLCHAIN_BIN:-}"               "$HOME/.platformio/packages/toolchain-gccarmnoneeabi/bin"               "$USERPROFILE/.platformio/packages/toolchain-gccarmnoneeabi/bin"               "/c/Users/$USERNAME/.platformio/packages/toolchain-gccarmnoneeabi/bin"; do
    [ -n "$cand" ] && [ -x "$cand/arm-none-eabi-gcc.exe" -o -x "$cand/arm-none-eabi-gcc" ] && TCP="$cand/" && break
  done
  if [ -z "${TCP:-}" ]; then
    echo "arm-none-eabi-gcc not found. Set TOOLCHAIN_BIN=/path/to/toolchain/bin" >&2; exit 1
  fi
fi
GCC="${TCP}arm-none-eabi-gcc"; OBJCOPY="${TCP}arm-none-eabi-objcopy"
NM="${TCP}arm-none-eabi-nm";   SIZE="${TCP}arm-none-eabi-size"

# -fno-jump-tables is REQUIRED: with more than a handful of cases GCC emits a Thumb-1 jump table
# that calls __gnu_thumb1_case_uqi/_sqi from libgcc, which does not exist in this freestanding
# -nostdlib link. It rewrites an if/else chain back into one too, so the flag is the only fix.
CFLAGS="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -Os \
  -ffreestanding -fno-builtin -fomit-frame-pointer \
  -fno-asynchronous-unwind-tables -fno-unwind-tables \
  -ffunction-sections -fdata-sections -fno-jump-tables   -Wall -Wextra -Werror -std=c11"

"$GCC" $CFLAGS -c stack_canary.c -o build/probe.o
"$GCC" -mcpu=cortex-m0plus -mthumb -c probe_entry.S -o build/probe-entry.o
"$GCC" $CFLAGS -nostdlib -Wl,-T,probe_link.ld -Wl,--gc-sections \
  -Wl,-Map=build/probe.map -o build/probe.elf build/probe.o build/probe-entry.o
"$OBJCOPY" -O binary build/probe.elf build/probe.bin
"$NM" --defined-only build/probe.elf | awk '$2=="T" || $2=="t" {print}' > build/probe.sym
echo "BUILD OK"
echo "--- symbols ---"; cat build/probe.sym
echo "--- size ---";    "$SIZE" build/probe.elf
ls -la build/probe.bin
