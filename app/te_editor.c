/*
 * te_editor.c: the editor interface (editor.h) on TextEdit. TextEdit wraps
 * the text to the view's width and holds at most 32,767 characters; we
 * stop at 32,000 to leave it room.
 *
 * Styled TextEdit (System 6 has it on the Plus and the 512Ke too) shows the
 * Markdown classes. Every style keeps the base font's size and the line
 * height stays fixed, so scrolling still goes by whole lines.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Memory.h>
#include <Events.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <Traps.h>
#include <string.h>

#include "../core/mdscan.h"
#include "editor.h"
#include "util.h"

struct Editor {
	TEHandle te;
	Boolean styled;                     /* styled TextEdit */
	short font, size;                   /* the base style */
	short height, ascent;               /* every line's */
};

enum { kMaxText = 32000 };
enum { kLeftArrow = 0x1C, kRightArrow = 0x1D, kUpArrow = 0x1E, kDownArrow = 0x1F,
	kHomeKey = 0x01, kEndKey = 0x04, kPageUp = 0x0B, kPageDown = 0x0C };

long EdMaxLength(void)
{
	return kMaxText;
}

/* The text area inside the view: a few pixels of margin */
static void TextRect(const Rect *view, Rect *r)
{
	*r = *view;
	InsetRect(r, 4, 2);
}

static Boolean HasStyledTE(void)
{
	return NGetTrapAddress(_TEStyleNew, kToolboxTrapType) != NGetTrapAddress(_Unimplemented, kToolboxTrapType);
}

/* Fixes the line height from the base font: TextEdit's own fields for plain
   TextEdit, and -1 there would mean a height per line for styled */
static void Metrics(Editor *ed)
{
	FontInfo fi;
	TEPtr t;

	TextFont(ed->font);
	TextSize(ed->size);
	TextFace(0);
	GetFontInfo(&fi);
	ed->ascent = fi.ascent;
	ed->height = (short) (fi.ascent + fi.descent + fi.leading);
	t = *ed->te;
	if (!ed->styled) {
		t->txFont = ed->font;
		t->txSize = ed->size;
	}
	t->fontAscent = ed->ascent;
	t->lineHeight = ed->height;
}

static void BaseStyle(Editor *ed, TextStyle *ts)
{
	memset(ts, 0, sizeof(*ts));
	ts->tsFont = ed->font;
	ts->tsSize = ed->size;
}

/* All the text in the base style, through the selection's fields so that
   nothing is drawn */
static void Plain(Editor *ed)
{
	TextStyle ts;
	short s = (*ed->te)->selStart, e = (*ed->te)->selEnd;

	BaseStyle(ed, &ts);
	(*ed->te)->selStart = 0;
	(*ed->te)->selEnd = (*ed->te)->teLength;
	TESetStyle(doAll, &ts, false, ed->te);
	(*ed->te)->selStart = s;
	(*ed->te)->selEnd = e;
}

Editor *EdNew(const Rect *view, short font, short size)
{
	Editor *ed = (Editor *) NewPtrClear(sizeof(Editor));
	Rect r;

	if (ed == NULL)
		return NULL;
	ed->font = font;
	ed->size = size;
	TextFont(font);
	TextSize(size);
	TextRect(view, &r);
	ed->styled = HasStyledTE();
	ed->te = ed->styled ? TEStyleNew(&r, &r) : TENew(&r, &r);
	if (ed->te == NULL) {
		DisposePtr((Ptr) ed);
		return NULL;
	}
	Metrics(ed);                        /* TEStyleNew took the port's font */
	TEAutoView(true, ed->te);
	return ed;
}

Boolean EdCanStyle(Editor *ed)
{
	return ed->styled;
}

void EdDispose(Editor *ed)
{
	if (ed == NULL)
		return;
	TEDispose(ed->te);
	DisposePtr((Ptr) ed);
}

