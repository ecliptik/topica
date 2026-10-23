/*
 * mdpage.h: a post as the page a blog would make of it, for the preview,
 * printing and Export HTML. The post is a run of blocks (paragraphs,
 * headings, list items, code, quotes are a block's depth), each with a
 * display text: the Markdown's markup taken out, line ends joined, styles
 * per character. The page starts with a head made from the front matter:
 * the title, the date and category, the description.
 *
 * Built on mdscan, so it shares its limits: inline spans stay within a
 * line, no Setext headings or indented code. Text is Mac Roman, CR or LF.
 */
#ifndef TOPICA_MDPAGE_H
#define TOPICA_MDPAGE_H

#include "mdscan.h"

enum {
	MP_END = 0,
	MP_TITLE,                   /* the head: front matter title */
	MP_BYLINE,                  /* date and category */
	MP_LEAD,                    /* description */
	MP_PARA,
	MP_HEADING,                 /* level 1 to 6 */
	MP_ITEM,                    /* a list item's first paragraph */
	MP_CODE,                    /* a fenced code block */
	MP_TABLE,                   /* lines that start with |, shown as they are */
	MP_HTML,                    /* an HTML or Liquid block, shown dimmed */
	MP_RULE,
	MP_IMAGE                    /* a paragraph that is one image */
};

typedef struct {
	long start, end;            /* the source lines (the head: 0, 0) */
	long at;                    /* where the text starts: after an item's marker, an image's ! */
	unsigned char kind;
	unsigned char level;        /* a heading's level */
	unsigned char depth;        /* list nesting: an item's, or the item a paragraph is in */
	unsigned char quote;        /* how many quotes it's in */
	long number;                /* an ordered item's number, 0 for a bullet */
	char task;                  /* a task item: ' ' or 'x'; else 0 */
} MpBlock;

/* Display text styles */
enum {
	MS_BOLD = 0x01,
	MS_ITALIC = 0x02,
	MS_CODE = 0x04,
	MS_LINK = 0x08,
	MS_META = 0x10,             /* Liquid, shown dimmed */
	MS_NOTE = 0x20              /* a footnote's number */
};

/* A forced line break in display text */
#define MP_BREAK '\r'

typedef struct {
	const char *text;
	long n;
	MdState s;                  /* the next line to read */
	int head;                   /* the next head block to try */
	int depth;                  /* list nesting */
	int indents[8];             /* each open list's marker indent */
	int listQuote;              /* the quote depth the open lists are in */
	int blank;                  /* the last line was blank */
} MpScan;

void mp_begin(MpScan *p, const char *text, long n);

/* The next block into b; 0 (and b->kind MP_END) at the end */
int mp_next(MpScan *p, MpBlock *b);

/* A block's display text, up to cap characters into out with a style per
   character in sty (either may be NULL to measure). cls is scratch for
   mdscan, b->end - b->start bytes. Returns the length. */
long mp_text(const char *text, long n, const MpBlock *b, char *out, unsigned char *sty, long cap,
		unsigned char *cls);

/* For a line of a paragraph, heading or item: where its text starts and
   (in *end) ends, without markers, indent and trailing spaces; *hard when
   it ends in a line break. cls are the line's classes, cls[0] for ls. */
long mp_line_text(const char *text, long n, const MpBlock *b, long ls, const unsigned char *cls,
		long *end, int *hard);

/* A link found while making display text: where it starts in the source */
typedef void (*MpLinkFn)(void *ctx, long at);

/* Like mp_text, telling fn about each link (and image) in the block */
long mp_text_links(const char *text, long n, const MpBlock *b, char *out, unsigned char *sty, long cap,
		unsigned char *cls, MpLinkFn fn, void *ctx);

/* A link at at (its '[', '!' or '<'): its address into url (NUL-ended, cut
   to cap), a reference's looked up in the post. Returns the address's
   length, 0 if it has none. */
long mp_link_url(const char *text, long n, long at, char *url, long cap);

/* The link's text, as display text (markup taken out) */
long mp_link_text(const char *text, long n, long at, char *out, long cap);

/* Fonts for layout: a block's kind and a character's style pick one */
enum {
	MF_BODY, MF_BOLD, MF_ITALIC, MF_BOLD_ITALIC, MF_CODE,
	MF_H1, MF_H2, MF_H3, MF_TITLE, MF_SMALL,
	MF_COUNT
};

int mp_font(const MpBlock *b, int style);

#endif
