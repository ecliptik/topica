# Changelog

## 0.8.0 (unreleased)

The to-do list, before 1.0.

- Option-[ and Option-] type straight quotes in code and the front matter,
  where curly ones would break them; prose keeps the curly ones.
- vi: registers "a to "z for y d c p ("A appends); marks (ma, 'a and `a,
  as jumps and motions); a command line, :w :q :wq :x :q! and :N (ZZ and ZQ
  too), shown in the status line as it's typed; and a block cursor in
  normal mode. :q asks to save a changed post, as Close does.
- Printing from the preview names the job after the post, and a window
  says what's printing and that Command-period stops it.
- Saving swaps the new post in where the volume can (System 7's HFS
  disks), so it keeps its Finder comments, file ID and creation date.
- Faster on a 68000:
  - Formatting a long post styles the lines on the screen first, then the
    rest a slice at a time while you read or type.
  - The preview measures its fonts from the Font Manager's width table
    (44 ticks instead of 96 in Snow), once a session.
  - After an edit, the preview reads the post again from just before the
    edit, and stops once it's back in step after it, instead of from the
    top.

## 0.7.0 (unreleased)

Polish.

- Hugo's TOML front matter (`+++` and `key = value`): Post Info reads and
  writes it, quoting as TOML wants (strings always, dates and booleans
  bare), and puts new keys before the first `[table]`. Formatting,
  spelling and Publish know it too.
- Your own template for New Post: a text file called "Topica Template"
  beside Topica (or with its preferences), any front matter you like;
  `{{date}}` in it becomes today's date. New keys from Post Info go above
  the front matter's trailing blank lines.
- Drag and drop text on System 7.5 and later: press on the selection and
  drag. Within a post it moves; to another post, another application or
  the desktop (a clipping) it copies; text dropped on a post goes where
  the caret shows. Edit says Undo Drag. Not in Topica Lite.
- Stationery pads on System 7: a post made a stationery pad in the
  Finder's Get Info opens as a new untitled post, `{{date}}` filled in.
- Balloon Help on System 7, short and only where it helps: the menu items
  whose names don't say it all, the Headings menu, and the Publish and
  Preferences fields. The text is in `resources/balloons.txt`.
- Word Services on System 7: Edit › Check Spelling With hands the post to
  a spelling checker that speaks it (Excalibur 2.6, for one), launching it
  if need be. The checker's corrections arrive as undoable edits, and one
  Undo takes back all of a check's. The item names the checker once you
  have chosen it; hold Option to choose another. Not in Topica Lite, and
  not shown on System 6.
- Themes, in Post › Theme: Light and Dark on every Mac (Dark on a Plus is
  white on black), and twelve color themes with Color QuickDraw:
  Solarized Light and Dark, TokyoNight Day and Night, Amber CRT, System 7,
  Dracula, Nord, Green Screen, Classic, Monokai and Gruvbox. The post, its
  status line and the preview take the theme; printing doesn't. Topica
  Lite has Light and Dark.
- Undoing (or redoing) back to the text as it was saved leaves the post
  unchanged: closing it doesn't ask to save.
- Publish over MacTCP says "Nothing answered" when the bridge doesn't
  answer, not that it refused: MacTCP reports both the same way.
- Publish can be canceled while it connects or waits for the bridge:
  Command-period, Escape, or the Cancel button in its progress window.
  The windows behind it are redrawn meanwhile.

## 0.6.0 (unreleased)

Publish.

- Post › Publish sends the post to topica-bridge, a small server on your
  LAN (`bridge/`), which commits it to the blog's drafts branch and pushes.
  A classic Mac has no TLS, so it can't push to a git host itself. The
  dialog suggests the post's path (`_posts/2026-10-23-title.md`, or
  `_drafts/title.md` for a draft, or `content/posts/title.md` for Hugo)
  and remembers the bridge's address and key. A blog that keeps posts in
  a folder for each year (`_posts/2026/2026-10-23-title.md`) works too:
  publish one there, and the next post is suggested there. The post goes as UTF-8 with
  LF line ends, whatever the Preferences say for saving. The key shows
  as bullets. When the bridge can't be reached, Topica says whether it
  refused, didn't answer, or the Mac got no address from the gateway.
- Shorter messages, and every one fits its alert: one about a slow bridge
  ran past the bottom on a 512Ke. `make -C tests text-fit` checks them all.
- MacTCP on the Plus and System 7. A `--talkip` build of Topica Lite
  carries [talkip](https://github.com/ecliptik/talkip), TCP/IP over
  LocalTalk, for a 512K Mac that has no room for MacTCP.
- The Edit menu's Preferences was dimmed in 0.5 (Retro68's Rez misreads
  hex that starts `0xB`), and Check Spelling's suggestions could keep rows
  from the word before.

## 0.5.0 (unreleased)

vi keys and spelling.