void EdSetView(Editor *ed, const Rect *view)
{
	TEPtr t;
	Rect r;
	short top;

	TextRect(view, &r);
	HLock((Handle) ed->te);
	t = *ed->te;
	top = t->viewRect.top - t->destRect.top;    /* keep the scroll position */
	t->viewRect = r;
	t->destRect = r;
	OffsetRect(&t->destRect, 0, (short) -top);
	HUnlock((Handle) ed->te);
	TECalText(ed->te);
}

void EdSetFont(Editor *ed, short font, short size)
{
	ed->font = font;
	ed->size = size;
	Metrics(ed);
	if (ed->styled)
		Plain(ed);                      /* the window restyles it afterwards */
	TECalText(ed->te);
}

void EdDraw(Editor *ed)
{
	Rect r = (*ed->te)->viewRect;

	InsetRect(&r, -4, -2);
	EraseRect(&r);
	TEUpdate(&r, ed->te);
}

void EdActivate(Editor *ed, Boolean active)
{
	if (active)
		TEActivate(ed->te);
	else
		TEDeactivate(ed->te);
}

void EdIdle(Editor *ed)
{
	TEIdle(ed->te);
}

void EdClick(Editor *ed, Point local, Boolean extend)
{
	TEClick(local, extend, ed->te);
}

static short LineHeight(Editor *ed)
{
	return (*ed->te)->lineHeight;
}

/* Lines that fit in the view */
static short PageLines(Editor *ed)
{
	TEPtr t = *ed->te;
	short n = (short) ((t->viewRect.bottom - t->viewRect.top) / t->lineHeight);

	return n < 1 ? 1 : n;
}

Boolean EdNavigate(Editor *ed, char c, short modifiers)
{
	short value, max, page;

	(void) modifiers;
	switch (c) {
	case kLeftArrow: case kRightArrow: case kUpArrow: case kDownArrow:
		TEKey(c, ed->te);
		return true;
	case kHomeKey: case kEndKey: case kPageUp: case kPageDown:
		EdScrollInfo(ed, &value, &max, &page);
		if (c == kHomeKey)
			value = 0;
		else if (c == kEndKey)
			value = max;
		else
			value += (short) ((c == kPageUp ? -1 : 1) * (page > 1 ? page - 1 : 1));
		EdScrollTo(ed, value);
		return true;
	}
	return false;
}

long EdLength(Editor *ed)
{
	return (*ed->te)->teLength;
}

const char *EdLockText(Editor *ed)
{
	Handle h = (Handle) TEGetText(ed->te);

	HLock(h);
	return *h;
}

void EdUnlockText(Editor *ed)
{
	HUnlock((Handle) TEGetText(ed->te));
}

void EdGetSelection(Editor *ed, long *start, long *end)
{
	*start = (*ed->te)->selStart;
	*end = (*ed->te)->selEnd;
}

void EdSetSelection(Editor *ed, long start, long end)
{
	TESetSelect(start, end, ed->te);
}

Boolean EdReplace(Editor *ed, long start, long end, const char *text, long len)
{
	TEHandle te = ed->te;
	long selStart = (*te)->selStart, selEnd = (*te)->selEnd;

	if (EdLength(ed) - (end - start) + len > kMaxText)
		return false;
	/* typing and Delete through TEKey: it redraws only what changed */
	if (len == 1 && start == selStart && end == selEnd && (unsigned char) text[0] >= 0x20 && text[0] != 0x7F) {
		TEKey(text[0], te);
		return true;
	}
	if (len == 1 && text[0] == '\r' && start == selStart && end == selEnd) {
		TEKey('\r', te);
		return true;
	}
	if (len == 0 && selStart == selEnd && end == selEnd && start == end - 1) {
		TEKey(0x08, te);
		return true;
	}
	TESetSelect(start, end, te);
	if (end > start)
		TEDelete(te);
	if (len > 0)
		TEInsert((void *) text, len, te);
	TESetSelect(start + len, start + len, te);
	return true;
}

Boolean EdSetText(Editor *ed, const char *text, long len)
{
	if (len > kMaxText)
		return false;
	TESetText((void *) text, len, ed->te);
	TESetSelect(0, 0, ed->te);
	EdScrollTo(ed, 0);
	return true;
}

