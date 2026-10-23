#!/usr/bin/env python3
"""Compares core/vi.c with vim, case by case: the text after the keys, and
where the cursor ends up. Needs vim; make -C tests vi-oracle.

A case is the text with | at the cursor, and keys in vim's notation:
<Esc> <CR> <BS> <C-R>. Every case ends in normal mode.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(HERE, "vi_run")

CASES = [
    # motions
    ("hello |world", "x"), ("|hello world", "$"), ("hello wor|ld", "0"), ("   ind|ent", "^"),
    ("|one two three", "w"), ("|one two three", "2w"), ("one two |three", "b"), ("|one two", "e"),
    ("|one.two three", "w"), ("|one.two three", "W"), ("|a\n\nb", "w"), ("a\n\n|b", "b"),
    ("|one\ntwo", "w"), ("one|\ntwo", "w"), ("|ab cd", "ee"), ("x |y", "b"),
    ("|l1\nl2\nl3", "G"), ("l1\nl2\n|l3", "gg"), ("|l1\nl2\nl3", "2G"), ("|a\nb\n\nc\nd\n\ne", "}"),
    ("|a\nb\n\nc\nd\n\ne", "2}"), ("a\nb\n\nc\n|d", "{"), ("|hello world", "fo"), ("|hello world", "fo;"),
    ("|hello world", "to"), ("hello |world", "Fe"), ("hello wor|ld", "To"), ("|a.b.c", "f.;,"),
    ("|abc\nde\nfghi", "jj"), ("ab|c\nd\nefg", "jj"), ("|ab\ncd", "j$k"), ("|one", "l"), ("on|e", "l"),
    # operators with motions
    ("|one two three", "dw"), ("one |two three", "dw"), ("one two |three", "dw"), ("|one two three", "d2w"),
    ("|one two three", "2dw"), ("|one two\nthree", "dw"),
    ("one tw|o\nthree", "dw"), ("|hello world", "de"), ("hello |world", "db"), ("hello |world", "d0"),
    ("hello |world", "d$"), ("hello |world", "D"), ("|hello world", "dfo"), ("|hello world", "dto"),
    ("|l1\nl2\nl3", "dj"), ("l1\nl2\n|l3", "dk"), ("l1\n|l2\nl3", "dG"), ("l1\n|l2\nl3", "dgg"),
    ("|a\nb\n\nc", "d}"), ("|hello", "x"), ("hell|o", "x"), ("|hello", "3x"), ("hel|lo", "X"),
    ("|hello", "~"), ("|hello", "3~"), ("|hello", "rX"), ("|hello", "3rX"),
    # lines
    ("|l1\nl2\nl3", "dd"), ("l1\n|l2\nl3", "dd"), ("l1\nl2\n|l3", "dd"), ("|l1\nl2\nl3", "2dd"),
    ("|only", "dd"), ("|l1\nl2", "yyp"), ("|l1\nl2", "yyP"), ("l1\n|l2", "yyp"), ("|l1\nl2\nl3", "ddp"),
    ("|l1\nl2", "Yp"), ("|  l1\nl2", "J"), ("|a\n  b\nc", "3J"), ("|a.\nb", "J"), ("|a\n)b", "J"),
    # change and insert
    ("|one two", "cwONE<Esc>"), ("one |two", "cwX<Esc>"), ("o|ne two", "cwX<Esc>"), ("|one two", "c2wX<Esc>"),
    ("|one two", "ceX<Esc>"), ("one |two", "C!<Esc>"), ("|l1\nl2", "ccX<Esc>"), ("|l1\nl2", "SX<Esc>"),
    ("|hello", "sJ<Esc>"), ("|hello", "3sJ<Esc>"), ("|ab", "iX<Esc>"), ("|ab", "aX<Esc>"),
    ("a|b", "IX<Esc>"), ("|ab", "AX<Esc>"), ("|ab\ncd", "oX<Esc>"), ("ab\n|cd", "OX<Esc>"),
    ("|ab", "3iX<Esc>"), ("|ab", "2aXY<Esc>"), ("|ab", "iXY<BS>Z<Esc>"), ("|ab", "i1<CR>2<Esc>"),
    ("|a", "3oX<Esc>"),
    # text objects
    ("one t|wo three", "diw"), ("one t|wo three", "daw"), ("one two thre|e", "daw"), ("one t|wo three", "ciwX<Esc>"),
    ('say "he|llo" now', 'di"'), ('say "he|llo" now', 'da"'), ('say "he|llo" now', 'ci"X<Esc>'),
    ("f(a, |b) g", "di("), ("f(a, |b) g", "da("), ("f(a, (|b)) g", "di("), ("[x |y]", "di["), ("{a\n|b\n}", "di{"),
    ("|a\nb\n\nc", "dip"), ("|a\nb\n\nc", "dap"), ("a\n|b\n\nc\nd", "yipP"),
    # put, undo, redo, dot
    ("|one two", "ywP"), ("|one two", "yw$p"), ("|one two", "dwu"), ("|one two", "dwu<C-R>"),
    ("|one two", "cwX<Esc>u"), ("|one two three", "cwX<Esc>wu"), ("|a\nb\nc", "dd."), ("|abcdef", "x.."),
    ("|abcdef", "x3."), ("|one two three", "cwX<Esc>w."), ("|a b c", "iX<Esc>w.w."), ("|ab\ncd", "Ax<Esc>j."),
    ("|l1\nl2\nl3\nl4", "dj."), ("|a\nb", "yyjp."), ("|one two", "dwP"), ("|abc", "xp"),
    ("|one two three", "dwwP"), ("|aXbXc", "fXr-;."), ("|l1\nl2", "ddu"),
    # visual
    ("|hello world", "vlld"), ("|hello world", "vey$p"), ("|l1\nl2\nl3", "Vjd"), ("|hello world", "vec!<Esc>"),
    ("|hello", "v$~"), ("|a\nb\nc", "VjJ"), ("hel|lo world", "vbd"), ("|abc", "lvhd"),
    ("|l1\nl2", "Vy$p"), ("|ab", "v<Esc>x"),
    # edges
    ("|", "x"), ("|", "dd"), ("|", "iX<Esc>"), ("|", "oX<Esc>"), ("|", "p"), ("|a", "ddp"),
    ("|ab", "dw"), ("a|b", "dw"), ("a |b", "dw"), ("ab|\ncd", "x"), ("ab\n|\ncd", "x"), ("ab\n|\ncd", "dd"),
    ("|one  two", "cwX<Esc>"), ("one| two", "cwX<Esc>"), ("|ab cd", "5w"), ("|ab cd", "5e"), ("ab |cd", "5b"),
    ("|l1\nl2", "9G"), ("|l1\nl2", "9j"), ("l1\n|l2", "9k"), ("|l1\nl2", "9dd"), ("|l1\nl2", "yy9p"),
    ("l1\n|l2", "yyP"), ("l1\n|l2", "ddP"), ("|l1\nl2", "J"), ("l1\n|l2", "J"), ("|l1", "J"),
    ("|abc", "$a!<Esc>"), ("|abc", "A<BS><BS>X<Esc>"), ("|ab\ncd", "jdk"), ("|ab\ncd\nef", "jdj"),
    ("|ab", "yl3p"), ("|ab", "yl3P"), ("|hello", "fz"), ("|hello", "dfz"), ("he|llo", "d0"),
    ("|a b c d", "dW."), ("|a\nb\nc", "jyykp"), ("|  ab", "0cwX<Esc>"), ("|ab", "vd"), ("a|b", "v$y0P"),
    ("|one\ntwo\nthree", "Vjy"), ("|one\ntwo\nthree", "VjyGp"), ("|abc def", "wvbd"),
    ("|a\n\nb", "}dd"), ("|x", "rX~"),
    # escape cancels
    ("|abc", "d<Esc>x"), ("|abc", "3<Esc>x"),
]


def vim_keys(keys):
    s = keys.replace("\\", "\\\\").replace('"', '\\"')
    for name in ("Esc", "CR", "BS", "C-R"):
        s = s.replace("<%s>" % name, "\\<%s>" % name)
    return s


def our_keys(keys):
    return (keys.replace("<Esc>", "\x1b").replace("<CR>", "\r").replace("<BS>", "\x08")
            .replace("<C-R>", "\x12"))


def offset_of(text, line, col):
    lines = text.split("\n")
    return sum(len(l) + 1 for l in lines[: line - 1]) + col - 1


def run_vim(text, cursor, keys, tmp):
    path = os.path.join(tmp, "t.txt")
    pos = os.path.join(tmp, "pos")
    with open(path, "w") as f:
        f.write(text)
    before = text[:cursor]
    line = before.count("\n") + 1
    col = len(before) - (before.rfind("\n") + 1) + 1
    subprocess.run(["vim", "-u", "NONE", "-N", "-i", "NONE", "-n", "-es",
                    "-c", "set nojoinspaces whichwrap= nofixeol",
                    "-c", "call cursor(%d, %d)" % (line, col),
                    "-c", 'exe "normal! %s"' % vim_keys(keys),
                    "-c", 'call writefile([line(".") . " " . col(".")], "%s")' % pos,
                    "-c", "wq", path], check=False, capture_output=True, timeout=20)
    with open(path) as f:
        out = f.read()
    with open(pos) as f:
        l, c = map(int, f.read().split())
    return out, offset_of(out, l, c)


def run_ours(text, cursor, keys, tmp):
    path = os.path.join(tmp, "o.txt")
    kpath = os.path.join(tmp, "k")
    with open(path, "w") as f:
        f.write(text)
    with open(kpath, "wb") as f:
        f.write(our_keys(keys).encode("latin-1"))
    r = subprocess.run([RUN, path, str(cursor), kpath], capture_output=True, check=True)
    head, mode, out = r.stdout.decode("latin-1").split("\n", 2)
    return out.replace("\r", "\n"), int(head), int(mode)


def show(text, at):
    return (text[:at] + "|" + text[at:]).replace("\n", "\\n")


def main():
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for case, keys in CASES:
            cursor = case.index("|")
            text = case.replace("|", "", 1)
            want, wat = run_vim(text, cursor, keys, tmp)
            if text == "" and want.endswith("\n"):
                want = want[:-1]        # vim writes an empty buffer with a line end
            got, gat, mode = run_ours(text, cursor, keys, tmp)
            if got != want or gat != wat or mode != 0:
                bad += 1
                print("DIFF %-22s %-14s vim %-24s ours %s%s" % (show(text, cursor), keys, show(want, wat),
                      show(got, gat), "" if mode == 0 else " (mode %d)" % mode))
    print("%d cases, %d differ" % (len(CASES), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
