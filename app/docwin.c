/*
 * docwin.c: document windows (see docwin.h).
 *
 * The window: the editor fills it but for a scroll bar on the right and a
 * status line along the bottom, where a horizontal scroll bar would be
 * (the text wraps, so there is none). Every change goes through
 * DocReplace, which records it for undo before the editor makes it.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Errors.h>
#include <string.h>
#include <stdio.h>

#include "../core/frontmatter.h"
#include "../core/post.h"
#include "../core/textconv.h"
#include "../core/undo.h"
#include "docwin.h"
#include "postinfo.h"
#include "util.h"

#ifndef TOPICA_MAX_DOCS
#define TOPICA_MAX_DOCS 6
#endif
#ifndef TOPICA_UNDO_BYTES
#define TOPICA_UNDO_BYTES 32768L
#endif

enum { kScrollWidth = 16, kStatusHeight = 15 };
enum { kMinWidth = 200, kMinHeight = 120 };
enum { kStatusEvery = 30 };             /* ticks between word counts */

struct Doc {
	WindowPtr w;
	Editor *ed;
	ControlHandle vbar;
	UndoStack undo;
	Ptr undoBuf;
	FileRef file;
	Boolean hasFile;
	Boolean dirty;
	Boolean countDue;                   /* the words need counting */
	unsigned long countAt;              /* not before this tick */
	long words;
	long line;                          /* shown in the status line */
};

static Doc *gDocs[TOPICA_MAX_DOCS];
static short gUntitled;

short DocMaxCount(void)
{
	return TOPICA_MAX_DOCS;
}

short DocCount(void)
{
	short i, n = 0;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL)
			n++;
	return n;
}

Doc *DocFromWindow(WindowPtr w)
{
	short i;

	if (w == NULL)
		return NULL;
	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL && gDocs[i]->w == w)
			return gDocs[i];
	return NULL;
}

Doc *DocFront(void)
{
	return DocFromWindow(FrontWindow());
}

Editor *DocEditor(Doc *d)
{
	return d->ed;
}

Boolean DocIsDirty(Doc *d)
{
	return d->dirty;
}

Boolean DocHasFile(Doc *d)
{
	return d->hasFile;
}

void DocSealUndo(Doc *d)
{
	undo_seal(&d->undo);
}

/* ---- layout ---- */

static void ViewRect(Doc *d, Rect *r)
{
	*r = d->w->portRect;
	r->right -= kScrollWidth - 1;
	r->bottom -= kStatusHeight;
}

static void BarRect(Doc *d, Rect *r)
{
	*r = d->w->portRect;
	r->left = r->right - kScrollWidth + 1;
	r->top -= 1;
	r->right += 1;
	r->bottom -= kStatusHeight - 1;
}

static void StatusRect(Doc *d, Rect *r)
{
	*r = d->w->portRect;
	r->top = r->bottom - kStatusHeight + 1;
	r->right -= kScrollWidth - 1;
}

static void SyncScrollBar(Doc *d)
{
	short value, max, page;

	EdScrollInfo(d->ed, &value, &max, &page);
	SetControlMaximum(d->vbar, max);
	SetControlValue(d->vbar, value);
}

/* ---- the status line ---- */

static void DrawStatus(Doc *d)
{
	Rect r;
	char s[80];
	Str255 p;
	long words = d->words;

	StatusRect(d, &r);
	EraseRect(&r);
	TextFont(kGeneva);
	TextSize(9);
	if (words == 1)
		sprintf(s, "1 word");
	else
		sprintf(s, "%ld words", words);
	if (words > 0)
		sprintf(s + strlen(s), "   %ld min", post_minutes(words));   /* System 6's Geneva 9 has no middle dot */
	sprintf(s + strlen(s), "   Line %ld", d->line + 1);
	PStr(p, s);
	MoveTo((short) (r.left + 6), (short) (r.bottom - 4));
	DrawString(p);
	TextFont(kMonaco);
	TextSize(9);
}

static void CountWords(Doc *d)
{
	const char *t = EdLockText(d->ed);

	d->words = post_words(t, EdLength(d->ed));
	EdUnlockText(d->ed);
	d->countDue = false;
}

/* Redraws the status line if what it shows has changed */
static void UpdateStatus(Doc *d, Boolean force)
{
	long start, end, line, words = d->words;
	GrafPtr save;

	if (d->countDue && TickCount() >= d->countAt)
		CountWords(d);
	EdGetSelection(d->ed, &start, &end);
	line = EdLineOf(d->ed, end);
	if (!force && line == d->line && words == d->words)
		return;
	d->line = line;
	GetPort(&save);
	SetPort(d->w);
	DrawStatus(d);
	SetPort(save);
}

