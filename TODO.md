# To do

## Next: the Mac's manners

- [x] System 7 Apple events (open, print, quit)
- [ ] Balloon Help
- [x] Find and Replace (⌘F, ⌘G)
- [x] Recover posts after a crash: an autosave copy reopened at the next launch
- [x] Preferences: font and size, the image folder, the new post's layout,
      Jekyll or Hugo, encoding and line ends, autosave
- [ ] Straight quotes in code and front matter
- [x] About box with the build's commit
- [ ] Swap files in place on volumes that can (FSpExchangeFiles), keeping
      the Finder's comments and dates
- [ ] A smaller application for the 512Ke (newlib's printf is most of it)
- [ ] Undo back to the saved text leaves the post unchanged (no Save
      Changes alert)

## Formatting preview and images

- [x] A Markdown line and span scanner in `core/`
- [x] Headings, emphasis, code and links styled in place (styled TextEdit)
- [x] Insert Link, a Headings menu to jump around the post
- [x] Image placeholders drawn as framed boxes in the preview
- [ ] Faster styling on a 68000: style the visible lines first and the
      rest while idle, or build TextEdit's style runs directly

## Web preview

- [x] A read-only window laid out like the post's page, double-buffered,
      in 1-bit and color
- [x] Print and Export HTML
- [ ] After an edit, read the blocks again from the edit only (a long post
      takes most of a second on a 68000)
- [ ] Name the print job after the post when the preview is in front
- [ ] A "Printing" status dialog that says how to cancel (Command-period)
- [ ] Measure fonts from the Font Manager's width table (1.5 s on a 68000),
      and keep them when the preview closes (reopening measures again)

## vi mode and spelling

- [ ] vi's normal, insert and visual modes over the editor interface;
      Escape is ⌘. or the Esc key on keyboards that have one
- [ ] Check Spelling with a dictionary on disk (front-coded, indexed),
      suggestions, a user dictionary; code, URLs and front matter keys
      skipped
- [ ] Word Services on System 7

## Publish

- [ ] Publish a post over the network to a small bridge that commits it to
      the blog's repository (MacTCP has no TLS), as a pull request or a
      branch
- [ ] TCP/IP over LocalTalk for Macs without MacTCP (the 512Ke)
- [ ] Drafts, and a Hugo profile (TOML front matter, `draft: true`)

## Later

- [ ] A custom text engine instead of TextEdit: no 32,000-character limit,
      inline styling, faster vi motions
- [ ] Stationery, drag and drop on 7.5, a quick-note desk accessory
- [ ] Release script: `.dsk`, `.hqx` and `.sit` for both builds
