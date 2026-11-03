/*
 * page.c: a post laid out like its web page (see page.h).
 *
 * Layout keeps only each block's place and height; drawing makes a block's
 * display text and breaks its lines again, which is quick for the few
 * blocks on a screen or a printed page. A block's height is a guess from
 * its length until it has been laid out exactly.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Memory.h>
#include <Events.h>
#include <string.h>

#include "../core/layout.h"
#include "../core/mdpage.h"
#include "images.h"
#include "page.h"
#include "theme.h"
#include "util.h"

enum { kTimes = 20 };

enum {
	kQuoteIndent = 14,          /* each quote's indent, its bar inside it */
	kListIndent = 20,           /* each list's indent, the marker inside it */
	kPad = 4,                   /* inside a code box */
	kImageHeight = 40,
	kRuleHeight = 9,
	kBottom = 16                /* below the last block */
};

/* A block, or one of the links listed at the end */
enum { kBody, kLinksHead, kLinkItem };

typedef struct {
	MpBlock b;
	long y;                     /* its top, below its gap */
	short h;                    /* its height */
	short gap;                  /* the space above it */
	char what;                  /* kBody, kLinksHead or kLinkItem */
	Boolean exact;              /* h is laid out, not guessed */
} PgBlock;

/* The block scanner as it was before block `block`: kept every 16 blocks,
   so that a layout after an edit starts at the last one before it, and
   stops once the scan is back in step with the old one after it */
typedef struct {
	long block;
	MpScan scan;
} PgMark;

enum { kMarkEvery = 16 };

struct Page {
	LayFonts f;
	short family[MF_COUNT], size[MF_COUNT];
	Style face[MF_COUNT];
	short avg[MF_COUNT];        /* a typical character's width */
	PgBlock **blocks;
	long count;
	long body;                  /* the post's own blocks, before the link list */
	long **links;               /* where each link starts, in the post's order */
	long nlinks;
	Boolean linksDue;           /* the link list needs making again */
	long **found;               /* the links in the block being laid out */
	long nfound;
	char *buf;                  /* a block's display text */
	unsigned char *sty, *cls;
	long cap;
	long height;
	short width;
	Boolean measured;           /* the fonts, in the port it was first laid out in */
	PgMark **marks;
	long nmarks;
};

Page *PageNew(void)
{
	Page *pg = (Page *) NewPtrClear(sizeof(Page));

	if (pg == NULL)
		return NULL;
	pg->blocks = (PgBlock **) NewHandle(0);
	pg->links = (long **) NewHandle(0);
	pg->found = (long **) NewHandle(0);
	pg->marks = (PgMark **) NewHandle(0);
	if (pg->blocks == NULL || pg->links == NULL || pg->found == NULL || pg->marks == NULL) {
		PageDispose(pg);
		return NULL;
	}
	return pg;
}

static void FreeBuffers(Page *pg)
{
	if (pg->buf != NULL)
		DisposePtr(pg->buf);
	if (pg->sty != NULL)
		DisposePtr((Ptr) pg->sty);
	if (pg->cls != NULL)
		DisposePtr((Ptr) pg->cls);
	pg->buf = NULL;
	pg->sty = pg->cls = NULL;
	pg->cap = 0;
}

void PageDispose(Page *pg)
{
	if (pg == NULL)
		return;
	FreeBuffers(pg);
	if (pg->blocks != NULL)
		DisposeHandle((Handle) pg->blocks);
	if (pg->links != NULL)
		DisposeHandle((Handle) pg->links);
	if (pg->found != NULL)
		DisposeHandle((Handle) pg->found);
	if (pg->marks != NULL)
		DisposeHandle((Handle) pg->marks);
	DisposePtr((Ptr) pg);
}

/* Room for len characters of display text */
static Boolean Room(Page *pg, long len)
{
	if (len < 256)
		len = 256;
	if (len <= pg->cap)
		return true;
	FreeBuffers(pg);
	pg->buf = NewPtr(len);
	pg->sty = (unsigned char *) NewPtr(len);
	pg->cls = (unsigned char *) NewPtr(len);
	if (pg->buf == NULL || pg->sty == NULL || pg->cls == NULL) {
		FreeBuffers(pg);
		return false;
	}
	pg->cap = len;
	return true;
}

/* A handle of count items of size bytes, made to hold at least want */
static Boolean Grow(Handle h, long want, long size)
{
	long have = GetHandleSize(h) / size;

	if (want <= have)
		return true;
	want += 32;
	SetHandleSize(h, want * size);
	return GetHandleSize(h) == want * size;
}

/* ---- fonts ---- */

static void Font(Page *pg, int mf, short family, short size, Style face)
{
	pg->family[mf] = family;
	pg->size[mf] = size;
	pg->face[mf] = face;
}

/* A font's widths from the Font Manager's width table, which one
   CharWidth fills for the port's font: much quicker than 256 CharWidths
   on a 68000. Checked against CharWidth; false when they differ. */