/* After any change: the scroll bar, and the words soon */
static void Changed(Doc *d)
{
	d->dirty = true;
	if (!d->countDue) {
		d->countDue = true;
		d->countAt = TickCount() + kStatusEvery;
	}
}

/* ---- making windows ---- */

static void SetTitle(Doc *d)
{
	if (d->hasFile) {
		SetWTitle(d->w, d->file.name);
	} else {
		char s[32];
		Str255 p;
		if (gUntitled <= 1)
			strcpy(s, "untitled");
		else
			sprintf(s, "untitled %d", gUntitled);
		PStr(p, s);
		SetWTitle(d->w, p);
	}
}

/* Where a new window goes: as big as the screen allows (at most a page's
   width), each one a little below and to the right of the last */
static void NewWindowRect(Rect *r)
{
	Rect s = qd.screenBits.bounds;
	short n = DocCount(), width;

	s.top += 20 + 18;                   /* the menu bar and a title bar */
	InsetRect(&s, 4, 4);
	width = s.right - s.left;
	if (width > 560)
		width = 560;
	SetRect(r, s.left, s.top, (short) (s.left + width), s.bottom);
	r->left += (short) (n * 12);
	r->top += (short) (n * 12);
	r->right += (short) (n * 12);
	if (r->right > s.right)
		OffsetRect(r, (short) (s.right - r->right), 0);
}

static Doc *MakeDoc(void)
{
	short slot;
	Doc *d;
	Rect r, v;

	for (slot = 0; slot < TOPICA_MAX_DOCS && gDocs[slot] != NULL; slot++)
		;
	if (slot == TOPICA_MAX_DOCS) {
		ShowNote("Close a window first. This copy of Topica opens only a few windows at a time.");
		return NULL;
	}
	d = (Doc *) NewPtrClear(sizeof(Doc));
	if (d == NULL)
		goto nomem;
	NewWindowRect(&r);
	d->w = NewWindow(NULL, &r, "\p", false, kZoomDocProc, (WindowPtr) -1L, true, 0);
	if (d->w == NULL)
		goto nomem;
	SetPort(d->w);
	ViewRect(d, &v);
	d->ed = EdNew(&v, kMonaco, 9);
	BarRect(d, &r);
	d->vbar = NewControl(d->w, &r, "\p", true, 0, 0, 0, scrollBarProc, 0);
	d->undoBuf = NewPtr(TOPICA_UNDO_BYTES);
	if (d->ed == NULL || d->vbar == NULL || d->undoBuf == NULL)
		goto nomem;
	undo_init(&d->undo, d->undoBuf, TOPICA_UNDO_BYTES);
	gDocs[slot] = d;
	return d;
nomem:
	if (d != NULL) {
		if (d->ed != NULL)
			EdDispose(d->ed);
		if (d->undoBuf != NULL)
			DisposePtr(d->undoBuf);
		if (d->w != NULL)
			DisposeWindow(d->w);
		DisposePtr((Ptr) d);
	}
	ErrorAlert("The window couldn't be opened.", memFullErr);
	return NULL;
}

static void ShowDoc(Doc *d)
{
	SetTitle(d);
	SyncScrollBar(d);
	CountWords(d);
	ShowWindow(d->w);
	SelectWindow(d->w);
}

Doc *DocNew(Boolean withTemplate)
{
	Doc *d = MakeDoc();
	char t[160];
	long n;

	if (d == NULL)
		return NULL;
	gUntitled++;
	if (withTemplate) {
		Str255 layout;
		char l[64] = "post";
		Handle h = GetResource('STR ', kLayoutString);
		if (h != NULL) {
			memcpy(layout, *h, (size_t) (**h + 1));
			CStr(l, layout);
		}
		n = post_template(l, '\r', t, sizeof(t));
		if (n > 0) {
			(void) EdSetText(d->ed, t, n);
			EdSetSelection(d->ed, n, n);
		}
	}
	ShowDoc(d);
	return d;
}

/* ---- files ---- */

static Boolean SameFile(const FileRef *a, const FileRef *b)
{
	return a->vRefNum == b->vRefNum && EqualString(a->name, b->name, false, true);
}

