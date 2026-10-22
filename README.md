# Topica

A Markdown writer for blogs, for the classic Macintosh: the Macintosh
512Ke and the Mac Plus on System 6, up to a 68k Mac in color on System 7.
It writes posts for static site generators such as Jekyll and Hugo:
front matter, Markdown, and UTF-8 files a blog can use as they are.

The name is Aristotle's *Topics*, his book on finding things to say: the
*topoi*, the "places" a writer goes for material.

> Topica is new and unfinished (0.2). See [TODO.md](TODO.md) for what's
> coming.

## What it does today

- **Edits posts** in windows that fit a 512×342 screen, in Monaco 9 with
  the text wrapped to the window. Several windows at once.
- **Undo and Redo** many steps (⌘Z, ⌘⇧Z): Typing, Delete, Cut, Paste,
  Clear, Post Info and Insert Image each undo as one step.
- **Post Info** (⌘E) edits the front matter's title, description,
  category, tags, layout and date in a dialog, and rewrites only those
  lines. Keys it doesn't know, comments and list styles stay as they were;
  values that need YAML quotes get them.
- **New Post** (⌘N) starts from a front-matter template; Save suggests a
  `YYYY-MM-DD-title.md` name that fits the Mac's 31 characters.
- **Insert Image** writes a Markdown image placeholder,
  `![description](/assets/images/posts/<category>/<file>)`.
- **UTF-8 in, UTF-8 out.** Posts are UTF-8 with LF line ends on disk and
  Mac Roman in the editor. Characters Mac Roman doesn't have (`ａ`, `→`,
  emoji) show as `&#xFF41;` while you edit and are written back as the same
  UTF-8, byte for byte. A file that isn't UTF-8 opens as Mac Roman.
- **Local disks and AppleShare.** Saving writes a temporary file beside
  the post first, then puts it in place, so a full disk or a dropped
  network leaves the old post whole. Open shows text files and files named
  `.md`, `.markdown` or `.txt` whatever their type, since a file server
  may not know a `.md` file is text.
- **Find and Replace** (⌘F, ⌘G, ⌘=), with whole words and matching case.
- **Preferences:** font and size, Jekyll or Hugo, UTF-8 or Mac Roman, the
  image folder and autosave, in a text file you can read.
- **Crash recovery:** unsaved posts are copied aside when you pause, and
  come back after a crash.
- **The status line** shows the words, the reading time and the line.
- **Mac manners:** the 1992 Human Interface Guidelines' menus and alerts
  (Save, Don't Save, Cancel), ⌘. cancels dialogs, the Clipboard is shared
  with desk accessories, documents open from the Finder (System 7's Apple
  events included), and its own icons.

## Requirements

| Build | For | Memory |
|---|---|---|
| Topica Lite | 512K Macs (the Macintosh 512Ke) | 2 windows, 8 KB of undo per window |
| Topica | the Mac Plus and later, System 6.0.8 to 7.6.1 | 6 windows, 32 KB of undo per window |

Both run on a 68000. Topica opens posts of up to 32,000 characters (it uses
the Toolbox's TextEdit for now).

## Building

Topica is C, cross-compiled with [Retro68](https://github.com/autc04/Retro68).

```sh
scripts/build.sh                 # Topica: build/full/
scripts/build.sh --preset lite   # Topica Lite: build/lite/
make -C tests test               # the portable core's tests, on the host
```

Each build makes a MacBinary `.bin`, an 800K `.dsk` floppy image and a
BinHex `.hqx`. `RETRO68` points at the toolchain. [docs/TESTING.md](docs/TESTING.md)
covers the emulators.

## Layout

- `core/`: portable C89 with no Toolbox calls: UTF-8 and Mac Roman, front
  matter, undo, post names and word counts. Tested on the host.
- `app/`: the Macintosh application. Every edit goes through one call
  (`EdReplace` in `app/editor.h`), with TextEdit behind it today.
- `resources/`: menus, dialogs, alerts and icons (Rez).
- `scripts/`, `tests/`, `tools/`: building, emulators, tests, icons.

## Licence

ISC. See [LICENSE](LICENSE).