static Boolean TableWidths(unsigned char *out)
{
	static const char kCheck[] = "eWim .";
	volatile long global = 0x0B2A;      /* WidthTabHandle (GCC frowns on LMGetWidthTabHandle) */
	WidthTableHandle h;
	long extra;
	short c;
	long w;
	const char *s;

	w = CharWidth('e');
	h = *(WidthTableHandle *) global;
	if (h == NULL || *h == NULL)
		return false;
	/* what bold and the like add to every character, in whole pixels */
	extra = w - (((**h).tabData['e'] + 0x8000L) >> 16);
	for (c = 0; c < 256; c++) {
		w = (((**h).tabData[c] + 0x8000L) >> 16) + extra;
		out[c] = (unsigned char) (w < 0 ? 0 : w > 255 ? 255 : w);
	}
	for (s = kCheck; *s; s++)
		if (CharWidth(*s) != out[(unsigned char) *s])
			return false;
	return true;
}

/* The screen's fonts, kept once measured: the preview opens again without
   measuring (a printer's port is measured each time) */
typedef struct {
	LayFonts f;
	short avg[MF_COUNT];
} Fonts;

static Fonts *gScreenFonts;

/* The fonts, measured in the current port */
static void Measure(Page *pg)
{
	static const char kSample[] = "etaoin shrdlu cmfwyp";
	GrafPtr port;
	short oldFont, oldSize, mf, c, w;
	Style oldFace;
	FontInfo fi;
	short body = RealFont(kTimes, 12) ? kTimes : kGeneva;
	long sum;
	const char *s;

	Font(pg, MF_BODY, body, 12, 0);
	Font(pg, MF_BOLD, body, 12, bold);
	Font(pg, MF_ITALIC, body, 12, italic);
	Font(pg, MF_BOLD_ITALIC, body, 12, bold | italic);
	Font(pg, MF_CODE, kMonaco, 9, 0);
	Font(pg, MF_H1, body, 18, bold);
	Font(pg, MF_H2, body, 14, bold);
	Font(pg, MF_H3, body, 12, bold);
	Font(pg, MF_TITLE, body, RealFont(body, 24) ? 24 : 18, bold);
	Font(pg, MF_SMALL, kGeneva, 9, 0);
	GetPort(&port);
	if (port->device == 0 && gScreenFonts != NULL) {
		pg->f = gScreenFonts->f;
		memcpy(pg->avg, gScreenFonts->avg, sizeof(pg->avg));
		return;
	}
	DLOG(("Measure: start %lu", TickCount()));
	oldFont = port->txFont;
	oldSize = port->txSize;
	oldFace = port->txFace;
	for (mf = 0; mf < MF_COUNT; mf++) {
		TextFont(pg->family[mf]);
		TextSize(pg->size[mf]);
		TextFace(pg->face[mf]);
		GetFontInfo(&fi);
		pg->f.ascent[mf] = fi.ascent;
		pg->f.descent[mf] = fi.descent;
		pg->f.leading[mf] = fi.leading;
		if (!TableWidths(pg->f.width[mf])) {
			DLOG(("Measure: font %d by CharWidth", mf));
			for (c = 0; c < 256; c++) {
				w = CharWidth((char) c);
				pg->f.width[mf][c] = (unsigned char) (w < 0 ? 0 : w > 255 ? 255 : w);
			}
		}
		for (sum = 0, s = kSample; *s; s++)
			sum += pg->f.width[mf][(unsigned char) *s];
		pg->avg[mf] = (short) (sum / (long) (sizeof(kSample) - 1));
	}
	TextFont(oldFont);
	TextSize(oldSize);
	TextFace(oldFace);
	DLOG(("Measure: end %lu", TickCount()));
	if (port->device == 0 && gScreenFonts == NULL && (gScreenFonts = (Fonts *) NewPtr(sizeof(Fonts))) != NULL) {
		gScreenFonts->f = pg->f;
		memcpy(gScreenFonts->avg, pg->avg, sizeof(pg->avg));
	}
}

static short LineHeight(const Page *pg, int mf)
{
	return (short) (pg->f.ascent[mf] + pg->f.descent[mf] + pg->f.leading[mf]);
}

/* ---- a block's text and size ---- */

static short Indent(const PgBlock *p)
{
	return (short) (p->b.quote * kQuoteIndent + p->b.depth * kListIndent);
}

static Boolean Boxed(const MpBlock *b)
{
	return b->kind == MP_CODE || b->kind == MP_TABLE || b->kind == MP_HTML;
}

/* The width lines have in the block */
static short LineRoom(const Page *pg, const PgBlock *p)
{
	short w = (short) (pg->width - Indent(p) - (Boxed(&p->b) ? 2 * kPad : 0));

	return w < 20 ? 20 : w;
}

/* The block's display text into the buffers: its length, or -1 when
   there's no room */