/* Lines in the text, counting an empty last line after a final return */
static short LineCount(Editor *ed)
{
	TEPtr t = *ed->te;
	short n = t->nLines;

	if (t->teLength > 0 && (*t->hText)[t->teLength - 1] == '\r')
		n++;
	return n;
}

void EdScrollInfo(Editor *ed, short *value, short *max, short *page)
{
	TEPtr t = *ed->te;
	short m;

	*page = PageLines(ed);
	*value = (short) ((t->viewRect.top - t->destRect.top) / t->lineHeight);
	m = (short) (LineCount(ed) - *page);
	*max = m < 0 ? 0 : m;
	if (*value > *max)
		*max = *value;          /* scrolled past the end by a deletion */
}

void EdScrollTo(Editor *ed, short value)
{
	short cur, max, page;

	EdScrollInfo(ed, &cur, &max, &page);
	if (value > max)
		value = max;
	if (value < 0)
		value = 0;
	if (value != cur)
		TEScroll(0, (short) ((cur - value) * LineHeight(ed)), ed->te);
}

void EdShowSelection(Editor *ed)
{
	TESelView(ed->te);
}

long EdVertical(Editor *ed, long pos, int lines, long *goal)
{
	TEPtr t;
	long line = EdLineOf(ed, pos) + lines;
	Point p;

	if (*goal < 0)
		*goal = LoWord(TEGetPoint((short) pos, ed->te));
	t = *ed->te;
	if (line > t->nLines - 1)
		line = t->nLines - 1;
	if (line < 0)
		line = 0;
	p.h = (short) *goal;
	p.v = (short) (t->destRect.top + line * (long) ed->height + ed->height / 2);
	return TEGetOffset(p, ed->te);
}

long EdLineOf(Editor *ed, long offset)
{
	TEPtr t = *ed->te;
	short i;

	for (i = 1; i < t->nLines; i++)
		if (t->lineStarts[i] > offset)
			return i - 1;
	return t->nLines > 0 ? t->nLines - 1 : 0;
}

/* TEToScrap and TEFromScrap are glue in Apple's libraries, not traps:
   TextEdit's private scrap to the Clipboard and back. Its handle and length
   are low-memory globals. */
#pragma GCC diagnostic ignored "-Warray-bounds"
void TEScrapToClipboard(void)
{
	Handle h = LMGetTEScrpHandle();

	(void) ZeroScrap();
	HLock(h);
	(void) PutScrap(LMGetTEScrpLength(), 'TEXT', *h);
	HUnlock(h);
}

Boolean TEScrapFromClipboard(void)
{
	long offset, len = GetScrap(LMGetTEScrpHandle(), 'TEXT', &offset);

	if (len < 0 || len > 32767) {
		LMSetTEScrpLength(0);
		return false;
	}
	LMSetTEScrpLength((short) len);
	return true;
}

/* Straight from the text: styled TextEdit's TECopy writes the Clipboard
   itself, styles and all, and a post wants plain text */
void EdCopySelection(Editor *ed)
{
	long s = (*ed->te)->selStart, e = (*ed->te)->selEnd;
	Handle h = (Handle) TEGetText(ed->te);

	(void) ZeroScrap();
	HLock(h);
	(void) PutScrap(e - s, 'TEXT', *h + s);
	HUnlock(h);
}

Handle EdClipboard(long *len)
{
	if (!TEScrapFromClipboard()) {
		*len = 0;
		return NULL;
	}
	*len = LMGetTEScrpLength();
	return LMGetTEScrpHandle();
}

/* ---- styles ---- */

/* Colors only on a color screen (util's ColorScreen); each is dark enough
   to come out black if the screen goes to black and white */
