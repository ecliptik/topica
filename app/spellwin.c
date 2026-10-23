/*
 * spellwin.c: Check Spelling (see spellwin.h).
 *
 * The dictionary, "Topica Dictionary" beside Topica (or in the Preferences
 * or System Folder), has the commonest words, always in memory, and all of
 * them: in memory when there's room, else read a block at a time, as on a
 * 512K Mac. The writer's own words are "Topica Words" in the Preferences or
 * System Folder, a word to a line, kept sorted.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>
#include <Multiverse.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

#include "../core/dict.h"
#include "../core/mdscan.h"
#include "../core/spell.h"
#include "../core/undo.h"
#include "docwin.h"
#include "editor.h"
#include "fileio.h"
#include "spellwin.h"
#include "util.h"

#define kDictName "Topica Dictionary"
#define kWordsName "Topica Words"

enum {
	kReplaceButton = 1, kDoneButton = 2, kOutline = 3, kSkipButton = 4, kSkipAllButton = 5,
	kLearnButton = 6, kWordText = 8, kChangeField = 10, kList = 11
};
enum { kSlots = 12, kSuggest = 8, kSkipMax = 32, kContext = 16 };

static Boolean gLoaded;
static Dict gCommon, gMain;
static short gDictRef;                  /* the dictionary, open while it's read a block at a time */
static char **gUser;                    /* the writer's words, each NUL-ended */
static long **gUserAt;                  /* where each starts, in sorted order */
static long gUserCount;
static char gSkipped[kSkipMax][DICT_WORD];
static int gSkippedCount;
static ListHandle gList;

/* ---- the dictionary ---- */

static unsigned long Get32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | p[3];
}

static long ReadBlock(void *ctx, long offset, unsigned char *buf, long len)
{
	long got;

	(void) ctx;
	return FileReadAt(gDictRef, offset, buf, len, &got) == noErr ? got : -1;
}

/* [offset, offset + len) of the file, in a new pointer */
static Ptr ReadPart(short ref, long offset, long len)
{
	Ptr p = NewPtr(len > 0 ? len : 1);
	long got;

	if (p != NULL && (FileReadAt(ref, offset, p, len, &got) != noErr || got != len)) {
		DisposePtr(p);
		p = NULL;
	}
	return p;
}

/* A section at offset: whole in memory, or its index and a cache */
static Boolean OpenSection(short ref, long offset, long len, Dict *d, Boolean whole)
{
	unsigned char head[DICT_HEADER];
	long blocks, words, ilen, got;
	Ptr index, entries, data;

	if (FileReadAt(ref, offset, head, DICT_HEADER, &got) != noErr || got != DICT_HEADER
			|| !dict_header(head, &blocks, &words, &ilen))
		return false;
	index = ReadPart(ref, offset + DICT_HEADER, ilen);
	entries = NewPtr(blocks * (long) sizeof(long));
	if (index == NULL || entries == NULL
			|| !dict_init(d, (unsigned char *) index, ilen, blocks, words, (long *) entries))
		return false;
	if (whole) {
		data = ReadPart(ref, offset + DICT_HEADER + ilen, len - DICT_HEADER - ilen);
		if (data == NULL)
			return false;
		dict_data(d, (unsigned char *) data);
	} else {
		Ptr cache = NewPtr(kSlots * d->maxBlock), cached = NewPtr(kSlots * (long) sizeof(long)),
			used = NewPtr(kSlots * (long) sizeof(unsigned long));
		if (cache == NULL || cached == NULL || used == NULL)
			return false;
		dict_reader(d, ReadBlock, NULL, offset + DICT_HEADER + ilen, (unsigned char *) cache, kSlots,
			(long *) cached, (unsigned long *) used);
	}
	return true;
}

/* ---- the writer's words ---- */

static int Compare(const char *a, int alen, const char *b, int blen)
{
	int n = alen < blen ? alen : blen, c = memcmp(a, b, (size_t) n);

	return c != 0 ? c : alen - blen;
}