static long Text(Page *pg, const char *text, long n, const PgBlock *p, MpLinkFn fn, void *ctx)
{
	long len, k;

	if (p->what == kLinksHead) {
		strcpy(pg->buf, "Links");
		memset(pg->sty, 0, 5);
		return 5;
	}
	if (p->what == kLinkItem) {
		/* the link's text, then its address on a line of its own */
		len = mp_link_text(text, n, p->b.at, pg->buf, pg->cap / 2);
		if (len > pg->cap / 2 - 1)
			len = pg->cap / 2 - 1;
		memset(pg->sty, 0, (size_t) len);
		if (text[p->b.at] != '<') {
			pg->buf[len] = MP_BREAK;
			pg->sty[len++] = 0;
		} else {
			len = 0;
		}
		k = mp_link_url(text, n, p->b.at, pg->buf + len, pg->cap - len);
		if (k > pg->cap - len - 1)
			k = pg->cap - len - 1;
		memset(pg->sty + len, MS_LINK | MS_NOTE, (size_t) k);
		return len + k;
	}
	/* mdscan's classes need room for the source, the text for itself
	   (usually no longer: code's tabs can make it so) */
	if (!Room(pg, p->b.end - p->b.start))
		return -1;
	len = mp_text_links(text, n, &p->b, pg->buf, pg->sty, pg->cap, pg->cls, fn, ctx);
	if (len > pg->cap) {
		if (!Room(pg, len))
			return -1;
		len = mp_text(text, n, &p->b, pg->buf, pg->sty, pg->cap, pg->cls);
	}
	return len;
}

/* The space above a block, after the one before it */
static short Gap(const PgBlock *p, const PgBlock *prev)
{
	if (prev == NULL)
		return 4;
	if (prev->b.kind <= MP_LEAD && p->b.kind > MP_LEAD)
		return 16;                  /* the head, then the body */
	switch (p->b.kind) {
	case MP_BYLINE:
		return 2;
	case MP_LEAD:
		return 6;
	case MP_HEADING:
		return (short) (p->b.level == 1 ? 18 : p->b.level == 2 ? 14 : 10);
	case MP_ITEM:
		return (short) (prev->b.kind == MP_ITEM || prev->b.depth > 0 ? 3 : 8);
	case MP_PARA:
		if (p->b.depth > 0)
			return 4;
		break;
	}
	return 8;
}

/* A guess at the block's height, from its length */
static short Estimate(const Page *pg, const PgBlock *p, const char *text)
{
	int mf = p->what == kLinkItem ? MF_BODY : mp_font(&p->b, 0);
	short lh = LineHeight(pg, mf);
	long lines, chars, i;

	switch (p->b.kind) {
	case MP_RULE:
		return kRuleHeight;
	case MP_IMAGE:
		return kImageHeight;
	case MP_CODE:
	case MP_TABLE:
	case MP_HTML:
		for (lines = 1, i = p->b.start; i < p->b.end - 1; i++)
			if (text[i] == '\r' || text[i] == '\n')
				lines++;
		return (short) (lines * lh + 2 * kPad);
	}
	if (p->what == kLinkItem)
		return (short) (lh + LineHeight(pg, MF_SMALL));
	chars = p->b.kind <= MP_LEAD ? 30 : p->b.end - p->b.start;
	lines = (chars * pg->avg[mf] + LineRoom(pg, p) - 1) / LineRoom(pg, p);
	if (lines < 1)
		lines = 1;
	if (lines > 400)
		lines = 400;
	return (short) (lines * lh);
}

static void Found(void *ctx, long at)
{
	Page *pg = (Page *) ctx;

	if (pg->nfound < 0 || !Grow((Handle) pg->found, pg->nfound + 1, sizeof(long))) {
		pg->nfound = -1;            /* out of memory: the list stays as it was */
		return;
	}
	(*pg->found)[pg->nfound++] = at;
}

/* The links found in [start, end) in place of the ones listed there */
static void SpliceLinks(Page *pg, long start, long end)
{
	long lo, hi, k = pg->nfound, i;
	long *l;

	if (k < 0)
		return;
	for (lo = 0; lo < pg->nlinks && (*pg->links)[lo] < start; lo++)
		;
	for (hi = lo; hi < pg->nlinks && (*pg->links)[hi] < end; hi++)
		;
	if (hi - lo == k && memcmp(*pg->links + lo, *pg->found, (size_t) k * sizeof(long)) == 0)
		return;                     /* the same links */
	if (!Grow((Handle) pg->links, pg->nlinks - (hi - lo) + k, sizeof(long)))
		return;
	l = *pg->links;
	memmove(l + lo + k, l + hi, (size_t) (pg->nlinks - hi) * sizeof(long));
	for (i = 0; i < k; i++)
		l[lo + i] = (*pg->found)[i];
	pg->nlinks += k - (hi - lo);
	pg->linksDue = true;
}

/* An image's size on the page: its picture's, made to fit the line, when
   there's one on the Mac (images.h); a framed box when not */
