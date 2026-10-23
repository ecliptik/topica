#!/usr/bin/env python3
"""check-text-fit.py: does every message Topica shows fit its box?

Alerts and a few dialog lines are StaticText items that TextEdit wraps into
a fixed rectangle; what doesn't fit is cut off. This finds each message in
app/*.c, fills in its %s/%ld/%d with long but likely values, wraps it the
way TextEdit does in Chicago 12 (the system font: 16 px lines) and fails
when it needs more lines than its box holds.

    python3 tests/check-text-fit.py         (make -C tests text-fit)
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

# Chicago 12's character widths (its FONT resource), Mac Roman 32..216
WIDTHS = [
    4, 6, 7, 10, 7, 11, 10, 3, 5, 5, 7, 7, 4, 7, 4, 7, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 4, 4, 6, 8, 6, 8, 11, 8, 8, 8, 8, 7, 7, 8, 8, 6, 7, 9, 7, 12, 9, 8,
    8, 8, 8, 7, 6, 8, 8, 12, 8, 8, 8, 5, 7, 5, 8, 8, 6, 8, 8, 7, 8, 8, 6, 8,
    8, 4, 6, 8, 4, 12, 8, 8, 8, 8, 6, 7, 6, 8, 8, 12, 8, 8, 8, 5, 5, 5, 8, 8,
    8, 8, 8, 7, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 6, 6, 6, 6, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 5, 6, 7, 9, 7, 7, 9, 9, 10, 10, 11, 5, 6, 9, 11, 9,
    14, 7, 6, 6, 10, 10, 8, 9, 10, 11, 6, 7, 7, 10, 12, 8, 8, 6, 7, 12, 6, 10, 9, 9,
    9, 14, 8, 8, 8, 8, 11, 12, 6, 10, 7, 7, 4, 4, 7, 9, 8,
]
LINE = 16

# The boxes (resources/topica.r.in): width, lines
ALERT = (274, 4)            # DITL 130, ^0 of ShowNote and ErrorAlert
PROGRESS = (348, 2)         # DITL 147, Publish's progress
ABOUT = (372, 2)            # DITL 146 item 12, Publish's first line

# Long but likely values for the arguments, by the name the code passes
SAMPLES = {
    "name": "A Post With a Long Long Name.md",          # an HFS name: 31 characters
    "bridge": "203.0.113.200:65535",                 # TEST-NET-3: as wide as a long LAN address
    "path": "_posts/2026/2026-10-23-a-post-with-a-long-name.md",
    "title": "A Post With a Long Long Name",
    "EdMaxLength()": "32000",
    "err": "-32768",
    "(int) err": "-32768",
    "(bodyLen + 1023) / 1024": "512",
    "said": "A post goes in _posts, _drafts, content/posts.",
    "folder": "/assets/images/posts/2026/",                # Insert Image's "In ^0"
}
# Reasons ErrorAlert adds after what couldn't be done (app/util.c Reason): the longest
LONGEST_REASON = "You don\xd5t have permission to change it."


def width(s):
    return sum(WIDTHS[ord(c) - 32] if 32 <= ord(c) <= 216 else 8 for c in s)


def wrap(text, box):
    """Lines as TextEdit breaks them: after spaces, a word too long for a line broken where it must."""
    lines, line = [], ""
    for word in re.findall(r"\S+\s*", text):
        if width((line + word).rstrip()) < box:     # a line as wide as its box wraps (seen on a 512Ke)
            line += word
            continue
        if line:
            lines.append(line.rstrip())
        line = ""
        while width(word.rstrip()) >= box:
            n = len(word)
            while width(word[:n]) >= box:
                n -= 1
            lines.append(word[:n])
            word = word[n:]
        line = word
    if line.strip():
        lines.append(line.rstrip())
    return lines


def literal(s):
    """A C string literal's contents (\\xNN, \\r, \\" and the like) as Mac Roman characters."""
    out, i = "", 0
    while i < len(s):
        if s[i] == "\\":
            c = s[i + 1]
            if c == "x":
                m = re.match(r"[0-9A-Fa-f]{1,2}", s[i + 2:])
                out += chr(int(m.group(0), 16))
                i += 2 + len(m.group(0))
                continue
            out += {"r": "\r", "n": "\n", "t": "\t"}.get(c, c)
            i += 2
            continue
        out += s[i]
        i += 1
    return out


