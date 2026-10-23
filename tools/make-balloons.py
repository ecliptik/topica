#!/usr/bin/env python3
"""Makes resources/balloons.r (Balloon Help: 'hmnu' and 'hdlg') from resources/balloons.txt.

    tools/make-balloons.py

Retro68 has no Balloons.r, so the resources are written as data. Their layout, as System 7's own:
  'hmnu': version 2, options (long), balloon proc, variant, a count (the title and the items), the missing
          items' record, then the title's record and one per item.
  'hdlg': version 2, an item offset, options (long), balloon proc, variant, a count (the items), the missing
          items' record, then one record per item.
  A record: its size, its type (1: four strings; 256: none), for a dialog item a tip point and a hot rectangle
  (the item's rectangle from its DITL in topica.r.in, the tip near its right end), then the strings: enabled,
  dimmed, checked, other mark. Each string is a Pascal
  string in Mac Roman; a record is padded to an even size.
"""
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SKIP = 256
STRINGS = 1


def pstr(text):
    b = text.encode("mac_roman")
    if len(b) > 255:
        sys.exit("make-balloons: longer than 255 characters: " + text)
    return bytes([len(b)]) + b


def ditl_rects(dlg_id):
    """The item rectangles (top, left, bottom, right) of DITL dlg_id in topica.r.in, in item order."""
    src = open(os.path.join(ROOT, "resources", "topica.r.in"), encoding="latin-1").read()
    m = re.search(r"resource 'DITL' \(%d\b.*?\n\};" % dlg_id, src, re.S)
    if not m:
        sys.exit("make-balloons: no DITL %d in topica.r.in" % dlg_id)
    return [tuple(int(v) for v in r) for r in re.findall(r"\{ (-?\d+), (-?\d+), (-?\d+), (-?\d+) \},", m.group(0))]


def record(texts, rect=None):
    if texts is None:
        return struct.pack(">hh", 4, SKIP)
    enabled, dimmed = texts
    data = b""
    if rect is not None:
        top, left, bottom, right = rect
        tip = ((top + bottom) // 2, max(left, right - 8))
        data = struct.pack(">hhhhhh", tip[0], tip[1], top, left, bottom, right)
    data += pstr(enabled) + pstr(dimmed) + pstr(enabled) + pstr(enabled)
    if (4 + len(data)) % 2:
        data += b"\0"
    return struct.pack(">hh", 4 + len(data), STRINGS) + data


def resource(kind, rid, items, missing):
    dialog = kind == "hdlg"
    rects = ditl_rects(rid) if dialog else None
    last = max(items) if items else 0
    first = 1 if dialog else 0              # a menu's records start with its title
    body = struct.pack(">h", 2)
    if dialog:
        body += struct.pack(">h", 0)        # item number offset
    body += struct.pack(">lhh", 0, 0, 0)
    body += struct.pack(">h", last - first + 1)
    body += record((missing, "") if missing else None, (0, 0, 0, 0) if dialog else None)
    for n in range(first, last + 1):
        body += record(items.get(n), rects[n - 1] if dialog else None)
    return body


def parse(path):
    out, cur = [], None
    for raw in open(path, encoding="utf-8"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        word = line.split()
        if word[0] in ("menu", "dialog"):
            cur = {"kind": "hmnu" if word[0] == "menu" else "hdlg", "id": int(word[1]), "items": {}, "missing": None}
            out.append(cur)
            continue
        key, _, text = line.partition(":")
        if key == "missing":
            cur["missing"] = text.strip()
            continue
        enabled, _, dimmed = text.partition("|")
        cur["items"][int(key)] = (enabled.strip(), dimmed.strip())
    return out


def main():
    res = parse(os.path.join(ROOT, "resources", "balloons.txt"))
    lines = ["/* Topica's Balloon Help: made by tools/make-balloons.py from balloons.txt, don't edit. */", ""]
    for r in res:
        body = resource(r["kind"], r["id"], r["items"], r["missing"])
        lines.append("data '%s' (%d, purgeable) {" % (r["kind"], r["id"]))
        for i in range(0, len(body), 16):
            chunk = body[i:i + 16].hex().upper()
            lines.append('\t$"%s"' % " ".join(chunk[j:j + 8] for j in range(0, len(chunk), 8)))
        lines.append("};")
        lines.append("")
    with open(os.path.join(ROOT, "resources", "balloons.r"), "w") as f:
        f.write("\n".join(lines))
    print("make-balloons: %d resources" % len(res))


if __name__ == "__main__":
    main()