static Boolean ImageFit(const Page *pg, const char *text, long n, const PgBlock *p, char *url, long *len,
		short *w, short *h)
{
	short room = LineRoom(pg, p);

	*len = mp_link_url(text, n, p->b.at, url, 256);
	if (*len <= 0 || !ImgSize(url, *len, w, h))
		return false;
	if (*w > room) {
		*h = (short) ((long) *h * room / *w);
		*w = room;
	}
	if (*h < 1)
		*h = 1;
	if (*h > 4000)
		*h = 4000;
	return true;
}

/* Lays block i out exactly; 0 when memory ran out */
static Boolean Exact(Page *pg, const char *text, long n, long i)
{
	PgBlock p = (*pg->blocks)[i];
	LayLine line;
	long len, at = 0;
	short h = 0, w;

	if (p.b.kind == MP_RULE) {
		h = kRuleHeight;
	} else if (p.b.kind == MP_IMAGE) {
		char url[256];
		short w;
		if (!ImageFit(pg, text, n, &p, url, &len, &w, &h))
			h = kImageHeight;
	} else {
		pg->nfound = 0;
		len = Text(pg, text, n, &p, p.what == kBody ? Found : NULL, pg);
		if (len < 0)
			return false;
		if (p.what == kBody && p.b.kind > MP_LEAD)
			SpliceLinks(pg, p.b.start, p.b.end);
		w = LineRoom(pg, &p);
		do {
			at = lay_line(&pg->f, &p.b, pg->buf, pg->sty, len, at, w, &line);
			h += line.height;
		} while (at < len);
		if (Boxed(&p.b))
			h += 2 * kPad;
	}
	(*pg->blocks)[i].h = h;
	(*pg->blocks)[i].exact = true;
	return true;
}

/* The blocks' places from block i on */
static void Restack(Page *pg, long i)
{
	PgBlock *b = *pg->blocks;
	long y = i > 0 ? b[i - 1].y + b[i - 1].h : 0;

	for (; i < pg->count; i++) {
		b[i].y = y + b[i].gap;
		y = b[i].y + b[i].h;
	}
	pg->height = y + kBottom;
}

static Boolean Append(Page *pg, const PgBlock *p)
{
	if (!Grow((Handle) pg->blocks, pg->count + 1, sizeof(PgBlock)))
		return false;
	(*pg->blocks)[pg->count++] = *p;
	return true;
}

/* The numbered list of links after the post's own blocks; an item whose
   link was listed before keeps its height */
static void MakeLinkItems(Page *pg, const char *text)
{
	PgBlock p, prev, *old = NULL;
	Handle oldH = NULL;
	long i, j = 0, oldCount = 0, shown = 0;

	pg->linksDue = false;
	if (pg->count > pg->body) {
		/* the old items, to keep their heights */
		oldCount = pg->count - pg->body;
		oldH = NewHandle(oldCount * (long) sizeof(PgBlock));
		if (oldH != NULL) {
			BlockMove((Ptr) (*pg->blocks + pg->body), *oldH, oldCount * (long) sizeof(PgBlock));
			HLock(oldH);
			old = (PgBlock *) *oldH;
		} else {
			oldCount = 0;
		}
	}
	pg->count = pg->body;
	memset(&prev, 0, sizeof(prev));
	if (pg->body > 0)
		prev = (*pg->blocks)[pg->body - 1];
	if (pg->nlinks > 0) {
		memset(&p, 0, sizeof(p));
		p.what = kLinksHead;
		p.b.kind = MP_HEADING;
		p.b.level = 3;
		p.gap = Gap(&p, pg->body > 0 ? &prev : NULL);
		p.h = Estimate(pg, &p, text);
		if (oldCount > 0 && old[0].what == kLinksHead) {
			p.h = old[0].h;
			p.exact = old[0].exact;
		}
		if (Append(pg, &p)) {
			prev = p;
			for (i = 0; i < pg->nlinks; i++) {
				long at = (*pg->links)[i];
				if (text[at] == '!')
					continue;           /* images show as boxes already */
				memset(&p, 0, sizeof(p));
				p.what = kLinkItem;
				p.b.kind = MP_ITEM;
				p.b.depth = 1;
				p.b.number = ++shown;
				p.b.at = at;
				p.gap = Gap(&p, &prev);
				while (j < oldCount && (old[j].what != kLinkItem || old[j].b.at < at))
					j++;
				if (j < oldCount && old[j].b.at == at) {
					p.h = old[j].h;
					p.exact = old[j].exact;
				} else {
					p.h = Estimate(pg, &p, text);
				}
				if (!Append(pg, &p))
					break;
				prev = p;
			}
		}
		if (shown == 0)
			pg->count = pg->body;   /* only images: no list */
	}
	if (oldH != NULL)
		DisposeHandle(oldH);
	Restack(pg, pg->body);
}