/* Where the word is in the sorted list, or where it would go (*found 0) */
static long UserFind(const char *w, int len, Boolean *found)
{
	long lo = 0, hi = gUserCount;

	*found = false;
	while (lo < hi) {
		long mid = (lo + hi) / 2;
		const char *u = *gUser + (*gUserAt)[mid];
		int c = Compare(u, (int) strlen(u), w, len);
		if (c == 0) {
			*found = true;
			return mid;
		}
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* Adds a word in memory: 0 when there's no room */
static Boolean UserAdd(const char *w, int len)
{
	long size = GetHandleSize(gUser), at;
	Boolean found;

	at = UserFind(w, len, &found);
	if (found)
		return true;
	SetHandleSize(gUser, size + len + 1);
	if (GetHandleSize(gUser) != size + len + 1)
		return false;
	SetHandleSize((Handle) gUserAt, (gUserCount + 1) * (long) sizeof(long));
	if (GetHandleSize((Handle) gUserAt) != (gUserCount + 1) * (long) sizeof(long))
		return false;
	memcpy(*gUser + size, w, (size_t) len);
	(*gUser)[size + len] = '\0';
	memmove(*gUserAt + at + 1, *gUserAt + at, (size_t) (gUserCount - at) * sizeof(long));
	(*gUserAt)[at] = size;
	gUserCount++;
	return true;
}

static void LoadUserWords(void)
{
	FileRef f;
	Ptr data;
	long len, i, s;

	gUser = NewHandle(0);
	gUserAt = (long **) NewHandle(0);
	gUserCount = 0;
	if (gUser == NULL || gUserAt == NULL)
		return;
	if (FilePrefsFolder(kWordsName, &f) != noErr || FileRead(&f, &data, &len) != noErr)
		return;
	for (i = s = 0; i <= len; i++) {
		if (i == len || data[i] == '\r' || data[i] == '\n') {
			if (i > s && i - s < DICT_WORD)
				(void) UserAdd(data + s, (int) (i - s));
			s = i + 1;
		}
	}
	DisposePtr(data);
}

/* Learn: the word in memory and in the file, sorted */
static void Learn(const char *w, int len)
{
	FileRef f;
	Ptr out;
	long i, n = 0, size;

	if (!UserAdd(w, len))
		return;
	size = GetHandleSize(gUser);
	out = NewPtr(size > 0 ? size : 1);
	if (out == NULL)
		return;
	for (i = 0; i < gUserCount; i++) {
		const char *u = *gUser + (*gUserAt)[i];
		long k = (long) strlen(u);
		memcpy(out + n, u, (size_t) k);
		n += k;
		out[n++] = '\r';
	}
	if (FilePrefsFolder(kWordsName, &f) == noErr)
		(void) FileWrite(&f, out, n, 'TEXT', 'ttxt');
	DisposePtr(out);
}

static int UserKnows(void *ctx, const char *w, int len)
{
	Boolean found;
	int i;

	(void) ctx;
	for (i = 0; i < gSkippedCount; i++)
		if ((int) strlen(gSkipped[i]) == len && memcmp(gSkipped[i], w, (size_t) len) == 0)
			return 1;
	if (gUser == NULL || gUserCount == 0)
		return 0;
	(void) UserFind(w, len, &found);
	return found;
}

static Boolean Load(void)
{
	FileRef f;
	unsigned char head[16];
	long got, commonLen, mainLen;
	short ref;
	Boolean whole;

	if (gLoaded)
		return true;
	FileBesideApp(kDictName, &f);
	if (FileOpenRead(&f, &ref) != noErr && (FilePrefsFolder(kDictName, &f) != noErr || FileOpenRead(&f, &ref) != noErr)) {
		ShowNote("To check spelling, put \xD2Topica Dictionary\xD3 in Topica\xD5s folder.");
		return false;
	}
	SetCursor(*GetCursor(watchCursor));
	if (FileReadAt(ref, 0, head, 16, &got) != noErr || got != 16 || memcmp(head, "TPSP", 4) != 0) {
		FileClose(ref);
		InitCursor();
		ShowNote("\xD2Topica Dictionary\xD3 is damaged, or from another version of Topica.");
		return false;
	}
	commonLen = (long) Get32(head + 8);
	mainLen = (long) Get32(head + 12);
	/* all the words in memory when there's plenty of it; else a block at a
	   time, as Topica Lite always does */
#ifdef TOPICA_LITE
	whole = false;
#else
	whole = MaxBlock() > mainLen + 96 * 1024L;
#endif
	if (!OpenSection(ref, 16, commonLen, &gCommon, true) || !OpenSection(ref, 16 + commonLen, mainLen, &gMain, whole)) {
		FileClose(ref);
		InitCursor();
		ErrorAlert("The dictionary couldn\xD5t be opened.", memFullErr);
		return false;
	}
	DLOG(("Spelling: dictionary %s", whole ? "in memory" : "read a block at a time"));
	if (whole)
		FileClose(ref);
	else
		gDictRef = ref;
	LoadUserWords();
	gLoaded = true;
	InitCursor();
	return true;
}

/* ---- the dialog ---- */

static pascal void DrawList(DialogPtr d, short item)
{
	short type;
	Handle h;
	Rect r;

	GetDialogItem(d, item, &type, &h, &r);
	FrameRect(&r);
	if (gList != NULL)
		LUpdate(d->visRgn, gList);
}

static void ListRect(DialogPtr d, Rect *r)
{
	short type;
	Handle h;

	GetDialogItem(d, kList, &type, &h, r);
}

static pascal Boolean SpellFilter(DialogPtr d, EventRecord *e, short *item)
{
	Rect r;
	Point p;

	if (e->what == mouseDown && gList != NULL) {
		GrafPtr save;
		GetPort(&save);
		SetPort(d);
		p = e->where;
		GlobalToLocal(&p);
		ListRect(d, &r);
		if (PtInRect(p, &r)) {
			Boolean twice = LClick(p, e->modifiers, gList);
			Cell c;
			c.h = c.v = 0;
			if (LGetSelect(true, &c, gList)) {
				char s[DICT_WORD + 2];
				short len = DICT_WORD;
				LGetCell(s, &len, c, gList);
				s[len] = '\0';
				SetItemText(d, kChangeField, s);
				SelectDialogItemText(d, kChangeField, 0, 32767);
			}
			SetPort(save);
			*item = twice ? kReplaceButton : kList;
			return true;
		}
		SetPort(save);
	}
	return StandardFilter(d, e, item);
}

static void FillList(char sug[][DICT_WORD], int n)
{
	Cell c;
	Rect r;
	int i;

	LSetDrawingMode(false, gList);
	if ((**gList).dataBounds.bottom > 0)
		LDelRow(0, 0, gList);
	if (n > 0)
		(void) LAddRow((short) n, 0, gList);
	c.h = 0;
	for (i = 0; i < n; i++) {
		c.v = (short) i;
		LSetCell(sug[i], (short) strlen(sug[i]), c, gList);
	}
	if (n > 0) {
		c.v = 0;
		LSetSelect(true, c, gList);
	}
	LSetDrawingMode(true, gList);
	/* the update draws only the rows there are: erase the old ones */
	r = (**gList).rView;
	EraseRect(&r);
}

/* The word in its line: "...he said \322wrod\323 and..." */
static void Context(const char *t, long n, long at, int len, char *out)
{
	long a = at, b = at + len, k;
	char *o = out;

	while (a > 0 && at - a < kContext && t[a - 1] != '\r' && t[a - 1] != '\n')
		a--;
	while (b < n && b - at - len < kContext && t[b] != '\r' && t[b] != '\n')
		b++;
	if (a > 0 && t[a - 1] != '\r' && t[a - 1] != '\n')
		*o++ = (char) 0xC9;             /* an ellipsis */
	for (k = a; k < at; k++)
		*o++ = t[k] == '\t' ? ' ' : t[k];
	*o++ = (char) 0xD2;
	memcpy(o, t + at, (size_t) len);
	o += len;
	*o++ = (char) 0xD3;
	for (k = at + len; k < b; k++)
		*o++ = t[k] == '\t' ? ' ' : t[k];
	if (b < n && t[b] != '\r' && t[b] != '\n')
		*o++ = (char) 0xC9;
	*o = '\0';
}

static DialogPtr MakeDialog(void)
{
	DialogPtr d = GetModalDialog(kSpellDialog);
	short type;
	Handle h;
	Rect r, bounds;
	Point cell;
	short width, height;

	if (d == NULL)
		return NULL;
	/* along the bottom of the screen, below the word */
	width = (short) (d->portRect.right - d->portRect.left);
	height = (short) (d->portRect.bottom - d->portRect.top);
	MoveWindow(d, (short) ((qd.screenBits.bounds.right - width) / 2), (short) (qd.screenBits.bounds.bottom - height - 8), false);
	OutlineDefault(d, kOutline);
	GetDialogItem(d, kList, &type, &h, &r);
	SetDialogItem(d, kList, userItem, (Handle) NewUserItemUPP(DrawList), &r);
	SetPort(d);
	InsetRect(&r, 1, 1);
	r.right -= 15;
	SetRect(&bounds, 0, 0, 1, 0);
	cell.h = cell.v = 0;
	gList = LNew(&r, &bounds, cell, 0, (WindowPtr) d, true, false, false, true);
	if (gList == NULL) {
		DisposeDialog(d);
		return NULL;
	}
	(**gList).selFlags = lOnlyOne;
	return d;
}

/* ---- checking ---- */

void SpellCheck(Doc *d)
{
	Editor *ed = DocEditor(d);
	Speller sp;
	DialogPtr dlg = NULL;
	unsigned char *cls;
	char sug[kSuggest][DICT_WORD], word[DICT_WORD], change[256], context[3 * kContext + DICT_WORD + 8];
	const char *t;
	long n, cap, pos, start, end, at, stop;
	int len, count, wrong = 0;
	short item = 0;
	Boolean wrapped = false, done = false;
	MdState st;

	if (!Load())
		return;
	sp.common = &gCommon;
	sp.main = &gMain;
	sp.user = UserKnows;
	sp.ctx = NULL;
	gSkippedCount = 0;
	n = EdLength(ed);
	cap = n + 1024;
	cls = (unsigned char *) NewPtr(cap);
	if (cls == NULL) {
		ErrorAlert("The spelling couldn\xD5t be checked.", memFullErr);
		return;
	}
	SetCursor(*GetCursor(watchCursor));
	t = EdLockText(ed);
	md_begin(&st);
	md_classify(&st, t, n, n, cls);
	sp_prepare(t, n, cls);
	EdGetSelection(ed, &start, &end);
	pos = start;
	stop = n;
	while (!done) {
		at = sp_next(t, n, pos, cls, &len);
		if (at < 0 || at >= stop) {
			if (wrapped || start == 0)
				break;
			/* from the top, back to where it began */
			wrapped = true;
			pos = 0;
			stop = start;
			continue;
		}
		pos = at + len;
		if (len >= DICT_WORD || sp_correct(&sp, t + at, len))
			continue;
		/* one to ask about */
		wrong++;
		memcpy(word, t + at, (size_t) len);
		word[len] = '\0';
		Context(t, n, at, len, context);
#ifdef TOPICA_DEBUG
		{
			unsigned long t0 = TickCount();
			count = sp_suggest(&sp, word, len, sug, kSuggest);
			DLOG(("Spelling: %s, %d suggestions in %ld ticks", word, count, (long) (TickCount() - t0)));
		}
#else
		count = sp_suggest(&sp, word, len, sug, kSuggest);
#endif
		EdUnlockText(ed);
		SetPort(DocWindow(d));
		EdSetSelection(ed, at, at + len);
		EdShowSelection(ed);
		if (dlg == NULL) {
			dlg = MakeDialog();
			if (dlg == NULL) {
				SysBeep(10);
				break;
			}
			ShowWindow(dlg);
		}
		SetPort(dlg);
		SetItemText(dlg, kWordText, context);
		SetItemText(dlg, kChangeField, count > 0 ? sug[0] : word);
		FillList(sug, count);
		InvalRect(&dlg->portRect);
		SelectDialogItemText(dlg, kChangeField, 0, 32767);
		InitCursor();
		do
			ModalDialog((ModalFilterUPP) SpellFilter, &item);
		while (item != kReplaceButton && item != kDoneButton && item != kSkipButton && item != kSkipAllButton
			&& item != kLearnButton);
		SetCursor(*GetCursor(watchCursor));
		switch (item) {
		case kReplaceButton: {
			long clen, delta;
			GetItemText(dlg, kChangeField, change, (short) sizeof(change));
			clen = (long) strlen(change);
			delta = clen - len;
			if (n + delta > cap || !DocReplace(d, kUndoReplace, at, at + len, change, clen)) {
				done = true;
				break;
			}
			/* the classes after it move with the text */
			memmove(cls + at + clen, cls + at + len, (size_t) (n - at - len));
			memset(cls + at, 0, (size_t) clen);
			n += delta;
			pos = at + clen;
			if (stop > at)
				stop += delta;
			if (start > at)
				start += delta;
			break;
		}
		case kSkipAllButton:
			if (gSkippedCount < kSkipMax)
				strcpy(gSkipped[gSkippedCount++], word);
			break;
		case kLearnButton:
			Learn(word, len);
			break;
		case kDoneButton:
			done = true;
			break;
		}
		t = EdLockText(ed);
	}
	EdUnlockText(ed);
	if (gList != NULL) {
		LDispose(gList);
		gList = NULL;
	}
	if (dlg != NULL)
		DisposeDialog(dlg);
	DisposePtr((Ptr) cls);
	InitCursor();
	if (gMain.failed) {
		gMain.failed = 0;
		ShowNote("Topica couldn\xD5t read all of its dictionary, so some words weren\xD5t checked.");
	} else if (!done) {
		ShowNote(wrong == 0 ? "Topica found no spelling mistakes." : "The spelling check is done.");
	}
}
