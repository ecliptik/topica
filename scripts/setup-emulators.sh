#!/bin/bash
# Makes Topica's own emulator disks from the classic Mac emulator hub
# (~/emulators: Snow, Basilisk II, ROMs, a System 6.0.8 and a System 7 disk;
# set EMU for another place). Run once; it never changes the hub's disks.
#   scripts/setup-emulators.sh [--force]
#
#   diskimages/topica-sys608.img  System 6.0.8, a copy of the hub's disk with
#                                 only the System at the top level
#   diskimages/topica.snoww       Snow: a Mac Plus with that disk
#   diskimages/topica-sys7.hda    System 7, a copy of the hub's disk
#   diskimages/basilisk_prefs     Basilisk II with that disk, slirp networking (Publish)
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
EMU=${EMU:-$HOME/emulators}
src=${TOPICA_SYS6_IMAGE:-$EMU/disks/system6.img}
src7=${TOPICA_SYS7_IMAGE:-$EMU/disks/system7.hda}
rom=${TOPICA_PLUS_ROM:-$EMU/roms/128KB ROMs/1986-03 - 4D1F8172 - MacPlus v3.ROM}
out="$here/diskimages"
img="$out/topica-sys608.img"
img7="$out/topica-sys7.hda"
force=0; [ "${1:-}" = "--force" ] && force=1

[ -f "$src" ] || { echo "setup-emulators: no System 6 disk at $src (set TOPICA_SYS6_IMAGE)" >&2; exit 1; }
[ -f "$rom" ] || { echo "setup-emulators: no Mac Plus ROM at $rom (set TOPICA_PLUS_ROM)" >&2; exit 1; }
if pgrep -x snowemu >/dev/null || pgrep -x snowemu-hd20 >/dev/null || pgrep -x snowemu-next >/dev/null || pgrep -x BasiliskII >/dev/null; then
    echo "setup-emulators: an emulator is running; quit it first (its disk is memory-mapped)" >&2; exit 1
fi
mkdir -p "$out"

if [ -f "$img" ] && [ $force = 0 ]; then
    echo "setup-emulators: $img exists (--force replaces it)"
else
    cp "$src" "$img"
    hmount "$img" >/dev/null
    rmtree() {
        local d=$1 n
        hls -a1 "$d" | while IFS= read -r n; do
            [ -n "$n" ] || continue
            if [ "$(hls -ld "$d:$n" | cut -c1)" = d ]; then rmtree "$d:$n"; else hdel "$d:$n"; fi
        done
        hrmdir "$d"
    }
    hls -a1 ":" | while IFS= read -r n; do
        case $n in "System Folder"|"Desktop Folder"|"Trash"|"TeachText"|"") ;;
        *) if [ "$(hls -ld ":$n" | cut -c1)" = d ]; then rmtree ":$n"; else hdel ":$n"; fi ;;
        esac
    done
    humount >/dev/null
    echo "setup-emulators: made $img"
fi

snoww() {   # model, file
    cat > "$out/$2" <<JSON
{
  "viewport_scale": 1.5,
  "rom_path": "$rom",
  "scsi_targets": [
    {"Disk": "topica-sys608.img"},
    "None",
    "None",
    "None",
    "None",
    "None",
    "None"
  ],
  "model": "$1",
  "map_cmd_ralt": true,
  "scaling_algorithm": "NearestNeighbor",
  "framebuffer_mode": "Centered",
  "shader_configs": []
}
JSON
    echo "setup-emulators: wrote $out/$2"
}
snoww Plus topica.snoww

if [ -f "$src7" ]; then
    if [ ! -f "$img7" ] || [ $force = 1 ]; then
        cp "$src7" "$img7"
        echo "setup-emulators: made $img7"
    fi
    hubprefs="$EMU/basilisk/basilisk_ii_prefs"
    if [ -f "$hubprefs" ]; then
        { sed -e '/^ether /d' -e "s#^disk .*#disk $img7#" "$hubprefs"; echo "ether slirp"; } > "$out/basilisk_prefs"
        echo "setup-emulators: wrote $out/basilisk_prefs (slirp: the host is 10.0.2.2)"
    fi
    mkdir -p "$EMU/unix/Topica"
else
    echo "setup-emulators: no System 7 disk at $src7; skipping Basilisk II"
fi