/* The old block is this one, laid out before an edit that moved it by delta */
static Boolean Same(const PgBlock *old, const MpBlock *b, long delta)
{
	const MpBlock *o = &old->b;

	return old->exact && old->what == kBody && o->start + delta == b->start && o->end + delta == b->end
		&& o->at + delta == b->at && o->kind == b->kind && o->level == b->level && o->depth == b->depth
		&& o->quote == b->quote && o->number == b->number && o->task == b->task;
}

/* The scans are in the same state, b's moved by delta from a's */
static Boolean SameScan(const MpScan *a, const MpScan *b, long delta)
{
	int i;

	if (a->s.pos + delta != b->s.pos || a->s.front != b->s.front || a->s.fence != b->s.fence
			|| a->s.fenceLen != b->s.fenceLen || a->s.quoteFace != b->s.quoteFace || a->head != b->head
			|| a->depth != b->depth || a->listQuote != b->listQuote || a->blank != b->blank)
		return false;
	for (i = 0; i < 8; i++)
		if (a->indents[i] != b->indents[i])
			return false;
	return true;
}

static Boolean AddMark(Page *pg, long block, const MpScan *scan)
{
	if (!Grow((Handle) pg->marks, pg->nmarks + 1, sizeof(PgMark)))
		return false;
	(*pg->marks)[pg->nmarks].block = block;
	(*pg->marks)[pg->nmarks].scan = *scan;
	pg->nmarks++;
	return true;
}

/* The start of the line before the one that holds pos (0 at the top) */
static long LineBefore(const char *text, long pos)
{
	while (pos > 0 && text[pos - 1] != '\r' && text[pos - 1] != '\n')
		pos--;
	if (pos > 0)
		pos--;
	while (pos > 0 && text[pos - 1] != '\r' && text[pos - 1] != '\n')
		pos--;
	return pos;
}