def calls(src):
    """(function, the whole argument text) for the calls that put text in a box."""
    pat = re.compile(r"\b(ShowNote|ErrorAlert|Progress|strcpy|sprintf)\s*\(")
    for m in pat.finditer(src):
        depth, i = 1, m.end()
        while depth and i < len(src):
            if src[i] == '"':
                i += 1
                while src[i] != '"':
                    i += 2 if src[i] == "\\" else 1
            elif src[i] == "(":
                depth += 1
            elif src[i] == ")":
                depth -= 1
            i += 1
        yield m.group(1), src[m.end():i - 1], src.count("\n", 0, m.start()) + 1


def messages(args):
    """The messages a call can show: each end of a ternary, the format filled in."""
    lits = list(re.finditer(r'"((?:[^"\\]|\\.)*)"', args))
    if not lits:
        return []
    texts, cur = [], ""
    for i, m in enumerate(lits):
        if i and ":" in args[lits[i - 1].end():m.start()]:     # a ? "x" : "y" shows one or the other
            texts.append(cur)
            cur = ""
        cur += literal(m.group(1))
    texts.append(cur)
    values = [a.strip() for a in args[lits[-1].end():].split(",")[1:] if a.strip()]
    out = []
    for t in texts:
        vals = iter(values)

        def fill(m):
            v = next(vals, "")
            return SAMPLES.get(v, SAMPLES["name"] if m.group(0) == "%s" else "32000")
        out.append(re.sub(r"%l?[sd]|%u", fill, t))
    return out


def resources():
    """The alerts whose text is in topica.r.in with ^0 (a window's name) and ^1 in it: (id, text, lines)."""
    src = open(os.path.join(ROOT, "resources", "topica.r.in"), encoding="latin-1").read()
    for m in re.finditer(r"resource 'DITL' \((\d+)[^{]*\{(.*?)\n\};", src, re.S):
        for item in re.finditer(r"\{ (\d+), (\d+), (\d+), (\d+) \}, StaticText \{ disabled,\s*((?:\"(?:[^\"\\\\]|\\\\.)*\"\s*)+)\}",
                                m.group(2)):
            text = "".join(literal(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', item.group(5)))
            if "^" not in text or text == "^0":
                continue
            top, left, bottom, right = (int(item.group(i)) for i in range(1, 5))
            text = text.replace("^0", SAMPLES["name"] if "\xd2^0" in text else SAMPLES["folder"])
            text = text.replace("^1", "quitting")
            yield m.group(1), text, (right - left, (bottom - top) // LINE)


def main():
    bad = 0
    for path in sorted(glob.glob(os.path.join(ROOT, "app", "*.c"))):
        src = open(path, encoding="latin-1").read()
        for fn, args, line in calls(src):
            target = args.split(",")[0].strip()
            if fn in ("strcpy", "sprintf") and target not in ("out", "what", "msg", "line", "about"):
                continue
            for text in messages(args):
                box = ALERT
                if fn == "Progress" or (fn == "sprintf" and target == "line" and text.endswith("\xc9")):
                    box = PROGRESS
                elif target == "about":
                    box = ABOUT
                elif fn == "ErrorAlert" or (target == "what" and "couldn\xd5t" in text):
                    text += " " + LONGEST_REASON
                lines = wrap(text, box[0])
                if len(lines) > box[1]:
                    bad += 1
                    where = os.path.relpath(path, ROOT)
                    print(f"{where}:{line}: {len(lines)} lines, the box holds {box[1]}:")
                    for l in lines:
                        print("    " + l.encode("latin-1").decode("mac_roman"))
    for rid, text, box in resources():
        lines = wrap(text, box[0])
        if len(lines) > box[1]:
            bad += 1
            print(f"resources/topica.r.in DITL {rid}: {len(lines)} lines, the box holds {box[1]}:")
            for l in lines:
                print("    " + l.encode("latin-1").decode("mac_roman"))
    if bad:
        print(f"{bad} message(s) don't fit")
        return 1
    print("every message fits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
