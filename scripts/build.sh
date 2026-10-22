#!/bin/bash
# Builds Topica with Retro68.
#   scripts/build.sh [--preset lite|full] [--debug]    (full is the default)
# --debug logs to a "Topica Log" file beside the application.
# Lite is for 512K Macs (the Macintosh 512Ke); Full runs on everything else,
# System 6 and 7. Output: build/<preset>/Topica-<preset>-<version>.{bin,dsk,hqx}
# RETRO68 points at the toolchain (default: the emulator hub's).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
RETRO68=${RETRO68:-$HOME/emulators/tools/retro68-build/toolchain}
toolchain="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake"
[ -f "$toolchain" ] || { echo "build.sh: no Retro68 toolchain at $RETRO68 (set RETRO68)" >&2; exit 1; }

preset=full
debug=OFF
while [ $# -gt 0 ]; do
    case $1 in
        --preset) preset=${2:-}; shift 2 ;;
        --debug) debug=ON; shift ;;
        *) echo "build.sh: unknown option $1" >&2; exit 2 ;;
    esac
done
case $preset in
    lite) lite=ON ;;
    full) lite=OFF ;;
    *) echo "build.sh: unknown preset '$preset' (lite, full)" >&2; exit 2 ;;
esac

# A tagged release shows its version; any other build shows its commit.
version=$(sed -nE 's/^project\(Topica VERSION ([0-9.]+).*/\1/p' "$here/CMakeLists.txt")
display=$version
if ! git -C "$here" tag --points-at HEAD 2>/dev/null | grep -qx "v$version"; then
    sha=$(git -C "$here" rev-parse --short HEAD 2>/dev/null || true)
    [ -n "$sha" ] && display="$version-$sha"
fi

build="$here/build/$preset"
mkdir -p "$build"
rm -f "$build/CMakeCache.txt"
cmake -S "$here" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DTOPICA_LITE=$lite -DTOPICA_DEBUG=$debug -DTOPICA_VERSION_DISPLAY="$display" >/dev/null
cmake --build "$build" -- -j"$(nproc 2>/dev/null || echo 2)"

# MacBinary's Finder flags: set hasBundle (0x2000) so the Finder reads the
# BNDL, and fix the header's CRC.
python3 - "$build/Topica.bin" <<'EOF'
import sys
path = sys.argv[1]
with open(path, 'r+b') as f:
    h = bytearray(f.read(128))
    h[73] |= 0x20
    crc = 0
    for b in h[:124]:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    h[124:126] = crc.to_bytes(2, 'big')
    f.seek(0)
    f.write(bytes(h))
EOF

# An 800K floppy with the application (from the MacBinary, keeping its flags)
# and the About file.
name="Topica-$preset-$display"
dsk="$build/$name.dsk"
rm -f "$dsk"
dd if=/dev/zero of="$dsk" bs=1024 count=800 status=none
hformat -l "Topica" "$dsk" >/dev/null
hmount "$dsk" >/dev/null
hcopy -m "$build/Topica.bin" ":Topica"
if [ -f "$here/docs/About Topica" ]; then
    sed "s/@VERSION@/$display/" "$here/docs/About Topica" | tr '\n' '\r' > "$build/About Topica"
    hcopy -r "$build/About Topica" ":About Topica"
    hattrib -t ttro -c ttxt ":About Topica"
fi
humount >/dev/null
cp "$build/Topica.bin" "$build/$name.bin"
if command -v binhex >/dev/null 2>&1; then
    binhex "$build/$name.bin" > "$build/$name.hqx"
fi
echo
echo "build.sh: Topica $display ($preset)"
for f in "$build/$name".*; do
    echo "  $(readlink -f "$f")"
done