Boolean PageLayout(Page *pg, const char *text, long n, short width, long from, long to, long delta)
{
	MpScan scan;
	PgBlock p, prev, *old = NULL;
	Handle oldH, oldMarksH;
	PgMark *om;
	long oldBody = pg->body, oldCount = pg->count, j = 0, i, keep, oldTo, oldMarks = pg->nmarks;
	long m, start = -1, lastMark = 0, before, ahead = 0;
	Boolean any = false, caught = false;

	if (!pg->measured) {
		Measure(pg);
		pg->measured = true;
	}
	if (width != pg->width) {
		pg->width = width;
		from = 0;
		to = n;
		oldBody = oldCount = 0;
	}
	if (!Room(pg, 256))
		return false;
	/* the links outside the edit stay; those after it move */
	oldTo = to - delta;
	for (i = keep = 0; i < pg->nlinks && from > 0; i++) {
		long at = (*pg->links)[i];
		if (at >= oldTo)
			at += delta;
		else if (at >= from)
			continue;
		if (at < n)
			(*pg->links)[keep++] = at;
	}
	pg->nlinks = keep;
	/* the old blocks, to keep their heights, and the old scan's marks */
	oldH = (Handle) pg->blocks;
	pg->blocks = (PgBlock **) NewHandle(0);
	oldMarksH = (Handle) pg->marks;
	pg->marks = (PgMark **) NewHandle(0);
	if (pg->blocks == NULL || pg->marks == NULL) {
		if (pg->blocks != NULL)
			DisposeHandle((Handle) pg->blocks);
		if (pg->marks != NULL)
			DisposeHandle((Handle) pg->marks);
		pg->blocks = (PgBlock **) oldH;
		pg->marks = (PgMark **) oldMarksH;
		return false;
	}
	HLock(oldH);
	HLock(oldMarksH);
	old = (PgBlock *) *oldH;
	om = (PgMark *) *oldMarksH;
	pg->count = pg->body = pg->nmarks = 0;
	memset(&p, 0, sizeof(p));
	memset(&prev, 0, sizeof(prev));
	mp_begin(&scan, text, n);
	/* the blocks before the edit stay as they were: start at the last mark
	   a line or more before it (a block can end on the line after its text) */
	if (from > 0) {
		before = LineBefore(text, from);
		for (m = 0; m < oldMarks && om[m].scan.s.pos <= before && om[m].block <= oldBody; m++)
			start = m;
	}
	if (start > 0 && om[start].block > 0) {
		for (i = 0; i < om[start].block; i++)
			if (!Append(pg, &old[i]))
				goto failed;
		for (m = 0; m < start; m++)
			if (!AddMark(pg, om[m].block, &om[m].scan))
				goto failed;
		scan = om[start].scan;
		scan.text = text;
		scan.n = n;
		prev = old[pg->count - 1];
		any = true;
		j = pg->count;
		lastMark = om[start - 1].block;
		ahead = start;
	}
	for (;;) {
		/* past the edit, and in step with the old scan: the rest is the old
		   blocks, moved */
		if (from > 0 && scan.s.pos >= to) {
			while (ahead < oldMarks && om[ahead].scan.s.pos + delta < scan.s.pos)
				ahead++;
			if (ahead < oldMarks && om[ahead].scan.s.pos >= oldTo && om[ahead].block <= oldBody
					&& SameScan(&om[ahead].scan, &scan, delta)) {
				long shift = pg->count - om[ahead].block;
				for (i = om[ahead].block; i < oldBody; i++) {
					p = old[i];
					if (p.b.at >= p.b.start)
						p.b.at += delta;    /* code and tables have none (0) */
					p.b.start += delta;
					p.b.end += delta;
					if (!Append(pg, &p))
						goto failed;
				}
				for (m = ahead; m < oldMarks && om[m].block <= oldBody; m++) {
					MpScan moved = om[m].scan;
					moved.text = text;
					moved.n = n;
					moved.s.pos += delta;
					if (!AddMark(pg, om[m].block + shift, &moved))
						goto failed;
				}
				caught = true;
				break;
			}
		}
		if (pg->count == 0 || pg->count - lastMark >= kMarkEvery) {
			if (!AddMark(pg, pg->count, &scan))
				goto failed;
			lastMark = pg->count;
		}
		if (!mp_next(&scan, &p.b))
			break;
		p.what = kBody;
		p.gap = Gap(&p, any ? &prev : NULL);
		p.exact = false;
		if (p.b.kind > MP_LEAD && (p.b.end <= from || p.b.start >= to)) {
			long d = p.b.start >= to ? delta : 0;
			while (j < oldBody && old[j].b.start + d < p.b.start)
				j++;
			if (j < oldBody && Same(&old[j], &p.b, d)) {
				p.h = old[j].h;
				p.exact = true;
			}
		}
		if (!p.exact)
			p.h = Estimate(pg, &p, text);
		if (!Append(pg, &p))
			goto failed;
		prev = p;
		any = true;
	}
	DLOG(("PageLayout: %ld blocks, from block %ld, %s", pg->count,
		start > 0 ? om[start].block : 0L, caught ? "caught up" : "to the end"));
#ifdef TOPICA_DEBUG
	{
		/* the whole scan, to check the shortcut */
		MpScan full;
		MpBlock b;
		long k = 0, bad = 0;
		mp_begin(&full, text, n);
		while (mp_next(&full, &b)) {
			const MpBlock *o = k < pg->count ? &(*pg->blocks)[k].b : NULL;
			if (o == NULL || o->start != b.start || o->end != b.end || o->at != b.at || o->kind != b.kind
					|| o->depth != b.depth || o->quote != b.quote || o->number != b.number)
				bad++;
			k++;
		}
		if (bad > 0 || k != pg->count)
			DLOG(("PageLayout: MISMATCH %ld blocks differ, %ld vs %ld", bad, k, pg->count));
	}
#endif
	(void) caught;                      /* only the debug log reads it */
	pg->body = pg->count;
	/* the old link list after the body, moved with the links, so that
	   MakeLinkItems keeps the heights of links the edit didn't touch */
	for (i = oldBody; i < oldCount && from > 0; i++) {
		p = old[i];
		if (p.what == kLinkItem) {
			if (p.b.at >= oldTo)
				p.b.at += delta;
			else if (p.b.at >= from)
				continue;
		}
		if (!Append(pg, &p))
			break;
	}
	HUnlock(oldH);
	DisposeHandle(oldH);
	DisposeHandle(oldMarksH);
	MakeLinkItems(pg, text);
	return true;
failed:
	HUnlock(oldH);
	DisposeHandle(oldH);
	DisposeHandle(oldMarksH);
	return false;
}

Boolean PageRefine(Page *pg, const char *text, long n, long top, long bottom, long ticks,
		long *shift, Boolean *redraw)
{
	unsigned long start = TickCount();
	long i;
	short h;
	Boolean all = true;

	*shift = 0;
	*redraw = false;
	/* what's on the screen first */
	for (i = 0; i < pg->count; i++) {
		PgBlock *p = &(*pg->blocks)[i];
		if (p->y - p->gap > bottom)
			break;
		if (p->exact || p->y + p->h < top)
			continue;
		if (!Exact(pg, text, n, i))
			return false;
		Restack(pg, i + 1);
		*redraw = true;
		if (pg->linksDue && i < pg->body)
			MakeLinkItems(pg, text);
	}
	/* then the rest, in order, while there's time */
	for (i = 0; i < pg->count; i++) {
		PgBlock *p = &(*pg->blocks)[i];
		if (p->exact)
			continue;
		if (ticks == 0 || (ticks > 0 && (long) (TickCount() - start) >= ticks)) {
			all = false;
			break;
		}
		h = p->h;
		if (!Exact(pg, text, n, i))
			return false;
		p = &(*pg->blocks)[i];
		if (p->y + h <= top)
			*shift += p->h - h;     /* above the screen: what's on it moves */
		else if (p->y <= bottom)
			*redraw = true;
		Restack(pg, i + 1);
		if (pg->linksDue && i < pg->body)
			MakeLinkItems(pg, text);
	}
	if (pg->linksDue)
		MakeLinkItems(pg, text);
	return all;
}

