#!/bin/bash
# Puts the last build where an emulator finds it, with the test posts.
#   scripts/deploy.sh snow [--preset lite|full] [--launch]
#   scripts/deploy.sh basilisk [--preset lite|full] [--launch]
# Snow's disk must not change while Snow runs: this stops Snow first (by
# PID, never pkill -f, which would match this very command line). Never run
# Snow and Basilisk II at once: they share the display.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
EMU=${EMU:-$HOME/emulators}
target=${1:?usage: deploy.sh snow|basilisk [--preset lite|full] [--launch]}
shift
preset=full
launch=0
while [ $# -gt 0 ]; do
    case $1 in
        --preset) preset=$2; shift 2 ;;
        --launch) launch=1; shift ;;
        *) echo "deploy: unknown option $1" >&2; exit 2 ;;
    esac
done
bin="$here/build/$preset/Topica.bin"
[ -f "$bin" ] || { echo "deploy: no build at $bin (scripts/build.sh --preset $preset)" >&2; exit 1; }
fixtures="$here/tests/fixtures"

stop_snow() {
    local pid
    pid=$(pgrep -x snowemu || true)
    [ -z "$pid" ] && return 0
    kill $pid; for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -x snowemu >/dev/null || return 0; sleep 0.5; done
    kill -9 $pid 2>/dev/null || true; sleep 1
    pgrep -x snowemu >/dev/null && { echo "deploy: Snow won't stop" >&2; exit 1; }
    return 0
}

case $target in
snow)
    img="$here/diskimages/topica-sys608.img"
    [ -f "$img" ] || { echo "deploy: no $img (scripts/setup-emulators.sh)" >&2; exit 1; }
    pgrep -x BasiliskII >/dev/null && { echo "deploy: Basilisk II is running; quit it first" >&2; exit 1; }
    stop_snow
    hmount "$img" >/dev/null
    # at the top level: a folder made by hfsutils has no window position,
    # and the Finder opens it off-screen
    hdel ":Topica" 2>/dev/null || true
    hcopy -m "$bin" ":Topica"
    # a test post as another Mac's text file, and as a file server leaves a .md
    hdel ":Round Trip" 2>/dev/null || true
    hcopy -r "$fixtures/round-trip.md" ":Round Trip"
    hattrib -t TEXT -c ttxt ":Round Trip"
    hdel ":untyped.md" 2>/dev/null || true
    hcopy -r "$fixtures/round-trip.md" ":untyped.md"
    hattrib -t '????' -c '????' ":untyped.md"
    humount >/dev/null
    echo "deploy: Topica ($preset) is on $img"
    if [ $launch = 1 ]; then
        DISPLAY=:0 SDL_AUDIODRIVER=dummy "$EMU/snow/snowemu" "$here/diskimages/topica.snoww" >/tmp/topica-snow.log 2>&1 &
        echo "deploy: Snow running (PID $!), log /tmp/topica-snow.log"
    fi
    ;;
basilisk)
    mkdir -p "$EMU/unix/Topica"
    python3 "$EMU/basilisk/macbin_to_extfs.py" "$bin" "$EMU/unix/Topica" Topica >/dev/null
    cp "$fixtures/round-trip.md" "$EMU/unix/Topica/round-trip.md"
    echo "deploy: Topica ($preset) is in $EMU/unix/Topica (the Unix disk in Basilisk II)"
    if [ $launch = 1 ]; then
        prefs="$here/diskimages/basilisk_prefs"
        [ -f "$prefs" ] || { echo "deploy: no $prefs (scripts/setup-emulators.sh)" >&2; exit 1; }
        pgrep -x snowemu >/dev/null && { echo "deploy: Snow is running; quit it first" >&2; exit 1; }
        pgrep -x BasiliskII >/dev/null && { echo "deploy: Basilisk II is already running" >&2; exit 0; }
        DISPLAY=:0 BasiliskII --config "$prefs" >/tmp/topica-basilisk.log 2>&1 </dev/null &
        echo "deploy: Basilisk II running (PID $!), log /tmp/topica-basilisk.log"
    fi
    ;;
*)
    echo "deploy: unknown target $target (snow or basilisk)" >&2; exit 2 ;;
esac
