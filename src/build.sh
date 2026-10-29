#!/usr/bin/env bash
# Baut dsound.dll (dk2hires) mit einem i686-w64-mingw32-gcc.
# Aufruf: ./build.sh [ausgabeverzeichnis]
# Mit gcc 15.2.0 und binutils 2.46 entsteht dieselbe Datei wie im Release (SHA-256 in der README).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"
CC="${CC:-i686-w64-mingw32-gcc}"
command -v "$CC" >/dev/null 2>&1 || { echo "$CC fehlt (Paket mingw-w64 bzw. gcc-mingw-w64-i686)"; exit 1; }
WINDRES="${CC%gcc}windres"
export PATH="$(dirname "$(command -v "$CC")"):$PATH"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1791288256}"   # Zeitstempel des Releases, macht den Build wiederholbar
"$WINDRES" -O coff -o "$OUT/version.o" "$HERE/version.rc"
"$CC" -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 -shared \
  -nostdlib -nostartfiles -e _DllMain@12 -fno-stack-protector -fno-builtin \
  -o "$OUT/dsound.dll" \
  "$HERE/dk2hires.c" "$HERE/hooks.c" "$HERE/tilefix.c" "$HERE/heapfix.c" "$HERE/camfix.c" "$HERE/fontfix.c" "$HERE/menufix.c" "$HERE/cursorfix.c" "$HERE/wheelzoom.c" "$HERE/diag_bp.c" "$HERE/dsound_proxy.c" "$OUT/version.o" "$HERE/dsound.def" \
  -Wl,--enable-stdcall-fixup -Wl,--dynamicbase -Wl,--nxcompat -Wl,--image-base=0x68b80000 \
  -lkernel32 -luser32 -ladvapi32 -lgcc
"${CC%gcc}strip" --strip-unneeded "$OUT/dsound.dll"
sha256sum "$OUT/dsound.dll"