/* Reads a file into a doc's editor; 0 (after an alert) when it can't */
static Boolean Load(Doc *d, const FileRef *f)
{
	Ptr data, text;
	long len, n;
	OSErr err;
	int utf8;
	char what[200], name[64];

	CStr(name, f->name);
	sprintf(what, "\xD2%s\xD3 couldn't be opened.", name);
	SetCursor(*GetCursor(watchCursor));
	err = FileRead(f, &data, &len);
	if (err != noErr) {
		ErrorAlert(what, err);
		return false;
	}
	utf8 = tc_is_utf8(data, len);
	n = tc_to_mac(data, len, NULL, 0, utf8);
	if (n > EdMaxLength()) {
		DisposePtr(data);
		sprintf(what, "\xD2%s\xD3 is too long to open. Topica opens posts of up to %ld characters.",
			name, EdMaxLength());
		ShowNote(what);
		return false;
	}
	text = NewPtr(n > 0 ? n : 1);
	if (text == NULL) {
		DisposePtr(data);
		ErrorAlert(what, memFullErr);
		return false;
	}
	(void) tc_to_mac(data, len, text, n, utf8);
	DisposePtr(data);
	(void) EdSetText(d->ed, text, n);
	DisposePtr(text);
	undo_clear(&d->undo);
	d->dirty = false;
	InitCursor();
	return true;
}

Doc *DocOpen(const FileRef *f)
{
	short i;
	Doc *d;

	for (i = 0; i < TOPICA_MAX_DOCS; i++) {
		if (gDocs[i] != NULL && gDocs[i]->hasFile && SameFile(&gDocs[i]->file, f)) {
			SelectWindow(gDocs[i]->w);
			return gDocs[i];
		}
	}
	d = MakeDoc();
	if (d == NULL)
		return NULL;
	if (!Load(d, f)) {
		for (i = 0; i < TOPICA_MAX_DOCS; i++)
			if (gDocs[i] == d)
				gDocs[i] = NULL;
		EdDispose(d->ed);
		DisposePtr(d->undoBuf);
		DisposeWindow(d->w);
		DisposePtr((Ptr) d);
		return NULL;
	}
	d->file = *f;
	d->hasFile = true;
	ShowDoc(d);
	return d;
}

static Boolean Write(Doc *d, const FileRef *f)
{
	const char *t;
	long n, len = EdLength(d->ed);
	Ptr out;
	OSErr err;
	char what[128], name[64];

	CStr(name, f->name);
	sprintf(what, "\xD2%s\xD3 couldn't be saved.", name);
	t = EdLockText(d->ed);
	n = tc_to_disk(t, len, NULL, 0, 1, 1);
	out = NewPtr(n > 0 ? n : 1);
	if (out == NULL) {
		EdUnlockText(d->ed);
		ErrorAlert(what, memFullErr);
		return false;
	}
	(void) tc_to_disk(t, len, out, n, 1, 1);
	EdUnlockText(d->ed);
	SetCursor(*GetCursor(watchCursor));
	err = FileWrite(f, out, n);
	DisposePtr(out);
	InitCursor();
	if (err != noErr) {
		ErrorAlert(what, err);
		return false;
	}
	d->dirty = false;
	undo_seal(&d->undo);
	return true;
}

/* The name Save As suggests: the post's file name from today and its title */
static void SuggestName(Doc *d, char *out, int cap)
{
	char title[128];
	unsigned long secs;
	DateTimeRec dt;
	const char *t;

	if (d->hasFile) {
		CStr(out, d->file.name);
		return;
	}
	t = EdLockText(d->ed);
	(void) fm_get(t, EdLength(d->ed), "title", title, sizeof(title));
	EdUnlockText(d->ed);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"   /* the clock is a low-memory global */
	GetDateTime(&secs);
#pragma GCC diagnostic pop
	SecondsToDate(secs, &dt);
	/* a Mac with no clock battery wakes up in 1904: no date then */
	post_filename(dt.year >= 1990 ? dt.year : 0, dt.month, dt.day, title, 31, out, cap);
}

Boolean DocSaveAs(Doc *d)
{
	FileRef f;
	char name[64];

	SuggestName(d, name, sizeof(name));
	if (!FileAskSave("Save post as:", name, &f))
		return false;
	if (!Write(d, &f))
		return false;
	d->file = f;
	d->hasFile = true;
	SetTitle(d);
	return true;
}

Boolean DocSave(Doc *d)
{
	if (!d->hasFile)
		return DocSaveAs(d);
	return Write(d, &d->file);
}