static const RGBColor kBlack = { 0, 0, 0 };
static const RGBColor kMarkColor = { 0x6000, 0x6000, 0x6000 };
static const RGBColor kCodeColor = { 0x0000, 0x6000, 0x0000 };
static const RGBColor kLinkColor = { 0x0000, 0x0000, 0xC000 };
static const RGBColor kHeadingColor = { 0x0000, 0x0000, 0x8000 };
static const RGBColor kMetaColor = { 0x7000, 0x3800, 0x0000 };

/* How a Markdown class looks */
static void ClassStyle(Editor *ed, int cls, ScrpSTElement *e)
{
	Style face = 0;

	memset(e, 0, sizeof(*e));
	if (cls & (MD_BOLD | MD_HEADING))
		face |= bold;
	if (cls & MD_ITALIC)
		face |= italic;
	if ((cls & (MD_LINK | MD_URL | MD_MARK)) == MD_LINK)
		face |= underline;
	if (cls & MD_URL)
		face |= condense;
	e->scrpFont = (cls & MD_CODE) ? kMonaco : ed->font;
	e->scrpFace = face;
	e->scrpSize = ed->size;
	e->scrpHeight = ed->height;
	e->scrpAscent = ed->ascent;
	e->scrpColor = kBlack;
	if (ColorScreen()) {
		if (cls & (MD_MARK | MD_URL))
			e->scrpColor = kMarkColor;
		else if (cls & MD_CODE)
			e->scrpColor = kCodeColor;
		else if (cls & MD_LINK)
			e->scrpColor = kLinkColor;
		else if (cls & MD_HEADING)
			e->scrpColor = kHeadingColor;
		else if (cls & MD_META)
			e->scrpColor = kMetaColor;
	}
}

static Boolean SameStyle(const ScrpSTElement *a, const ScrpSTElement *b)
{
	return a->scrpFont == b->scrpFont && a->scrpFace == b->scrpFace && a->scrpSize == b->scrpSize
		&& a->scrpColor.red == b->scrpColor.red && a->scrpColor.green == b->scrpColor.green
		&& a->scrpColor.blue == b->scrpColor.blue;
}

static Boolean SameAsTE(const STElement *st, const ScrpSTElement *e)
{
	return st->stFont == e->scrpFont && st->stFace == e->scrpFace && st->stSize == e->scrpSize
		&& st->stColor.red == e->scrpColor.red && st->stColor.green == e->scrpColor.green
		&& st->stColor.blue == e->scrpColor.blue;
}

/* Does TextEdit already show [from, to) in style e? run: a hint, the run to
   start looking from */
static Boolean Shows(TEHandle te, long from, long to, const ScrpSTElement *e, short *run)
{
	TEStyleHandle sh = TEGetStyleHandle(te);
	TEStylePtr sp = *sh;
	STPtr tab = *sp->styleTab;
	short i = *run;

	if (i >= sp->nRuns || sp->runs[i].startChar > from)
		i = 0;
	while (i + 1 < sp->nRuns && sp->runs[i + 1].startChar <= from)
		i++;
	*run = i;
	for (; i < sp->nRuns && sp->runs[i].startChar < to; i++)
		if (!SameAsTE(&tab[sp->runs[i].styleIndex], e))
			return false;
	return true;
}

/* The lines from from to to drawn again now, or to the bottom of the view
   when the text wrapped anew (to < 0) */
static void Redraw(Editor *ed, long from, long to)
{
	TEPtr t = *ed->te;
	Rect r = t->viewRect;
	long top = t->destRect.top + EdLineOf(ed, from) * (long) ed->height;
	RgnHandle clip;

	if (top > r.top)
		r.top = (short) top;
	if (to >= 0) {
		long bottom = t->destRect.top + (EdLineOf(ed, to > from ? to - 1 : from) + 1) * (long) ed->height;
		if (bottom < r.bottom)
			r.bottom = (short) bottom;
	}
	if (r.top >= r.bottom || (clip = NewRgn()) == NULL)
		return;
	GetClip(clip);
	ClipRect(&r);
	EraseRect(&r);
	TEUpdate(&r, ed->te);
	SetClip(clip);
	DisposeRgn(clip);
}

