#!/usr/bin/env python3
"""Builds Topica's spelling dictionary from SCOWL's word lists.

    tools/mkdict.py SCOWL_FINAL_DIR OUT [--size 60] [--common 10] [--block 1024]

SCOWL (http://wordlist.aspell.net/) is Kevin Atkinson's; its licence lets
the lists and what's made from them be copied with its notice (see
docs/Dictionary Licence). scripts/build.sh fetches the release this was
made with.

The file is "TPSP", a version (2 bytes), 2 spare, the common words'
section's length (4) and the main section's (4), then the two sections:
the commonest words (kept in memory), and all of them. Version 2 adds the
prefix table after them (core/spell.h's SP_PREFIXES bytes): a bit for every
two- and three-character start a word has, so that suggestions look up only
what can be there. A section (core/dict.h reads one):
  header   "TPDC", version (2 bytes), blocks (2), words (4), index length (4)
  index    per block: offset (4, from the end of the index), length (2),
           its first word: length (1) and bytes
  blocks   front-coded words: bytes shared with the word before (1), the
           rest's length (1), the rest
Words are Mac Roman, sorted by byte. A possessive ('s) isn't listed when
the word is: the reader takes it off.
"""
import argparse
import glob
import os
import struct
import sys

KINDS = ("words", "upper", "proper-names", "contractions", "abbreviations")
SPELLINGS = ("english", "american", "special-hacker")


def mac_roman(word):
    try:
        return word.encode("mac_roman")
    except UnicodeEncodeError:
        return None


def load(final, size):
    words = set()
    for path in glob.glob(os.path.join(final, "*")):
        name = os.path.basename(path)
        base, _, level = name.rpartition(".")
        if not level.isdigit() or int(level) > size:
            continue
        spelling, _, kind = base.partition("-")
        if spelling not in SPELLINGS or (kind not in KINDS and spelling != "special-hacker"):
            continue
        with open(path, encoding="latin-1") as f:
            for line in f:
                w = line.strip()
                if w and len(w) < 32:
                    b = mac_roman(w)
                    if b is not None:
                        words.add(b)
    # possessives the reader can find from the word itself
    return sorted(w for w in words if not (w.endswith(b"'s") and w[:-2] in words))


def build(words, block):
    blocks, index, cur, first, prev, count = [], [], bytearray(), None, b"", 0
    for w in words:
        if first is None:
            first, prev = w, b""
        shared = 0
        while shared < min(len(w), len(prev), 255) and w[shared] == prev[shared]:
            shared += 1
        cur += bytes([shared, len(w) - shared]) + w[shared:]
        prev = w
        count += 1
        if len(cur) >= block:
            blocks.append((first, bytes(cur)))
            cur, first, prev = bytearray(), None, b""
    if cur:
        blocks.append((first, bytes(cur)))
    offset = 0
    for first, data in blocks:
        index.append(struct.pack(">IHB", offset, len(data), len(first)) + first)
        offset += len(data)
    index = b"".join(index)
    head = b"TPDC" + struct.pack(">HHII", 1, len(blocks), count, len(index))
    return head + index + b"".join(d for _, d in blocks), len(blocks), count, len(index)


def slot(b):
    """A character's place in the prefix table: a-z (either case), ', the rest"""
    if 65 <= b <= 90:
        b += 32
    if 97 <= b <= 122:
        return b - 97
    return 26 if b == 39 else 27


def prefixes(words):
    """28 * 28 bits for two-character starts, then 28 ** 3 for three"""
    two, three = bytearray(98), bytearray(2744)
    for w in words:
        if len(w) >= 2:
            k = slot(w[0]) * 28 + slot(w[1])
            two[k >> 3] |= 0x80 >> (k & 7)
        if len(w) >= 3:
            k = (slot(w[0]) * 28 + slot(w[1])) * 28 + slot(w[2])
            three[k >> 3] |= 0x80 >> (k & 7)
    return bytes(two + three)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("final")
    ap.add_argument("out")
    ap.add_argument("--size", type=int, default=60)
    ap.add_argument("--common", type=int, default=10)
    ap.add_argument("--block", type=int, default=1024)
    a = ap.parse_args()
    words = load(a.final, a.size)
    common = load(a.final, a.common)
    if not words or not common:
        sys.exit("mkdict: no words in %s" % a.final)
    small, _, ccount, _ = build(common, a.block)
    data, nblocks, count, ilen = build(words, a.block)
    with open(a.out, "wb") as f:
        f.write(b"TPSP" + struct.pack(">HHII", 2, 0, len(small), len(data)) + small + data + prefixes(words))
    print("mkdict: %d words (%d common), %d blocks, index %d bytes, %d bytes in all"
          % (count, ccount, nblocks, ilen, 16 + len(small) + len(data)))


if __name__ == "__main__":
    main()