void DocRevert(Doc *d)
{
	Str255 name;

	if (!d->hasFile || !d->dirty)
		return;
	memcpy(name, d->file.name, (size_t) d->file.name[0] + 1);
	ParamText(name, "\p", "\p", "\p");
	if (CautionAlert(kRevertAlert, NULL) != kOK)
		return;
	if (Load(d, &d->file)) {
		SetPort(d->w);
		InvalRect(&d->w->portRect);
		SyncScrollBar(d);
		CountWords(d);
	}
}

Boolean DocClose(Doc *d, Boolean quitting)
{
	short i;

	if (d->dirty) {
		Str255 name;
		GetWTitle(d->w, name);
		ParamText(name, quitting ? "\pquitting" : "\pclosing", "\p", "\p");
		SelectWindow(d->w);
		InitCursor();
		switch (CautionAlert(kSaveChangesAlert, NULL)) {
		case 1:                         /* Save */
			if (!DocSave(d))
				return false;
			break;
		case 2:                         /* Cancel */
			return false;
		}
	}
	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] == d)
			gDocs[i] = NULL;
	EdDispose(d->ed);
	DisposePtr(d->undoBuf);
	DisposeWindow(d->w);
	DisposePtr((Ptr) d);
	return true;
}

/* ---- editing ---- */

Boolean DocReplace(Doc *d, int kind, long start, long end, const char *text, long len)
{
	const char *t;

	if (EdLength(d->ed) - (end - start) + len > EdMaxLength()) {
		char msg[128];
		sprintf(msg, "That would make the post longer than %ld characters, the most Topica can edit.",
			EdMaxLength());
		ShowNote(msg);
		return false;
	}
	SetPort(d->w);
	t = EdLockText(d->ed);
	undo_record(&d->undo, kind, start, t + start, end - start, text, len);
	EdUnlockText(d->ed);
	(void) EdReplace(d->ed, start, end, text, len);
	EdShowSelection(d->ed);
	SyncScrollBar(d);
	Changed(d);
	return true;
}

Boolean DocHasSelection(Doc *d)
{
	long s, e;

	EdGetSelection(d->ed, &s, &e);
	return s != e;
}

int DocUndoKind(Doc *d, Boolean redo)
{
	return undo_peek(&d->undo, redo);
}

void DocUndo(Doc *d, Boolean redo)
{
	UndoApply a;

	SetPort(d->w);
	if (!undo_step(&d->undo, redo, &a))
		return;
	if (!EdReplace(d->ed, a.pos, a.pos + a.removeLen, a.insert, a.insertLen)) {
		SysBeep(10);
		return;
	}
	EdSetSelection(d->ed, a.selStart, a.selEnd);
	EdShowSelection(d->ed);
	SyncScrollBar(d);
	Changed(d);
}

void DocEdit(Doc *d, short cmd)
{
	long s, e, len;
	Handle h;

	SetPort(d->w);
	EdGetSelection(d->ed, &s, &e);
	switch (cmd) {
	case kEdCut:
		if (s == e)
			return;
		EdCopySelection(d->ed);
		(void) DocReplace(d, kUndoCut, s, e, "", 0);
		break;
	case kEdCopy:
		if (s != e)
			EdCopySelection(d->ed);
		break;
	case kEdPaste:
		h = EdClipboard(&len);
		if (h == NULL || len == 0) {
			SysBeep(10);
			return;
		}
		HLock(h);
		(void) DocReplace(d, kUndoPaste, s, e, *h, len);
		HUnlock(h);
		break;
	case kEdClear:
		if (s != e)
			(void) DocReplace(d, kUndoClear, s, e, "", 0);
		break;
	case kEdSelectAll:
		EdSetSelection(d->ed, 0, EdLength(d->ed));
		undo_seal(&d->undo);
		break;
	}
	undo_seal(&d->undo);
}

/* ---- events ---- */

void DocUpdate(Doc *d)
{
	SetPort(d->w);
	BeginUpdate(d->w);
	EdDraw(d->ed);
	DrawControls(d->w);
	DrawGrowIcon(d->w);
	DrawStatus(d);
	EndUpdate(d->w);
}

void DocActivate(Doc *d, Boolean active)
{
	SetPort(d->w);
	EdActivate(d->ed, active);
	HiliteControl(d->vbar, active ? 0 : 255);
	DrawGrowIcon(d->w);
	if (active)
		undo_seal(&d->undo);
}

