# Known limits

What Topica 1.0 doesn't do, or does only so far. Most come from the Macs
it runs on: a 68000 at 8 MHz, 512K or 1 MB of memory, 512×342 pixels in
black and white, and networks without encryption.

## Posts

- **32,000 characters at most.** Topica edits with the Toolbox's TextEdit,
  which stops near 32K. A longer post doesn't open (Topica says how long it
  may be), and an edit that would make it longer is refused. Characters the
  Mac can't show (below) count as their codes, `&#x1F600;` as nine.
- **Mac Roman in the editor.** Posts are UTF-8 on disk, but a classic Mac
  shows Mac Roman. Any other character appears as its code, `&#xFF41;`,
  while you write, and goes back to the same UTF-8 bytes when you save. The
  other side of that: typing such a code for a character Mac Roman lacks
  saves the character itself. A code for one Mac Roman has (`&#x2014;`)
  stays as you typed it.
- **31-character file names**, the Mac's own limit. Save suggests a
  `YYYY-MM-DD-title.md` name cut to fit; Publish can still give the post
  a longer name on the blog.
- **The Mac's clock decides "today".** Post Info's date, New Post's
  `{{date}}`, Save's suggested name and Publish's path all come from it.
  A Mac whose clock battery is flat may think it's 1904 or 1926: put the
  date in the front matter yourself and Publish uses that.
- **Front matter:** Post Info edits the title, description, category,
  tags, layout and date. It reads YAML's plain and quoted values and lists,
  and TOML's strings, one-line arrays, booleans, numbers and dates. Other
  keys, comments and values it can't read (a `|` block, a nested map, a
  multi-line array) stay exactly as they are; Post Info leaves them alone.

## Writing

- **Formatting as you write is off on a 68000** (the Macintosh 512Ke,
  Plus, SE and Classic), where styling a long post takes seconds. Post ›
  Show Formatting turns it on; the screen's lines come first and the rest
  follows while you pause.
- **Markdown, read line by line.** The formatting, the Headings menu and
  the preview read emphasis, code spans and links within a line (a blog's
  paragraph is one line), fenced code blocks, lists, quotes, tables and
  footnotes. They don't read indented code blocks or Setext (underlined)
  headings, and HTML and Liquid are shown, dimmed, not run.
- **The Headings menu** lists the first 40 headings.
- **Undo** keeps 32K of changes for each post (8K in Topica Lite); the
  oldest steps go first, and a change larger than that can't be undone.
- **vi keys** cover what's in the Read Me: modes, motions, operators, text
  objects, counts, `.`, registers, marks, and `:w` `:q` `:wq` `:x` `:q!`
  `:N`. There's no ex beyond that (`:s`, `:g`, `:set`) and no macros
  (`q`). Most early Mac keyboards have no Escape key: Command-period is
  Escape, or turn on "jk leaves insert mode". Yanks hold 32,000 characters
  (4,096 in Topica Lite).
- **Find and Replace** matches text, ignoring case and accents if you like,
  or whole words. There are no regular expressions.
- **Spelling** is American English, about 89,000 words from SCOWL, plus the
  words you teach it. On a 68000 a word's suggestions take a second or
  more, and several on a 512Ke with the dictionary on AppleShare.

## The preview, printing and HTML

- The preview is laid out like a post's page, not like your blog's theme:
  Geneva and Times, images as framed boxes with their description, links
  listed at the end.
- **Export HTML** writes the Markdown as Topica reads it (above), with its
  own simple style. Liquid tags can't run outside the blog and show as text.
- **Printing** prints that layout, in black and white whatever the theme.

## Publish

- **It needs a bridge** ([bridge/](../bridge/README.md)) on your network,
  because MacTCP has no TLS and can't talk to GitHub. The bridge commits the
  post to the blog's drafts branch; merging it, and building the site, are
  yours to do.
- **Nothing on the way is encrypted.** The post and the bridge's key cross
  your network as plain HTTP. Keep the bridge on a network you trust, and
  give it an allow list of the Macs that may publish (its README says how).
- **Only the post goes.** Images aren't sent: put them in the blog
  yourself, where the post's image placeholders say.
- **512K for a post** by default (the bridge's `TOPICA_BRIDGE_MAX_BYTES`),
  far more than TextEdit holds.
- **Over MacTCP**, or, in the `--talkip` build of Topica Lite, over
  LocalTalk through a MacIP gateway. Open Transport's MacTCP compatibility
  hasn't been tried.
- **On a 68000 the network is slow to give up:** a bridge that doesn't
  answer takes 20 seconds to report, and a name that doesn't exist up to
  45 (MacTCP retries its name servers). Publishing goes on in the
  background meanwhile, and Post › Stop Publishing ends it.

## Topica Lite

The build for 512K Macs leaves things out to fit:

- two windows at a time, 8K of undo each, and the spelling dictionary
  read from disk a block at a time;
- Light and Dark only (no color themes: a 512K Mac has no color);
- no Word Services or drag and drop, which need System 7.

## Systems and Macs

- **Tested on:** a Macintosh 512Ke (System 6.0.8, Topica Lite with talkip
  over LocalTalk, posts on an AppleShare server) and a Mac Plus (System
  6.0.8 with MultiFinder, MacTCP over Ethernet), both real; Snow (a Mac
  Plus and a 512Ke on System 6.0.8) and Basilisk II (a IIci on System
  7.6.1 in 256 colors), both emulators.
- **Not yet tried on:** a real color Mac, System 7 on real hardware, Mac
  OS 8, a PowerPC Mac, or a large screen. Topica is 68000 code and should
  run on all of them; reports are welcome.
- **System 7 only:** Apple events (opening posts from the Finder works on
  System 6 too), Balloon Help, stationery, Word Services (tried with
  Excalibur 2.6). Drag and drop needs System 7.5's Drag Manager.
- **Basilisk II:** MacTCP crashes (an error of type 3) a few seconds into a
  connection that nothing answers, and stays broken until the emulated Mac
  restarts. Other MacTCP programs do it too, and a real Mac doesn't: it's
  the emulator. Publish to a bridge that's running.