long PageHeight(const Page *pg)
{
	return pg->height;
}

/* ---- drawing ---- */

/* The theme's color for role (theme.h), on a color screen */
static void Color(Boolean color, short role)
{
	RGBColor c;

	if (!color)
		return;
	ThemeColor(role, &c);
	RGBForeColor(&c);
}

/* The text's color */
static void Black(Boolean color)
{
	if (color)
		Color(true, kThText);
	else
		ForeColor(blackColor);
}

/* A line's characters in runs of one look */
static void DrawLine(Page *pg, const PgBlock *p, const LayLine *line, short x, short base, Boolean color)
{
	long i = line->start, e, end = line->start + line->len;
	int mf, look;

	MoveTo(x, base);
	while (i < end) {
		mf = mp_font(&p->b, pg->sty[i]);
		look = pg->sty[i] & (MS_LINK | MS_META);
		for (e = i + 1; e < end && mp_font(&p->b, pg->sty[e]) == mf && (pg->sty[e] & (MS_LINK | MS_META)) == look; e++)
			;
		TextFont(pg->family[mf]);
		TextSize(pg->size[mf]);
		TextFace((Style) (pg->face[mf] | (look & MS_LINK ? underline : 0)));
		if (look & MS_LINK)
			Color(color, kThLink);
		else if ((look & MS_META) || p->b.kind == MP_BYLINE)
			Color(color, kThMark);
		else
			Black(color);
		if ((pg->sty[i] & MS_NOTE) && p->what == kBody) {
			Move(0, -4);                /* a footnote's number, raised */
			DrawText(pg->buf, (short) i, (short) (e - i));
			Move(0, 4);
		} else {
			DrawText(pg->buf, (short) i, (short) (e - i));
		}
		if ((pg->face[mf] & italic) && e < end && !(pg->face[mp_font(&p->b, pg->sty[e])] & italic))
			Move(1, 0);                 /* room for the slant */
		i = e;
	}
	Black(color);
}

/* An item's marker: a bullet, a number or a task's box, left of x */
static void Marker(Page *pg, const PgBlock *p, short x, short base, Boolean color)
{
	char s[16];
	short len = 0, w;
	long v = p->b.number;

	TextFont(pg->family[MF_BODY]);
	TextSize(pg->size[MF_BODY]);
	TextFace(0);
	Black(color);
	if (p->b.task) {
		Rect r;
		SetRect(&r, (short) (x - 14), (short) (base - 9), (short) (x - 5), base);
		FrameRect(&r);
		if (p->b.task == 'x') {
			MoveTo(r.left + 2, r.top + 2);
			LineTo(r.right - 3, r.bottom - 3);
			MoveTo(r.right - 3, r.top + 2);
			LineTo(r.left + 2, r.bottom - 3);
		}
		return;
	}
	if (v > 0) {
		char d[12];
		short k = 0;
		do
			d[k++] = (char) ('0' + v % 10);
		while ((v /= 10) > 0 && k < 10);
		while (k > 0)
			s[len++] = d[--k];
		s[len++] = '.';
	} else {
		s[len++] = p->b.depth > 1 ? (char) 0xA1 : (char) 0xA5;   /* a ring, or a bullet */
	}
	w = TextWidth(s, 0, len);
	MoveTo((short) (x - 5 - w), base);
	DrawText(s, 0, len);
}

