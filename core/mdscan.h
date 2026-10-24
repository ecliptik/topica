/*
 * mdscan.h: Markdown, line by line, for styling a post in place and for the
 * Headings menu. Each character gets a class (MD_ bits); the editor decides
 * how a class looks. Text is Mac Roman with CR (or LF) line ends.
 *
 * It is not a whole CommonMark parser. Emphasis, code spans and links stay
 * within a line (a blog paragraph is one long line that the window wraps);
 * fenced code blocks are recognised but indented ones aren't, and nor are
 * Setext headings. Front matter is the "---" block on the first line.
 */
#ifndef TOPICA_MDSCAN_H
#define TOPICA_MDSCAN_H

enum {
	MD_BOLD = 0x01,
	MD_ITALIC = 0x02,
	MD_LINK = 0x04,             /* a link's text, an image's description */
	MD_URL = 0x08,              /* a link's address, a reference's name */
	MD_CODE = 0x10,             /* code spans and fenced code blocks */
	MD_MARK = 0x20,             /* the markup itself: # * _ ` [ ] > - and so on */
	MD_HEADING = 0x40,
	MD_META = 0x80              /* front matter, Liquid tags, HTML tags */
};

/* Where the scan is, and the blocks that carry from line to line */
typedef struct {
	long pos;                   /* the start of the next line to scan */
	int front;                  /* 0: at the top, 1: in the front matter (3: TOML's), 2: past it */
	char fence;                 /* '`' or '~' inside a fenced code block, else 0 */
	int fenceLen;
	int quoteFace;              /* 1: a block quote's lines are MD_ITALIC (the default) */
} MdState;

/* At the top of the text */
void md_begin(MdState *s);

/* The start of the line after the one that holds pos (n at the end) */
long md_line_after(const char *text, long n, long pos);

/* Moves s forward to the start of the line holding pos and returns it.
   s->pos must be a line start at or before pos. */
long md_seek(MdState *s, const char *text, long n, long pos);

/* Classes for the lines from s->pos up to end, which must be a line start
   or n: out[0] is text[s->pos]'s class, and out needs end - s->pos bytes.
   Line ends get 0. Leaves s at end. */
void md_classify(MdState *s, const char *text, long n, long end, unsigned char *out);

/* What a line is, as far as the blocks go */
enum { MD_LINE_TEXT, MD_LINE_FRONT_DELIM, MD_LINE_FRONT, MD_LINE_FENCE, MD_LINE_CODE };

/* The kind of the line at s->pos, and its end in *le; moves s to the next
   line */
int md_line(MdState *s, const char *text, long n, long *le);

/* For a text line [ls, le): a heading's level (0 if it isn't one) and its
   text [*ts, *te) without the #s */
int md_heading(const char *text, long ls, long le, long *ts, long *te);

/* 1 when the line is a thematic break (---, ***, ___) */
int md_rule(const char *text, long ls, long le);

/* Where a list item's text starts, after its marker (and a task box), or
   ls when the line isn't a list item */
long md_list_marker(const char *text, long ls, long le);

/* 1 when a quote typed at offset at belongs straight ("), not curly: in
   the front matter, a fenced code block or a code span. scratch holds a
   line's classes (cap bytes); a longer line is judged by its block alone. */
int md_straight_at(const char *text, long n, long at, unsigned char *scratch, long cap);

/* A number that changes when the blocks do (the front matter's lines,
   code fences), which restyles the rest of the post */
unsigned long md_blocks(const char *text, long n);

typedef struct {
	long line;                  /* where the heading's line starts */
	long start, len;            /* its text, without the #s */
	int level;                  /* 1 to 6 */
} MdHeading;

/* The headings outside front matter and code, in order: up to max of them
   in out; returns how many there are in all */
int md_headings(const char *text, long n, MdHeading *out, int max);

#endif