- vi keys (Edit › Preferences): normal, insert and visual modes, with
  counts, motions, operators (d c y), text objects (iw, ip, i", i( ...),
  x D C s S J r ~ p P, undo and redo, and . to repeat. j and k move by the
  lines on the screen. Escape is the Esc key or ⌘.; jk can be too. The
  status line shows INSERT and VISUAL. A vi change undoes as one step.
- Edit › Check Spelling (⌘;): from the caret, the words the dictionary
  doesn't know, in their line, with suggestions; Replace, Skip, Skip All,
  and Learn, which keeps the word in Topica Words. Code, addresses, Liquid,
  HTML, front matter (but the title and description), acronyms and words
  with digits are left alone.
- Topica Dictionary: about 89,000 American words from SCOWL, in the same
  folder as Topica. Topica Lite reads it a block at a time.

## 0.4.0 (unreleased)

The web preview.

- Post › Show Preview (⌘R): a window that shows the post laid out like its
  web page. The title, date, category and description come from the
  front matter; headings, lists with their numbers and task boxes, quotes
  with a bar, code in a box, tables, rules, footnotes, and images as
  framed boxes with their description and file. Links are underlined and
  listed with their addresses at the end. In color on a color screen.
- The preview follows the front post and catches up a moment after you
  stop typing. Double-click in it to go to that place in the post.
- On a 68000 the preview shows the screen you're looking at first and lays
  out the rest while you read; after an edit, only what changed is laid
  out again.
- File › Print… (⌘P) and Page Setup… print that layout, with page numbers.
  The Finder's Print and System 7's print event work too.
- File › Export HTML… writes the post as a web page of its own, in UTF-8.
- Menus dim and undim as soon as a window comes to the front.

## 0.3.0 (unreleased)

Formatting in place.

- Markdown styled as you write: headings and strong text bold, emphasis in
  italics, code in Monaco, link text underlined and addresses condensed.
  On a color screen, headings, links, code, markup and front matter each
  have a color. A code fence or the front matter's end restyles the rest
  of the post.
- Post › Show Formatting / Hide Formatting, kept in Preferences. It starts
  off on a 68000 (512Ke, Plus, SE), where styled TextEdit takes seconds
  to lay out a long post, and on with a 68020 or better.
- Post › Insert Link (⌘K): the selection becomes the link's text, or its
  address when it looks like one.
- The Headings menu: the post's headings, indented by level, with a check
  mark on the one you're in; choosing one scrolls to it.
- Copy puts plain text on the Clipboard.
- Fixed: posts opened from the Finder on System 6 couldn't be found (0.2).

## 0.2.0 (unreleased)

The Mac's manners.

- Find and Replace (⌘F): match case, whole words, Replace, Replace All
  (one undo step), Find Again (⌘G), Replace & Find Again (⌘=).
- Preferences (Edit menu): font and size, Jekyll or Hugo, UTF-8 with LF or
  Mac Roman with CR, the image folder, the new post's layout, how often to
  autosave. Kept as readable text in Topica Preferences (the Preferences
  folder on System 7, the System Folder before it).
- Crash recovery: unsaved posts are copied to the System Folder's Topica
  Recovery folder when you pause; after a crash Topica reopens them.
- System 7 Apple events: documents dropped on Topica or opened from the
  Finder, and Quit (a Cancel in Save Changes cancels Shut Down too).
- Hugo: a front-matter template with `date` and `draft: true`, Post Info
  edits `categories` and `draft`, file names without a date.
- Every File Manager call uses its own zeroed parameter block.
- Alerts redraw the windows behind them; ⌘. and Escape cancel them.

## 0.1.0 (unreleased)

The first editor, on System 6 and System 7.

- Document windows with TextEdit behind an editor interface, a scroll bar
  and a status line (words, reading time, line).
- Multi-level Undo and Redo (⌘Z, ⌘⇧Z), with typing and Delete runs joined.
- Post Info (⌘E): the front matter's usual fields in a dialog; unknown
  keys, comments, quoting and list styles kept.
- New Post from a front-matter template; Save suggests
  `YYYY-MM-DD-title.md` within the Mac's 31 characters.
- Insert Image: a Markdown image placeholder in the post's category
  folder.
- UTF-8 and LF on disk, Mac Roman and CR in the editor, with a lossless
  round trip for characters Mac Roman lacks.
- Safe saving through a temporary file; Open shows `.md` files of any
  type; Revert; Save Changes before closing or quitting.
- Finder icons for the application and its posts; documents open from the
  Finder.
- Two builds: Topica Lite for 512K Macs and Topica.
- Saving no longer uses Retro68's GetFInfo/SetFInfo glue, which leaves
  ioFDirIndex unset: on a file server it could look up another file in the
  folder and change that file's type and creator.
- With the clock unset (a Mac with no clock battery wakes in 1904), Save
  suggests a name without a date.