static void DrawBlock(Page *pg, const char *text, long n, const PgBlock *p, long top, const Rect *r, Boolean color)
{
	short x = (short) (r->left + Indent(p)), py = (short) (r->top + (p->y - top)), q;
	short right = (short) (r->left + pg->width);
	Rect box;
	LayLine line;
	long len, at = 0;
	short y;

	/* a bar for each quote it's in */
	for (q = 0; q < p->b.quote; q++) {
		SetRect(&box, (short) (r->left + q * kQuoteIndent + 2), (short) (py - (p->gap > 6 ? 3 : p->gap)),
			(short) (r->left + q * kQuoteIndent + 5), (short) (py + p->h));
		if (color) {
			Color(true, kThLine);
			PaintRect(&box);
			Black(true);
		} else {
			FillRect(&box, &qd.gray);
		}
	}
	SetRect(&box, x, py, right, (short) (py + p->h));
	if (p->b.kind == MP_RULE) {
		MoveTo(x, (short) (py + kRuleHeight / 2));
		if (color)
			Color(true, kThLine);
		else
			PenPat(&qd.gray);
		LineTo(right, (short) (py + kRuleHeight / 2));
		PenNormal();
		Black(color);
		return;
	}
	if (p->b.kind == MP_IMAGE) {
		/* the picture, if it's on the Mac; else a framed box: the
		   description, and the file */
		char url[256];
		short w, h;
		if (ImageFit(pg, text, n, p, url, &len, &w, &h)) {
			Rect pic;
			SetRect(&pic, (short) (x + (right - x - w) / 2), py, 0, (short) (py + h));
			pic.right = (short) (pic.left + w);
			if (ImgDraw(url, len, &pic))
				return;
		}
		if (color) {
			Color(true, kThShade);
			PaintRect(&box);
			Color(true, kThLine);
		}
		FrameRect(&box);
		Black(color);
		len = Text(pg, text, n, p, NULL, NULL);
		if (len > 0) {
			TextFont(pg->family[MF_ITALIC]);
			TextSize(pg->size[MF_ITALIC]);
			TextFace(italic);
			w = TextWidth(pg->buf, 0, (short) len);
			MoveTo((short) ((box.left + box.right - w) / 2 > box.left + 4 ? (box.left + box.right - w) / 2 : box.left + 4),
				(short) (py + 6 + pg->f.ascent[MF_ITALIC]));
			DrawText(pg->buf, 0, (short) len);
		}
		len = mp_link_url(text, n, p->b.at, url, sizeof(url));
		if (len > 0) {
			TextFont(pg->family[MF_SMALL]);
			TextSize(pg->size[MF_SMALL]);
			TextFace(0);
			w = TextWidth(url, 0, (short) len);
			Color(color, kThMark);
			MoveTo((short) ((box.left + box.right - w) / 2 > box.left + 4 ? (box.left + box.right - w) / 2 : box.left + 4),
				(short) (box.bottom - 6 - pg->f.descent[MF_SMALL]));
			DrawText(url, 0, (short) len);
			Black(color);
		}
		return;
	}
	if (Boxed(&p->b)) {
		if (color) {
			Color(true, kThShade);
			PaintRect(&box);
			Black(true);
		} else {
			FrameRect(&box);
		}
		x += kPad;
	}
	len = Text(pg, text, n, p, NULL, NULL);
	if (len < 0)
		return;
	y = (short) (py + (Boxed(&p->b) ? kPad : 0));
	do {
		at = lay_line(&pg->f, &p->b, pg->buf, pg->sty, len, at, LineRoom(pg, p), &line);
		if (y + line.height > r->top - 40 && y < r->bottom + 40) {
			if (line.start == 0 && p->b.kind == MP_ITEM)
				Marker(pg, p, x, (short) (y + line.ascent), color);
			DrawLine(pg, p, &line, x, (short) (y + line.ascent), color);
		}
		y += line.height;
	} while (at < len && y < r->bottom + 40);
	if (p->b.kind == MP_ITEM && len == 0)
		Marker(pg, p, x, (short) (py + pg->f.ascent[MF_BODY]), color);
}

void PageDraw(Page *pg, const char *text, long n, long top, const Rect *r, Boolean color)
{
	long i, bottom = top + (r->bottom - r->top);
	PgBlock *p;
	GrafPtr port;
	short oldFont, oldSize;
	Style oldFace;

	GetPort(&port);
	oldFont = port->txFont;
	oldSize = port->txSize;
	oldFace = port->txFace;
	if (color) {
		RGBColor bg;
		ThemeColor(kThBg, &bg);
		RGBBackColor(&bg);
	}
	EraseRect(r);
	HLock((Handle) pg->blocks);
	for (i = 0; i < pg->count; i++) {
		p = &(*pg->blocks)[i];
		if (p->y + p->h < top - 4)
			continue;
		if (p->y - p->gap > bottom)
			break;
		DrawBlock(pg, text, n, p, top, r, color);
	}
	HUnlock((Handle) pg->blocks);
	TextFont(oldFont);
	TextSize(oldSize);
	TextFace(oldFace);
	PenNormal();
}

long PageBreak(Page *pg, const char *text, long n, long top, long limit)
{
	long i, len, at, best = limit;
	PgBlock *p;
	LayLine line;
	long y;

	HLock((Handle) pg->blocks);
	for (i = 0; i < pg->count; i++) {
		p = &(*pg->blocks)[i];
		if (p->y + p->h <= limit)
			continue;
		if (p->y >= limit)
			break;
		/* the block runs over the limit */
		best = p->y - p->gap > top ? p->y - p->gap : limit;
		if (p->b.kind == MP_RULE || p->b.kind == MP_IMAGE)
			break;
		len = Text(pg, text, n, p, NULL, NULL);
		if (len < 0)
			break;
		y = p->y + (Boxed(&p->b) ? kPad : 0);
		at = 0;
		do {
			at = lay_line(&pg->f, &p->b, pg->buf, pg->sty, len, at, LineRoom(pg, p), &line);
			if (y + line.height > limit)
				break;
			y += line.height;
			if (y > top)
				best = y;
		} while (at < len);
		break;
	}
	HUnlock((Handle) pg->blocks);
	return best > top ? best : limit;
}

long PageSourceAt(const Page *pg, long y)
{
	long i;
	const PgBlock *p;

	for (i = 0; i < pg->count; i++) {
		p = &(*pg->blocks)[i];
		if (y < p->y + p->h) {
			if (p->what != kBody || p->b.kind <= MP_LEAD)
				return -1;
			return p->b.start;
		}
	}
	return -1;
}
