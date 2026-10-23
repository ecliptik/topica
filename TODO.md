# To do

## Next: the Mac's manners

- [x] System 7 Apple events (open, print, quit)
- [x] Balloon Help: a few short balloons where an item's name doesn't say
      enough (resources/balloons.txt)
- [x] Find and Replace (⌘F, ⌘G)
- [x] Recover posts after a crash: an autosave copy reopened at the next launch
- [x] Preferences: font and size, the image folder, the new post's layout,
      Jekyll or Hugo, encoding and line ends, autosave
- [ ] Straight quotes in code and front matter
- [x] About box with the build's commit
- [ ] Swap files in place on volumes that can (FSpExchangeFiles), keeping
      the Finder's comments and dates
- [ ] A smaller application for the 512Ke (newlib's printf is most of it;
      sscanf alone would add 20K)
- [x] Undo back to the saved text leaves the post unchanged (no Save
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

- [x] vi's normal, insert and visual modes over the editor interface;
      Escape is ⌘. or the Esc key on keyboards that have one
- [x] Check Spelling with a dictionary on disk (front-coded, indexed),
      suggestions, a user dictionary; code, URLs and front matter keys
      skipped
- [x] Word Services on System 7 (Check Spelling With; Excalibur 2.6)
- [ ] Faster suggestions on a 68000 (a second for most words)
- [ ] vi: :w and :q, registers, marks, a block cursor in normal mode

## Publish

- [x] Publish a post over the network to a small bridge that commits it to
      the blog's drafts branch (MacTCP has no TLS)
- [x] TCP/IP over LocalTalk for Macs without MacTCP (the 512Ke): the
      `--talkip` build
- [x] Drafts (`_drafts/`), and Hugo's `content/posts/`
- [x] Hugo's TOML front matter (`+++`)
- [x] Cancel Publish while it waits: ⌘., Escape or Cancel (asynchronous
      MacTCP calls; talkip's open and a receive that asks TCPStatus)
- [ ] Publish in the background, while you go on writing
- [ ] **Before 1.0:** MacTCP in Basilisk II crashes (an error of type 3) a
      few seconds into a connection nothing answers (an unroutable
      address). Flynn, another MacTCP application, crashes the same way
      there, so it looks like the emulator's; confirm on a real Mac with
      MacTCP (the Plus)
- [ ] Publish the post's images too
- [ ] A bridge name instead of an address (MacTCP's resolver; talkip's DNS)
- [x] Hide the key in Publish as it's typed (bullets, like a password
      field)
- [x] Say why a connection failed: no address from the MacIP gateway, a
      refusal, or nothing answering (a firewall in between)

## Later

- [ ] A custom text engine instead of TextEdit: no 32,000-character limit,
      inline styling, faster vi motions
- [x] A template file for New Post ("Topica Template", `{{date}}`)
- [x] Stationery pads (System 7): open as an untitled post, `{{date}}`
      filled in
- [x] Drag and drop text on System 7.5 (moves within a post, copies out)
- [ ] A quick-note desk accessory
- [x] Release script: `.dsk`, `.hqx` and `.sit` for each build
