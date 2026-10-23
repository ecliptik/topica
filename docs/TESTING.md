# Testing Topica

## Host tests

```sh
make -C tests test
```

They build `core/` as C89 with `-Werror`, AddressSanitizer and UBSan, and
check the UTF-8 round trip, front matter reading and writing, undo and
redo, post names, the Markdown scanner (with random text, for ASan), and
Publish's addresses, paths, request and reply.

`python3 -m unittest discover -s bridge` tests the bridge against a bare
repository in a temporary folder. `tests/check-mactcp-layout.sh` compares
`app/mactcp.h` with Apple's MacTCP.h (talkip carries a copy).

`make -C tests text-fit` (part of `make test`) wraps every alert and dialog
message the way the Mac does, in Chicago 12 with long sample names and
addresses, and fails when one needs more lines than its box holds. A line
exactly as wide as its box wraps on the Mac too.

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

`SNOW_DISPLAY=:78` runs either emulator (and `tests/snow_session.py`) on
that X display: a private `Xvfb :78 -screen 0 1280x800x24` keeps a
desktop's password prompts and notifications away from the keystrokes.

Publish's cancel: point the Bridge at a listener that accepts and never
answers (a few lines of Python on the host), wait for "Waiting for the
bridge", press Escape or Command-period: the progress window goes, no
alert, and Topica goes on working; then publish to the real bridge.

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
- Publish, in Basilisk II (System 7, MacTCP): run the bridge on this
  machine against a bare repository standing in for the blog (see
  `bridge/README.md`; `TOPICA_BRIDGE_BIND=127.0.0.1` is enough, since
  slirp's 10.0.2.2 is the host's loopback), then Publish "Formatting" to
  `10.0.2.2:8023`: the file on the drafts branch must be the fixture's
  bytes. Also a draft, a wrong key (the bridge's refusal is shown) and the
  bridge stopped (Topica says it couldn't reach it). Once per disk,
  MacTCP's More… must say Obtain Address: Server, then restart. Run a
  Basilisk II built from current macemu source (`BASILISK=… deploy.sh`):
  older builds crash on a TCP close over slirp.
- Opening a UTF-8 post with characters Mac Roman lacks, and saving it.
- Saving to an AppleShare volume, and a full or locked disk.
- Topica Lite on a 512K Mac; the `--talkip` build publishing over
  LocalTalk through a MacIP gateway.
- The `--talkip` build in Snow, without the real Mac: a Snow build with
  LocalTalk over UDP puts the emulated printer port on LToUDP, so an
  AppleTalk router on the LAN (TashRouter, say) carries it to the MacIP
  gateway and the bridge beside it:
  `SNOW=/path/to/snowemu SNOW_ARGS="--serial-bridge-b localtalk"
  scripts/deploy.sh snow --preset lite-talkip --launch`. Click into the
  Key field before typing a key there: a dropped Tab puts it in another,
  visible field.