/* Restyles [from, to) by taking its text out and putting it back with the
   new styles. TEUseStyleScrap would add faces rather than set them, skip a
   scrap's last style, and leave the lines unwrapped; TECalText after it
   takes seconds on a long post on a 68000. Nothing shows until the lines
   are drawn again at the end. */
static void Reinsert(Editor *ed, long from, long to, StScrpHandle styles)
{
	TEHandle te = ed->te;
	long s = (*te)->selStart, e = (*te)->selEnd, len = to - from;
	short lines = (*te)->nLines;
	Boolean active = (*te)->active != 0;
	Ptr copy = NewPtr(len);
	RgnHandle clip = NewRgn();
	Rect none = { 0, 0, 0, 0 };

	if (copy == NULL || clip == NULL) {
		if (copy != NULL)
			DisposePtr(copy);
		if (clip != NULL)
			DisposeRgn(clip);
		return;
	}
	BlockMove(*(*te)->hText + from, copy, len);
	if (active)
		TEDeactivate(te);               /* the caret off while it can draw */
	GetClip(clip);
	ClipRect(&none);
	TEAutoView(false, te);
	TESetSelect(from, to, te);
	TEDelete(te);
	TEStyleInsert(copy, len, styles, te);
	TESetSelect(s, e, te);
	TEAutoView(true, te);
	SetClip(clip);
	DisposeRgn(clip);
	DisposePtr(copy);
	Redraw(ed, from, (*te)->nLines == lines ? to : -1);
	if (active)
		TEActivate(te);
}

void EdSetClasses(Editor *ed, long start, long end, const unsigned char *cls)
{
	StScrpHandle want, set;
	ScrpSTElement e, *w;
	long i, n = 0, cap = 16, from = -1, to = -1, runEnd;
	short run = 0, k, first = -1, last = -1;

	if (!ed->styled || end <= start)
		return;
	/* the runs wanted, merged where classes look the same */
	want = (StScrpHandle) NewHandle(sizeof(short) + sizeof(ScrpSTElement) * cap);
	if (want == NULL)
		return;
	for (i = start; i < end; i++) {
		if (i > start && cls[i - start] == cls[i - start - 1])
			continue;
		ClassStyle(ed, cls[i - start], &e);
		if (n > 0 && SameStyle(&(*want)->scrpStyleTab[n - 1], &e))
			continue;
		if (n == cap) {
			cap += 32;
			SetHandleSize((Handle) want, (long) sizeof(short) + (long) sizeof(ScrpSTElement) * cap);
			if (MemError() != noErr) {
				DisposeHandle((Handle) want);
				return;
			}
		}
		e.scrpStartChar = i;
		(*want)->scrpStyleTab[n++] = e;
	}
	(*want)->scrpNStyles = (short) n;
	/* the runs TextEdit doesn't show yet: restyling only those keeps the
	   rest from flickering */
	HLock((Handle) want);
	w = (*want)->scrpStyleTab;
	for (k = 0; k < n; k++) {
		runEnd = k + 1 < n ? w[k + 1].scrpStartChar : end;
		if (!Shows(ed->te, w[k].scrpStartChar, runEnd, &w[k], &run)) {
			if (first < 0) {
				first = k;
				from = w[k].scrpStartChar;
			}
			last = k;
			to = runEnd;
		}
	}
	HUnlock((Handle) want);
	if (first < 0) {
		DisposeHandle((Handle) want);
		return;
	}
	/* a scrap of just those, its offsets from the start of the range */
	set = (StScrpHandle) NewHandle(sizeof(short) + sizeof(ScrpSTElement) * (last - first + 1));
	if (set != NULL) {
		(*set)->scrpNStyles = (short) (last - first + 1);
		for (k = first; k <= last; k++) {
			(*set)->scrpStyleTab[k - first] = (*want)->scrpStyleTab[k];
			(*set)->scrpStyleTab[k - first].scrpStartChar -= from;
		}
		Reinsert(ed, from, to, set);
		DisposeHandle((Handle) set);
	}
	DisposeHandle((Handle) want);
}
