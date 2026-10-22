# Changelog

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
