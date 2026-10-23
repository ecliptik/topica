#!/usr/bin/env python3
"""Drives Topica in Snow (System 6), through the emulator hub's XTEST library.

    tests/snow_session.py STEP [STEP...]

Steps, in order (coordinates are the Mac's 512x342 screen, calibrated each run):
    click:X,Y     a click; dclick:X,Y a double-click
    drag:X1,Y1,X2,Y2  press, move, release
    cmd:K         Command-K (a letter or digit; "period" for Command-.)
    cmdshift:K    Command-Shift-K
    type:TEXT     types plain text
    key:NAME      one key by X keysym name (Return, BackSpace, Tab, Up, Down...)
    menu:X,Y      the menu at X, its item at Y (items 16 apart from y = 28)
    sub:X,Y,X2,Y2 a submenu's item: the menu at X, rest on its item at Y until
                  the submenu opens, then across and down to X2,Y2
    move:X,Y      the pointer, out of a screenshot's way (509,339)
    shot:NAME     a screenshot of the display: /tmp/topica/NAME.png, and the
                  Mac's screen alone: /tmp/topica/NAME-mac.png
    wait:SECONDS  sleep

Snow must be running on DISPLAY=:0 (scripts/deploy.sh snow --launch), or on
the display SNOW_DISPLAY names: a private Xvfb, say, which no desktop prompt
can pop up on (SNOW_DISPLAY=:78 for both deploy.sh and this). Never
use ImageMagick's import here: it grabs the pointer and breaks Snow's mouse.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.environ.get("EMU", os.path.expanduser("~/emulators")), "scripts"))
from snow_automation import SnowAutomation  # noqa: E402

OUT = "/tmp/topica"
DISPLAY = os.environ.get("SNOW_DISPLAY", ":0")
ENV = {**os.environ, "DISPLAY": DISPLAY}


def shot(snow, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".png")
    subprocess.run(["scrot", "-o", path], env=ENV, check=True)
    # the Mac's screen alone, at its own size
    x0, y0 = snow.mac_to_screen(0, 0)
    x1, y1 = snow.mac_to_screen(512, 342)
    try:
        from PIL import Image
        Image.open(path).crop((x0, y0, x1, y1)).save(
            os.path.join(OUT, name + "-mac.png"))
    except ImportError:
        pass
    print("shot", path)


def snow_has_focus():
    """Keystrokes go to whatever window has the focus. Stop rather than
    type into another application (a password prompt that popped up).
    A private display has no window manager and nothing else on it: give
    Snow the focus there."""
    if DISPLAY != ":0":
        subprocess.run(["xdotool", "search", "--name", "Snow", "windowfocus", "--sync"], env=ENV,
                       capture_output=True)
        return
    name = subprocess.run(["xdotool", "getactivewindow", "getwindowname"], env=ENV,
                          capture_output=True, text=True).stdout.strip()
    if "Snow" not in name:
        sys.exit("snow_session: the focused window is %r, not Snow: stopping before typing" % name)


def at(snow, xy, double=False):
    x, y = snow.mac_to_screen(*xy)
    snow.move_to(x, y)
    snow.jiggle()
    time.sleep(0.3)
    if double:
        snow.double_click()
    else:
        snow.click()


def main(steps):
    snow = SnowAutomation(display_name=DISPLAY, auto_calibrate=True)
    for step in steps:
        print("step", step, flush=True)
        kind, _, arg = step.partition(":")
        if kind in ("cmd", "cmdshift", "type", "key"):
            snow_has_focus()
        if kind in ("click", "dclick"):
            x, y = (int(v) for v in arg.split(","))
            at(snow, (x, y), double=kind == "dclick")
            time.sleep(0.5)
        elif kind == "drag":
            x1, y1, x2, y2 = (int(v) for v in arg.split(","))
            snow.move_to(*snow.mac_to_screen(x1, y1))
            snow.jiggle()
            time.sleep(0.3)
            subprocess.run(["xdotool", "mousedown", "1"], env=ENV)
            time.sleep(0.2)
            snow.move_to(*snow.mac_to_screen(x2, y2))
            time.sleep(0.3)
            subprocess.run(["xdotool", "mouseup", "1"], env=ENV)
            time.sleep(0.5)
        elif kind == "cmd":
            snow.cmd_key(arg)
            time.sleep(0.8)
        elif kind == "cmdshift":
            snow.cmd_shift_key(arg)
            time.sleep(0.8)
        elif kind == "type":
            snow.type_text(arg)
            time.sleep(0.5)
        elif kind == "key":
            snow.key_sym_press(arg)
            time.sleep(0.3)
        elif kind == "menu":
            x, y = (int(v) for v in arg.split(","))
            snow.menu_select(x, y)
            time.sleep(1.5)
        elif kind == "sub":
            x, y, x2, y2 = (int(v) for v in arg.split(","))
            path = [(x, 8)] + [(x, 8 + (y - 8) * i // 12) for i in range(1, 13)]
            path += [(x + (x2 - x) * i // 12, y) for i in range(1, 13)]
            path += [(x2, y + (y2 - y) * i // 12) for i in range(1, 13)]
            snow.move_to(*snow.mac_to_screen(x, 8))
            time.sleep(0.3)
            subprocess.run(["xdotool", "mousedown", "1"], env=ENV)
            time.sleep(0.5)
            for n, (mx, my) in enumerate(path):
                snow._xtest_move(*snow.mac_to_screen(mx, my))
                time.sleep(0.8 if n == 12 else 0.03)   # the submenu opens after a rest
            time.sleep(0.3)
            subprocess.run(["xdotool", "mouseup", "1"], env=ENV)
            time.sleep(1.5)
        elif kind == "move":
            x, y = (int(v) for v in arg.split(","))
            snow.move_to_mac(x, y)
            time.sleep(0.3)
        elif kind == "shot":
            shot(snow, arg)
        elif kind == "wait":
            time.sleep(float(arg))
        else:
            sys.exit("snow_session: unknown step %r" % step)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
