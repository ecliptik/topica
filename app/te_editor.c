/*
 * te_editor.c: the editor interface (editor.h) on TextEdit. TextEdit wraps
 * the text to the view's width and holds at most 32,767 characters; we
 * stop at 32,000 to leave it room.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Memory.h>
#include <Events.h>
#include <ToolUtils.h>
#include <string.h>

#include "editor.h"

struct Editor {
	TEHandle te;
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

Editor *EdNew(const Rect *view, short font, short size)
{
	Editor *ed = (Editor *) NewPtrClear(sizeof(Editor));
	Rect r;

	if (ed == NULL)
		return NULL;
	TextFont(font);
	TextSize(size);
	TextRect(view, &r);
	ed->te = TENew(&r, &r);
	if (ed->te == NULL) {
		DisposePtr((Ptr) ed);
		return NULL;
	}
	TEAutoView(true, ed->te);
	return ed;
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
	FontInfo fi;
	TEPtr t;

	TextFont(font);
	TextSize(size);
	GetFontInfo(&fi);
	t = *ed->te;
	t->txFont = font;
	t->txSize = size;
	t->fontAscent = fi.ascent;
	t->lineHeight = (short) (fi.ascent + fi.descent + fi.leading);
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

void EdCopySelection(Editor *ed)
{
	TECopy(ed->te);
	TEScrapToClipboard();
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
