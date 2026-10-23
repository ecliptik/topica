# Testing Topica

## Host tests

```sh
make -C tests test
```

They build `core/` as C89 with `-Werror`, AddressSanitizer and UBSan, and
check the UTF-8 round trip, front matter reading and writing, undo and
redo, post names, and the Markdown scanner (with random text, for ASan).

`make -C tests vi-oracle` runs the vi cases through vim itself and through
`core/vi.c`, and compares the text and the cursor (it needs vim). The host
tests keep vim's answers. `tests/spell_run "build/Topica Dictionary"
post.md` checks a post's spelling on the host, as Topica Lite reads the
dictionary.

## Emulators

The scripts expect a classic Mac emulator set-up in `~/emulators` (set
`EMU` for another place) with:

- [Snow](https://github.com/twvd/snow) at `snow/snowemu`, a Mac Plus ROM in
  `roms/`, and a System 6.0.8 disk at `disks/system6.img`;
- Basilisk II on the `PATH`, its prefs in `basilisk/basilisk_ii_prefs`, a
  System 7 disk at `disks/system7.hda`, and a shared folder at `unix/`;
- `scripts/snow_automation.py`, an XTEST helper for driving Snow.

```sh
scripts/setup-emulators.sh            # once: Topica's own copies of the disks
scripts/build.sh
scripts/deploy.sh snow --launch       # System 6 on a Mac Plus
tests/snow_session.py dclick:476,52 shot:desktop
scripts/deploy.sh basilisk --launch   # System 7 in 256 colors
```

`deploy.sh` stops Snow before it changes Snow's disk (the disk is
memory-mapped while Snow runs). Never run Snow and Basilisk II at once, and
shut Basilisk II down from the Special menu rather than killing it.

`deploy.sh` also puts `tests/fixtures/formatting.md` on the disk as
"Formatting", a post with every kind of Markdown Topica styles, and two
copies of `tests/fixtures/round-trip.md`: "Round Trip", typed as another application's text file, and
"untyped.md", with no type, as a file server might leave it. To check the
round trip, open "Round Trip", Save As another name, quit, and compare the
copy with the fixture: they must be the same bytes.

A `--debug` build (`scripts/build.sh --debug`) appends to a "Topica Log"
file beside the application.

## What to check by hand

- New Post, Post Info, typing, Undo and Redo, Save, Revert, Close and Quit
  with unsaved changes.
- Formatting: open "Formatting", type emphasis, open and close a code
  fence (the rest of the post restyles), Undo, Hide and Show Formatting,
  a font change in Preferences; colors on System 7.
- Preview: Show Preview on "Formatting" (every kind of block), on a long
  post (the screen first, the rest while idle: the scroll bar settles),
  typing with the preview open (it catches up after a pause), a
  double-click going back to the post, resizing, colors on System 7.
- Export HTML of "Formatting": the file matches what the host tests'
  `mh_page` makes.
- Print and Page Setup: on System 7 the job reaches PrintMonitor. In Snow
  the ImageWriter driver prints the page, then waits for good in
  `PrPicFile` for a printer that isn't there: stop Snow.
- vi keys: turn them on in Preferences; motions, dd, cw and typing,
  ⌘. (or Esc), u undoing a change in one step, ., V and d, p; the status
  line's INSERT.
- Check Spelling on "Spelling": the planted slips and nothing else,
  Replace, Skip All, Learn (Topica Words), with the dictionary in memory
  (Topica) and read a block at a time (Topica Lite).
- Opening a UTF-8 post with characters Mac Roman lacks, and saving it.
- Saving to an AppleShare volume, and a full or locked disk.
- Topica Lite on a 512K Mac.
