#!/usr/bin/env bash
# Builds a CrossPoint dynamic app (.eapp) from C sources.
#
#   ./build-eapp.sh apps/sysmon [out/sysmon.eapp]
#
# Output layout contract (see eapp.lds.in and docs/engineering/dynapp.md):
# text at vaddr 0x700000, byte-accessed sections at vaddr == physical offset.
# The data base depends on the text size, so this links twice: pass 1 with a
# placeholder, pass 2 with DATA_VBASE = align16(text span). Text content is
# identical between passes (only vaddrs shift), so pass 2 is exact.
#
# Toolchain: riscv32-esp-elf-gcc, from $RISCV_TOOLCHAIN or PlatformIO's
# default install location.
set -euo pipefail

SDK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SDK_DIR/../.." && pwd)"
APP_DIR="${1:?usage: build-eapp.sh <app-dir> [out.eapp]}"
APP_NAME="$(basename "$APP_DIR")"
OUT="${2:-$SDK_DIR/out/$APP_NAME.eapp}"

TC_BIN="${RISCV_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-riscv32-esp/bin}"
CC="$TC_BIN/riscv32-esp-elf-gcc"
READELF="$TC_BIN/riscv32-esp-elf-readelf"
[ -x "$CC" ] || { echo "error: $CC not found (set RISCV_TOOLCHAIN)"; exit 1; }

BUILD="$SDK_DIR/out/.build-$APP_NAME"
mkdir -p "$BUILD" "$(dirname "$OUT")"

CFLAGS=(
  -march=rv32imc_zicsr -mabi=ilp32
  -Os -fPIC -fvisibility=hidden
  -ffunction-sections -fdata-sections
  -fno-builtin-printf
  -Wall -Wextra
  -I"$REPO_DIR/lib/DynApp"
  -I"$SDK_DIR/libmini"
)
LDFLAGS=(
  -shared -nostdlib
  -Wl,--gc-sections
  -Wl,--hash-style=sysv
  -Wl,-z,max-page-size=16
  -Wl,--no-undefined
)

SRCS=("$APP_DIR"/*.c "$SDK_DIR/libmini/mini_libc.c")

link_with_databases() {
  local data_vbase=$1 out=$2
  sed "s/@DATA_VBASE@/$data_vbase/" "$SDK_DIR/eapp.lds.in" > "$BUILD/eapp.lds"
  "$CC" "${CFLAGS[@]}" "${LDFLAGS[@]}" -Wl,-T,"$BUILD/eapp.lds" -o "$out" "${SRCS[@]}"
}

# Pass 1: placeholder data base, far below the text window.
link_with_databases 0x400000 "$BUILD/pass1.so"

# Text span = the RX PT_LOAD's MemSiz (vaddr 0x700000).
TEXT_SIZE=$("$READELF" -lW "$BUILD/pass1.so" | python3 -c "
import sys
for line in sys.stdin:
    f = line.split()
    if f and f[0] == 'LOAD' and int(f[2], 16) >= 0x700000:
        print(int(f[5], 16))
        break
")
[ -n "$TEXT_SIZE" ] || { echo "error: no text segment found"; exit 1; }
DATA_VBASE=$(( (TEXT_SIZE + 15) / 16 * 16 ))

# Pass 2: real layout.
link_with_databases "$DATA_VBASE" "$OUT"

# Structural verification (fails the build on any layout/reloc violation).
python3 "$SDK_DIR/verify-eapp.py" "$OUT"

SIZE=$(stat -c%s "$OUT" 2>/dev/null || stat -f%z "$OUT")
echo "built $OUT ($SIZE bytes, text=$TEXT_SIZE data_vbase=$DATA_VBASE)"
