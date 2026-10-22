# Changelog

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
