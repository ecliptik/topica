#!/usr/bin/env python3
"""Draws Topica's Finder icons and writes resources/icons.r (ICN# with masks,
and the application's ICON for the About box).

    tools/make-icons.py [--show]

128: the application, a page with a pencil across it.
129: a post, a page with a dog-ear, a Markdown heading mark and lines.
--show prints the bitmaps as text.
"""
import os
import sys

N = 32


def blank():
    return [[0] * N for _ in range(N)]


def hline(b, y, x0, x1):
    for x in range(x0, x1 + 1):
        b[y][x] = 1


def vline(b, x, y0, y1):
    for y in range(y0, y1 + 1):
        b[y][x] = 1


def page(b, m, left, top, right, bottom, ear):
    """A page outline with a folded corner; its filled shape into the mask."""
    hline(b, top, left, right - ear)
    vline(b, left, top, bottom)
    hline(b, bottom, left, right)
    vline(b, right, top + ear, bottom)
    for i in range(ear + 1):                    # the fold's diagonal and its edge
        b[top + i][right - ear + i] = 1
    vline(b, right - ear, top, top + ear)
    hline(b, top + ear, right - ear, right)
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            if not (y - top < ear and x - (right - ear) > y - top):
                m[y][x] = 1


def text_lines(b, left, right, ys, lengths):
    for y, n in zip(ys, lengths):
        hline(b, y, left, min(right, left + n))


def pencil(b, m, x0, y0, length, width):
    """A pencil from (x0, y0) pointing down and left at 45 degrees: white
    inside with a black outline, a band near the top, a sharpened point."""
    def inside(x, y):
        t = ((x0 - x) + (y - y0)) / 2.0      # along the pencil
        w = ((y - y0) - (x0 - x)) / 2.0      # across it
        if t < 0 or t > length + width:
            return False
        half = width if t <= length else width * (length + width - t) / width
        return abs(w) <= half + 0.01
    cells = [(x, y) for y in range(N) for x in range(N) if inside(x, y)]
    for x, y in cells:
        m[y][x] = 1
        b[y][x] = 0
    for x, y in cells:
        edge = any(not (0 <= x + dx < N and 0 <= y + dy < N) or not inside(x + dx, y + dy)
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        t = ((x0 - x) + (y - y0)) / 2.0
        if edge or abs(t - 4) < 0.6 or t > length + width - 1.5:
            b[y][x] = 1


def app_icon():
    b, m = blank(), blank()
    page(b, m, 3, 2, 22, 29, 5)
    text_lines(b, 6, 19, range(10, 27, 3), [8, 12, 10, 12, 6, 11])
    hline(b, 6, 6, 10)                          # a heading
    hline(b, 7, 6, 10)
    pencil(b, m, 28, 3, 16, 3)
    return b, m


def doc_icon():
    b, m = blank(), blank()
    page(b, m, 4, 1, 27, 30, 7)
    # "#": a Markdown heading mark
    for x in (8, 11):
        vline(b, x, 6, 12)
    for y in (8, 10):
        hline(b, y, 7, 13)
    hline(b, 9, 15, 20)                         # the heading
    hline(b, 10, 15, 20)
    text_lines(b, 7, 24, range(15, 28, 3), [16, 12, 17, 9, 14])
    return b, m


def hexrows(bits):
    rows = []
    for row in bits:
        v = 0
        for x in row:
            v = (v << 1) | x
        rows.append("%08X" % v)
    return rows


def rez(rid, name, bits, mask):
    rows = hexrows(bits) + hexrows(mask)
    lines = ['\t$"%s %s %s %s"' % tuple(rows[i:i + 4]) for i in range(0, 64, 4)]
    return "data 'ICN#' (%d, \"%s\", purgeable) {\n%s\n};\n" % (rid, name, "\n".join(lines))


def rez_icon(rid, name, bits):
    rows = hexrows(bits)
    lines = ['\t$"%s %s %s %s"' % tuple(rows[i:i + 4]) for i in range(0, 32, 4)]
    return "data 'ICON' (%d, \"%s\", purgeable) {\n%s\n};\n" % (rid, name, "\n".join(lines))


def show(name, bits):
    print(name)
    for row in bits:
        print("".join("#" if x else "." for x in row))


def main():
    app, appmask = app_icon()
    doc, docmask = doc_icon()
    if "--show" in sys.argv:
        show("128 application", app)
        show("129 post", doc)
        return
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "resources", "icons.r")
    with open(out, "w") as f:
        f.write("/* Topica's Finder icons: made by tools/make-icons.py, don't edit. */\n\n")
        f.write(rez(128, "Topica", app, appmask))
        f.write("\n")
        f.write(rez(129, "Topica post", doc, docmask))
        f.write("\n/* The About box's */\n")
        f.write(rez_icon(128, "Topica", app))
    print("make-icons: wrote", os.path.normpath(out))


if __name__ == "__main__":
    main()