void DocIdle(Doc *d)
{
	SetPort(d->w);
	EdIdle(d->ed);
	UpdateStatus(d, false);
}

static Doc *gScrolling;

static pascal void ScrollAction(ControlHandle c, short part)
{
	short value, max, page, delta;

	if (part == 0 || gScrolling == NULL)
		return;
	EdScrollInfo(gScrolling->ed, &value, &max, &page);
	switch (part) {
	case inUpButton: delta = -1; break;
	case inDownButton: delta = 1; break;
	case inPageUp: delta = (short) -(page > 1 ? page - 1 : 1); break;
	case inPageDown: delta = (short) (page > 1 ? page - 1 : 1); break;
	default: return;
	}
	EdScrollTo(gScrolling->ed, (short) (value + delta));
	SetControlValue(c, (short) (value + delta));
}

void DocClick(Doc *d, EventRecord *e)
{
	Point p = e->where;
	Rect v;
	ControlHandle c;
	short part;

	SetPort(d->w);
	GlobalToLocal(&p);
	undo_seal(&d->undo);
	part = FindControl(p, d->w, &c);
	if (part != 0 && c == d->vbar) {
		if (part == inThumb) {
			(void) TrackControl(c, p, NULL);
			EdScrollTo(d->ed, GetControlValue(c));
		} else {
			gScrolling = d;
			(void) TrackControl(c, p, (ControlActionUPP) ScrollAction);
			gScrolling = NULL;
		}
		SyncScrollBar(d);
		return;
	}
	ViewRect(d, &v);
	if (PtInRect(p, &v)) {
		EdClick(d->ed, p, (e->modifiers & shiftKey) != 0);
		SyncScrollBar(d);
		UpdateStatus(d, false);
	}
}

void DocKey(Doc *d, EventRecord *e)
{
	char c = (char) (e->message & charCodeMask);
	long s, en, len;

	SetPort(d->w);
	ObscureCursor();
	EdGetSelection(d->ed, &s, &en);
	len = EdLength(d->ed);
	if (EdNavigate(d->ed, c, e->modifiers)) {
		undo_seal(&d->undo);
	} else if (c == 0x08) {                 /* Delete */
		if (s != en)
			(void) DocReplace(d, kUndoDelete, s, en, "", 0);
		else if (s > 0)
			(void) DocReplace(d, kUndoDelete, s - 1, s, "", 0);
	} else if (c == 0x7F) {                 /* forward delete */
		if (s != en)
			(void) DocReplace(d, kUndoDelete, s, en, "", 0);
		else if (s < len)
			(void) DocReplace(d, kUndoDelete, s, s + 1, "", 0);
	} else if (c == 0x03 || c == '\r') {    /* Enter or Return */
		(void) DocReplace(d, kUndoTyping, s, en, "\r", 1);
	} else if (c == '\t' || (unsigned char) c >= 0x20) {
		(void) DocReplace(d, kUndoTyping, s, en, &c, 1);
	} else {
		return;
	}
	SyncScrollBar(d);
	UpdateStatus(d, false);
}

static void Resized(Doc *d)
{
	Rect r;

	SetPort(d->w);
	ViewRect(d, &r);
	EdSetView(d->ed, &r);
	BarRect(d, &r);
	MoveControl(d->vbar, r.left, r.top);
	SizeControl(d->vbar, (short) (r.right - r.left), (short) (r.bottom - r.top));
	SyncScrollBar(d);
	InvalRect(&d->w->portRect);
}

void DocGrow(Doc *d, Point where)
{
	Rect limits = qd.screenBits.bounds;
	long size;

	limits.left = kMinWidth;
	limits.top = kMinHeight;
	size = GrowWindow(d->w, where, &limits);
	if (size == 0)
		return;
	SizeWindow(d->w, LoWord(size), HiWord(size), true);
	Resized(d);
}

void DocZoom(Doc *d, Point where, short part)
{
	SetPort(d->w);
	if (!TrackBox(d->w, where, part))
		return;
	EraseRect(&d->w->portRect);
	ZoomWindow(d->w, part, false);
	Resized(d);
}

void DocAdjustCursor(Doc *d, Point where)
{
	Rect v;
	GrafPtr save;

	GetPort(&save);
	SetPort(d->w);
	GlobalToLocal(&where);
	ViewRect(d, &v);
	if (PtInRect(where, &v))
		SetCursor(*GetCursor(iBeamCursor));
	else
		InitCursor();
	SetPort(save);
}
