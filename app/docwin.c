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

#include "../core/fmt.h"
#include "../core/frontmatter.h"
#include "../core/mdscan.h"
#include "../core/post.h"
#include "../core/recovery.h"
#include "../core/textconv.h"
#include "../core/undo.h"
#include "../core/vi.h"
#include "docwin.h"
#include "findwin.h"
#include "postinfo.h"
#include "preview.h"
#include "settings.h"
#include "textdrag.h"
#include "publishwin.h"
#include "theme.h"
#include "util.h"

#ifndef TOPICA_MAX_DOCS
#define TOPICA_MAX_DOCS 6
#endif
#ifndef TOPICA_UNDO_BYTES
#define TOPICA_UNDO_BYTES 32768L
#endif
#ifdef TOPICA_LITE
#define kViRegister 4096L               /* what vi's y and d can hold */
#else
#define kViRegister 32000L
#endif

enum { kScrollWidth = 16, kStatusHeight = 15 };
enum { kDivider = 5 };                  /* the pane's edge that drags */
enum { kMinWidth = 200, kMinHeight = 120 };
enum { kStatusEvery = 30 };             /* ticks between word counts */
enum { kRecoveryPause = 120 };          /* ticks of quiet before a recovery copy */
enum { kStylePause = 30 };              /* ticks of quiet before restyling */

#define kRecoveryFolder "Topica Recovery"

struct Doc {
	WindowPtr w;
	short pane;                         /* the preview's pane on the right: its width, or 0 */
	short paneMin;
	char viCmd;                         /* a :w :q ... for after the key: w W(q) x q Q(q!) */
	Boolean blockOn;                    /* vi's block cursor is drawn, over block */
	Boolean teOff;                      /* TextEdit deactivated for it (no caret) */
	Rect block;
	long id;                            /* for Apple events: unique while Topica runs */
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
	Str63 untitled;                     /* the title while it has no file */
	FileRef rec;                        /* its recovery copy, when it has one */
	Boolean hasRec;
	Boolean recDue;                     /* changed since the last copy */
	unsigned long changedAt, recAt;     /* ticks */
	Boolean styleDue;                   /* [styleFrom, styleTo) needs restyling */
	long styleFrom, styleTo;
	long styleNext;                     /* a long restyle's next slice, -1: the screen's first */
	long styleOrigin;                   /* where its first slice began */
	MdState styleState;                 /* the scan at styleNext, kept between slices */
	unsigned long styleAt;              /* not before this tick */
	unsigned long blocks;               /* md_blocks when last restyled */
	unsigned long changes;              /* counts every change, for the preview */
	Boolean editDue;                    /* [editFrom, editTo) changed since the preview looked, */
	long editFrom, editTo, editDelta;   /* moving what follows by editDelta */
	Vi vi;                              /* vi mode's state */
	const char *viText;                 /* the text, locked while vi reads it */
	char viShown[20];                   /* the mode the status line shows */
};

static void Resized(Doc *d);
static void DragDivider(Doc *d);
static void ViClicked(Doc *d);
static void ViSettings(Doc *d);

static Doc *gDocs[TOPICA_MAX_DOCS];
static ViReg gViReg;                    /* one register for every window, as vi has */
static char gAppliedFont[32];           /* the font DocApplySettings last set */
static short gAppliedSize;
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

WindowPtr DocWindow(Doc *d)
{
	return d->w;
}

unsigned long DocChanges(Doc *d)
{
	return d->changes;
}

Boolean DocTakeEdits(Doc *d, long *from, long *to, long *delta)
{
	Boolean due = d->editDue;

	*from = d->editFrom;
	*to = d->editTo;
	*delta = d->editDelta;
	d->editDue = false;
	d->editDelta = 0;
	return due;
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

Boolean DocFile(Doc *d, FileRef *f)
{
	if (d->hasFile)
		*f = d->file;
	return d->hasFile;
}

void DocSealUndo(Doc *d)
{
	undo_seal(&d->undo);
}

/* ---- layout ---- */

/* The post's half of the window: all of it, or left of the preview's pane */
static void PostArea(Doc *d, Rect *r)
{
	*r = d->w->portRect;
	r->right -= d->pane;
}

static void ViewRect(Doc *d, Rect *r)
{
	PostArea(d, r);
	r->right -= kScrollWidth - 1;
	r->bottom -= kStatusHeight;
}

void DocTextRect(Doc *d, Rect *r)
{
	ViewRect(d, r);
}

void DocGroupEdits(Doc *d, Boolean begin)
{
	undo_seal(&d->undo);
	undo_group(&d->undo, begin);
}

void DocJoinEdits(Doc *d, short kind, Boolean begin)
{
	undo_seal(&d->undo);
	if (!begin)
		undo_group(&d->undo, 0);
	else if (undo_peek(&d->undo, 0) == kind)
		undo_join(&d->undo);
}

static void BarRect(Doc *d, Rect *r)
{
	PostArea(d, r);
	r->left = r->right - kScrollWidth + 1;
	r->top -= 1;
	r->right += 1;
	r->bottom -= kStatusHeight - 1;
}

/* Right of the post's scroll bar, above the status line, which runs under
   both */
void DocPaneRect(Doc *d, Rect *r)
{
	*r = d->w->portRect;
	r->left = (short) (r->right - d->pane + 1);
	r->bottom -= kStatusHeight;
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
	GrafPtr save;

	EdScrollInfo(d->ed, &value, &max, &page);
	GetPort(&save);
	SetPort(d->w);
	ThemePlain();                       /* the scroll bar draws in the port's colors */
	SetControlMaximum(d->vbar, max);
	SetControlValue(d->vbar, value);
	ThemePort();
	SetPort(save);
}

/* ---- the status line ---- */

static void DrawStatus(Doc *d)
{
	Rect r;
	char s[80];
	Str255 p;
	long words = d->words;
	const char *step;

	StatusRect(d, &r);
	ThemePort();                        /* TextEdit leaves its last run's color */
	EraseRect(&r);
	TextFont(kGeneva);
	TextSize(9);
	if (words == 1)
		tp_sprintf(s, "1 word");
	else
		tp_sprintf(s, "%ld words", words);
	if (words > 0)
		tp_sprintf(s + strlen(s), "   %ld min", post_minutes(words));   /* System 6's Geneva 9 has no middle dot */
	tp_sprintf(s + strlen(s), "   Line %ld", d->line + 1);
	if ((step = PublishStatus(d->id)) != NULL)
		tp_sprintf(s + strlen(s), "   %s", step);     /* Publish, in the background */
	PStr(p, s);
	MoveTo((short) (r.left + 6), (short) (r.bottom - 4));
	DrawString(p);
	strcpy(d->viShown, vi_status(&d->vi));
	if (d->viShown[0]) {
		PStr(p, d->viShown);
		MoveTo((short) (r.right - 6 - StringWidth(p)), (short) (r.bottom - 4));
		DrawString(p);
	}
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

void DocShowStatus(Doc *d)
{
	GrafPtr save;

	GetPort(&save);
	SetPort(d->w);
	DrawStatus(d);
	SetPort(save);
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
	if (!force && line == d->line && words == d->words && strcmp(d->viShown, vi_status(&d->vi)) == 0)
		return;
	d->line = line;
	GetPort(&save);
	SetPort(d->w);
	DrawStatus(d);
	SetPort(save);
}

void DocShowSelection(Doc *d)
{
	GrafPtr save;

	GetPort(&save);
	SetPort(d->w);
	EdShowSelection(d->ed);
	SyncScrollBar(d);
	UpdateStatus(d, false);
	SetPort(save);
}

/* After any change: the scroll bar, and the words soon. Undone (or redone)
   back to the saved text, the post is unchanged again. */
static void Changed(Doc *d)
{
	d->changes++;
	d->dirty = !undo_at_saved(&d->undo);
	d->recDue = true;
	d->changedAt = TickCount();
	if (!d->countDue) {
		d->countDue = true;
		d->countAt = TickCount() + kStatusEvery;
	}
}

/* ---- making windows ---- */

static void SetTitle(Doc *d)
{
	SetWTitle(d->w, d->hasFile ? d->file.name : d->untitled);
}

static void NameUntitled(Doc *d)
{
	char s[32];
	Str255 p;

	gUntitled++;
	if (gUntitled <= 1)
		strcpy(s, "untitled");
	else
		tp_sprintf(s, "untitled %d", gUntitled);
	PStr(p, s);
	memcpy(d->untitled, p, (size_t) p[0] + 1);
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

static long gLastId;

long DocId(Doc *d)
{
	return d->id;
}

Doc *DocFromId(long id)
{
	short i;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL && gDocs[i]->id == id)
			return gDocs[i];
	return NULL;
}

Doc *DocNth(short n)
{
	WindowPtr w;
	Doc *d;

	for (w = FrontWindow(); w != NULL; w = (WindowPtr) ((WindowPeek) w)->nextWindow)
		if ((d = DocFromWindow(w)) != NULL && --n == 0)
			return d;
	return NULL;
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
	if (d != NULL)
		d->id = ++gLastId;
	if (d == NULL)
		goto nomem;
	NewWindowRect(&r);
	/* a color port where there's Color QuickDraw: an old one shows only
	   QuickDraw's eight colors */
	if (HasColorQD())
		d->w = NewCWindow(NULL, &r, "\p", false, kZoomDocProc, (WindowPtr) -1L, true, 0);
	else
		d->w = NewWindow(NULL, &r, "\p", false, kZoomDocProc, (WindowPtr) -1L, true, 0);
	if (d->w == NULL)
		goto nomem;
	SetPort(d->w);
	ThemeWindow(d->w);                  /* before TextEdit takes the port's colors */
	ViewRect(d, &v);
	d->ed = EdNew(&v, SettingsFontNumber(), gPrefs.size);
	BarRect(d, &r);
	d->vbar = NewControl(d->w, &r, "\p", true, 0, 0, 0, scrollBarProc, 0);
	d->undoBuf = NewPtr(TOPICA_UNDO_BYTES);
	if (d->ed == NULL || d->vbar == NULL || d->undoBuf == NULL)
		goto nomem;
	undo_init(&d->undo, d->undoBuf, TOPICA_UNDO_BYTES);
	if (gViReg.buf == NULL) {
		gViReg.buf = NewPtr(kViRegister);
		gViReg.cap = gViReg.buf != NULL ? kViRegister : 0;
		strcpy(gAppliedFont, gPrefs.font);
		gAppliedSize = gPrefs.size;
	}
	vi_init(&d->vi, &gViReg);
	d->vi.on = gPrefs.vi;
	d->vi.jk = gPrefs.viJk;
	DragInstall(d->w);
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
	ErrorAlert("The window couldn\xD5t be opened.", memFullErr);
	return NULL;
}

/* ---- styles ---- */

/* Adds an edit (at start, oldEnd - start characters became newLen) to a
   range of changed text, [*from, *to) in the text as it was before it;
   due: there was a range already */
static void Merge(Boolean due, long *from, long *to, long start, long oldEnd, long newLen)
{
	long delta = newLen - (oldEnd - start), f = start, t = start + newLen, a, b;

	if (due) {
		a = *from >= oldEnd ? *from + delta : *from > start ? start : *from;
		b = *to >= oldEnd ? *to + delta : *to > start ? start : *to;
		if (a < f)
			f = a;
		if (b > t)
			t = b;
	}
	*from = f;
	*to = t;
}

/* An edit replaced [start, oldEnd) with newLen characters: restyle there,
   along with what was waiting, and tell the preview */
static void Touch(Doc *d, long start, long oldEnd, long newLen)
{
	Merge(d->styleDue, &d->styleFrom, &d->styleTo, start, oldEnd, newLen);
	d->styleDue = true;
	d->styleNext = -1;
	d->styleAt = TickCount() + kStylePause;
	Merge(d->editDue, &d->editFrom, &d->editTo, start, oldEnd, newLen);
	d->editDelta += newLen - (oldEnd - start);
	d->editDue = true;
}

static void TouchAll(Doc *d)
{
	d->styleFrom = 0;
	d->styleTo = EdLength(d->ed);
	d->styleDue = true;
	d->styleNext = -1;
	d->styleAt = TickCount();
}

static void BlockOff(Doc *d);

/* Classes for the lines that changed; all of the rest of the post when a
   code fence or the front matter's end moved */
enum { kStyleSlice = 1024 };            /* characters restyled in one go while idle (~1 s on a 68000) */

static void Restyle(Doc *d)
{
	Editor *ed = d->ed;
	long n = EdLength(ed), from = d->styleFrom, to = d->styleTo;
	const char *t;
	unsigned long blocks;
	MdState s;
	Ptr buf;
	Boolean whole;

	d->styleDue = false;
	if (!EdCanStyle(ed))
		return;
	SetPort(d->w);
	if (from > n)
		from = n;
	if (to > n)
		to = n;
	t = EdLockText(ed);
	if (d->styleNext >= 0 && d->styleState.pos == d->styleNext && gPrefs.format) {
		/* the next slice of a long restyle: nothing changed since the last */
		s = d->styleState;
	} else {
		blocks = md_blocks(t, n);
		if (blocks != d->blocks) {
			d->blocks = blocks;
			to = n;
		}
		md_begin(&s);
		d->styleState.pos = -1;
	}
	from = s.pos > from ? from : md_seek(&s, t, n, from);
	to = md_line_after(t, n, to > from ? to - 1 : from);
	if (to - from > kStyleSlice && gPrefs.format) {   /* plain text costs little: one go */
		/* a long range (a post just opened, formatting turned on): the
		   screen's lines first, then the rest a slice at a time while idle,
		   so the post can be read and typed in meanwhile (styled TextEdit
		   takes ~0.035 ticks a character on a 68000) */
		long start = d->styleNext, end, vs, ve, want;
		Boolean first = start < from || start >= to;
		if (first) {
			EdVisible(ed, &vs, &ve);
			start = vs > from && vs < to ? vs : from;
			want = ve - start > kStyleSlice ? ve - start : kStyleSlice;
		} else {
			want = kStyleSlice;
		}
		start = md_seek(&s, t, n, start);
		if (first)
			d->styleOrigin = start;
		end = start + want < to ? md_line_after(t, n, start + want - 1) : to;
		if (end > to)
			end = to;
		/* what's left: up to the end, then what was before the first slice */
		if (end < to) {
			d->styleFrom = from;
			d->styleTo = to;
			d->styleNext = end;
			d->styleDue = true;
		} else if (d->styleOrigin > from) {
			d->styleFrom = from;
			d->styleTo = d->styleOrigin;
			d->styleNext = from;
			d->styleOrigin = from;
			d->styleDue = true;
		}
		d->styleAt = TickCount();
		from = start;
		to = end;
	}
	buf = to > from ? NewPtr(to - from) : NULL;
	if (buf == NULL) {
		EdUnlockText(ed);
		return;
	}
	if (gPrefs.format) {
		md_classify(&s, t, n, to, (unsigned char *) buf);
		if (d->styleDue && d->styleNext == to)
			d->styleState = s;          /* the next slice goes on from here */
	} else {
		memset(buf, 0, (size_t) (to - from));
	}
	EdUnlockText(ed);
	whole = to - from > 4096;
	if (whole)
		SetCursor(*GetCursor(watchCursor));
	BlockOff(d);
	DLOG(("Restyle: %ld-%ld at %lu", from, to, TickCount()));
	EdSetClasses(ed, from, to, (unsigned char *) buf);
	DisposePtr(buf);
	if (whole)
		InitCursor();
	SyncScrollBar(d);
}

static void StyleIdle(Doc *d)
{
	if (d->styleDue && TickCount() >= d->styleAt)
		Restyle(d);
}

void DocRestyleAll(void)
{
	short i;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL)
			TouchAll(gDocs[i]);
}

void DocApplyTheme(void)
{
	short i;
	Doc *d;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if ((d = gDocs[i]) != NULL) {
			ThemeWindow(d->w);
			SetPort(d->w);
			EdSetTheme(d->ed);
			InvalRect(&d->w->portRect);
			TouchAll(d);                /* the colors, restyled */
		}
}

Boolean DocCanStyle(Doc *d)
{
	return EdCanStyle(d->ed);
}

static void ShowDoc(Doc *d)
{
	TouchAll(d);
	Restyle(d);                         /* before it shows: no plain flash */
	SetTitle(d);
	SyncScrollBar(d);
	CountWords(d);
	ShowWindow(d->w);
	SelectWindow(d->w);
}

#define kTemplateName "Topica Template"
enum { kTemplateMax = 8192 };

/* The writer's own template, beside Topica or with the preferences, made
   into a new post's text: a new pointer, or NULL when there's none */
static Ptr OwnTemplate(const char *date, long *len)
{
	FileRef f;
	Ptr data, text, out;
	long n, m;
	int utf8;

	FileBesideApp(kTemplateName, &f);
	if (FileRead(&f, &data, &n) != noErr
			&& (FilePrefsFolder(kTemplateName, &f) != noErr || FileRead(&f, &data, &n) != noErr))
		return NULL;
	if (n > kTemplateMax) {
		DisposePtr(data);
		return NULL;
	}
	utf8 = tc_is_utf8(data, n);
	m = tc_to_mac(data, n, NULL, 0, utf8);
	text = NewPtr(m > 0 ? m : 1);
	if (text == NULL) {
		DisposePtr(data);
		return NULL;
	}
	(void) tc_to_mac(data, n, text, m, utf8);
	DisposePtr(data);
	n = post_fill(text, m, date, NULL, 0);
	out = NewPtr(n > 0 ? n : 1);
	if (out != NULL)
		(void) post_fill(text, m, date, out, n);
	DisposePtr(text);
	*len = n;
	return out;
}

Doc *DocNew(Boolean withTemplate)
{
	Doc *d = MakeDoc();
	char t[160];
	long n;

	if (d == NULL)
		return NULL;
	NameUntitled(d);
	if (withTemplate) {
		char date[16];
		Ptr own;
		Today(date);
		if ((own = OwnTemplate(date, &n)) != NULL) {
			(void) EdSetText(d->ed, own, n);
			EdSetSelection(d->ed, n, n);
			DisposePtr(own);
		} else {
			n = post_template(gPrefs.profile == kProfileHugo, gPrefs.layout, date, '\r', t, sizeof(t));
			if (n > 0) {
				(void) EdSetText(d->ed, t, n);
				EdSetSelection(d->ed, n, n);
			}
		}
	}
	ShowDoc(d);
	return d;
}

/* ---- recovery copies ---- */

static void DiscardRecovery(Doc *d)
{
	if (d->hasRec)
		(void) FileDelete(&d->rec);
	d->hasRec = false;
}

/* A copy of an unsaved post in the System Folder's Topica Recovery folder,
   a few minutes apart, when the writer pauses */
static void WriteRecovery(Doc *d)
{
	FileRef dir;
	Str255 title;
	char name[32], t[256];
	long h, len = EdLength(d->ed);
	Ptr buf;

	d->recDue = false;
	d->recAt = TickCount();
	if (!d->hasRec) {
		if (FileSystemSubfolder(kRecoveryFolder, &dir) != noErr)
			return;
		tp_sprintf(name, "Post %lu", (unsigned long) (TickCount() % 1000000UL));
		FileRefSibling(&dir, name, &d->rec);
		d->hasRec = true;
	}
	GetWTitle(d->w, title);
	CStr(t, title);
	h = recovery_header(t, NULL, 0);
	buf = NewPtr(h + len > 0 ? h + len : 1);
	if (buf == NULL)
		return;
	(void) recovery_header(t, buf, h);
	memcpy(buf + h, EdLockText(d->ed), (size_t) len);
	EdUnlockText(d->ed);
	(void) FileWrite(&d->rec, buf, h + len, kPostType, kCreator);
	DisposePtr(buf);
}

static void RecoveryIdle(Doc *d)
{
	unsigned long now = TickCount();

	if (gPrefs.autosaveMinutes <= 0 || !d->dirty || !d->recDue)
		return;
	if (now - d->changedAt < kRecoveryPause)
		return;
	if (d->hasRec && now - d->recAt < (unsigned long) gPrefs.autosaveMinutes * 3600UL)
		return;
	WriteRecovery(d);
}

short DocRecoverAll(void)
{
	FileRef dir, f;
	short i, opened = 0;
	OSErr err;

	if (FileSystemSubfolder(kRecoveryFolder, &dir) != noErr)
		return 0;
	for (i = 1; DocCount() < TOPICA_MAX_DOCS; i++) {
		Ptr data;
		long len, body;
		char title[64];
		Doc *d;
		err = FileIndexed(&dir, i, &f);
		if (err == dirNFErr)
			continue;
		if (err != noErr)
			break;
		if (FileRead(&f, &data, &len) != noErr)
			continue;
		if (recovery_parse(data, len, title, sizeof(title) - 12, &body)
				&& len - body <= EdMaxLength() && (d = MakeDoc()) != NULL) {
			Str255 p;
			strcat(title, " (recovered)");
			PStr(p, title);
			memcpy(d->untitled, p, (size_t) (p[0] > 63 ? 63 : p[0]) + 1);
			(void) EdSetText(d->ed, data + body, len - body);
			d->rec = f;
			d->hasRec = true;
			d->dirty = true;
			undo_forget_saved(&d->undo);
			ShowDoc(d);
			opened++;
		}
		DisposePtr(data);
	}
	return opened;
}

void DocApplySettings(void)
{
	short i;

	Boolean font = strcmp(gAppliedFont, gPrefs.font) != 0 || gAppliedSize != gPrefs.size;

	strcpy(gAppliedFont, gPrefs.font);
	gAppliedSize = gPrefs.size;
	for (i = 0; i < TOPICA_MAX_DOCS; i++) {
		Doc *d = gDocs[i];
		if (d == NULL)
			continue;
		SetPort(d->w);
		ViSettings(d);
		if (font) {
			EdSetFont(d->ed, SettingsFontNumber(), gPrefs.size);
			TouchAll(d);
			Resized(d);
		}
		UpdateStatus(d, true);
	}
}

/* ---- files ---- */

/* Reads a file into a doc's editor; 0 (after an alert) when it can't */
static Boolean Load(Doc *d, const FileRef *f)
{
	Ptr data, text;
	long len, n;
	OSErr err;
	int utf8;
	char what[200], name[64];

	CStr(name, f->name);
	tp_sprintf(what, "\xD2%s\xD3 couldn\xD5t be opened.", name);
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
		tp_sprintf(what, "\xD2%s\xD3 is too long to open: the most is %ld characters.", name, EdMaxLength());
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
	d->changes++;
	d->editDue = true;                  /* all of it, for the preview */
	d->editFrom = 0;
	d->editTo = n;
	d->editDelta = 0;
	InitCursor();
	return true;
}

/* A stationery pad opened: an untitled post with the pad's text, {{date}}
   filled in as Topica Template's is */
static void FromPad(Doc *d)
{
	char date[16];
	const char *t;
	long n, m;
	Ptr p;

	Today(date);
	t = EdLockText(d->ed);
	n = EdLength(d->ed);
	m = post_fill(t, n, date, NULL, 0);
	p = m != n ? NewPtr(m > 0 ? m : 1) : NULL;
	if (p != NULL)
		(void) post_fill(t, n, date, p, m);
	EdUnlockText(d->ed);
	if (p != NULL) {
		(void) EdSetText(d->ed, p, m);
		DisposePtr(p);
		undo_clear(&d->undo);
	}
	EdSetSelection(d->ed, EdLength(d->ed), EdLength(d->ed));
	NameUntitled(d);
}

Doc *DocOpen(const FileRef *f)
{
	short i;
	Doc *d;
	Boolean pad = FileIsStationery(f);

	for (i = 0; i < TOPICA_MAX_DOCS && !pad; i++) {
		if (gDocs[i] != NULL && gDocs[i]->hasFile && FileRefSame(&gDocs[i]->file, f)) {
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
		DragRemove(d->w);
		DisposeWindow(d->w);
		DisposePtr((Ptr) d);
		return NULL;
	}
	if (pad) {
		FromPad(d);                     /* a pad can be opened again and again */
	} else {
		d->file = *f;
		d->hasFile = true;
	}
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
	tp_sprintf(what, "\xD2%s\xD3 couldn\xD5t be saved.", name);
	t = EdLockText(d->ed);
	n = tc_to_disk(t, len, NULL, 0, gPrefs.utf8, gPrefs.lf);
	out = NewPtr(n > 0 ? n : 1);
	if (out == NULL) {
		EdUnlockText(d->ed);
		ErrorAlert(what, memFullErr);
		return false;
	}
	(void) tc_to_disk(t, len, out, n, gPrefs.utf8, gPrefs.lf);
	EdUnlockText(d->ed);
	SetCursor(*GetCursor(watchCursor));
	err = FileWrite(f, out, n, kPostType, kCreator);
	DisposePtr(out);
	InitCursor();
	if (err != noErr) {
		ErrorAlert(what, err);
		return false;
	}
	d->dirty = false;
	undo_mark_saved(&d->undo);
	DiscardRecovery(d);
	return true;
}

/* The name Save As suggests: the post's file name from today and its title */
static void SuggestName(Doc *d, char *out, int cap)
{
	char title[128];
	DateTimeRec dt;
	const char *t;

	if (d->hasFile) {
		CStr(out, d->file.name);
		return;
	}
	t = EdLockText(d->ed);
	(void) fm_get(t, EdLength(d->ed), "title", title, sizeof(title));
	EdUnlockText(d->ed);
	/* a Mac with no clock battery wakes up in 1904: no date then; Hugo
	   posts carry their date inside */
	post_filename(ClockDate(&dt) && gPrefs.profile != kProfileHugo ? dt.year : 0, dt.month, dt.day,
		title, 31, out, cap);
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
	NoDialogHelp();
	if (CautionAlert(kRevertAlert, (ModalFilterUPP) StandardFilter) != kOK)
		return;
	if (Load(d, &d->file)) {
		TouchAll(d);
		Restyle(d);
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
		NoDialogHelp();
		switch (CautionAlert(kSaveChangesAlert, (ModalFilterUPP) StandardFilter)) {
		case 1:                         /* Save */
			if (!DocSave(d))
				return false;
			break;
		case 2:                         /* Cancel */
			return false;
		}
	}
	d->blockOn = false;                 /* the window goes */
	DiscardRecovery(d);
	PreviewForget(d);
	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] == d)
			gDocs[i] = NULL;
	EdDispose(d->ed);
	DisposePtr(d->undoBuf);
	DragRemove(d->w);
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
		tp_sprintf(msg, "A post can\xD5t be longer than %ld characters.", EdMaxLength());
		ShowNote(msg);
		return false;
	}
	SetPort(d->w);
	t = EdLockText(d->ed);
	undo_record(&d->undo, kind, start, t + start, end - start, text, len);
	EdUnlockText(d->ed);
	(void) EdReplace(d->ed, start, end, text, len);
	Touch(d, start, end, len);
	EdShowSelection(d->ed);
	SyncScrollBar(d);
	Changed(d);
	return true;
}

void DocShowLine(Doc *d, long offset)
{
	SetPort(d->w);
	undo_seal(&d->undo);
	EdSetSelection(d->ed, offset, offset);
	EdScrollTo(d->ed, (short) EdLineOf(d->ed, offset));
	SyncScrollBar(d);
	UpdateStatus(d, false);
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
	do {
		/* a group (a vi change) undoes as one */
		if (!undo_step(&d->undo, redo, &a))
			return;
		if (!EdReplace(d->ed, a.pos, a.pos + a.removeLen, a.insert, a.insertLen)) {
			SysBeep(10);
			return;
		}
		Touch(d, a.pos, a.pos + a.removeLen, a.insertLen);
	} while (a.more);
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
	BlockOff(d);                        /* everywhere, not just in the update */
	BeginUpdate(d->w);
	EdDraw(d->ed);
	if (d->pane > 0)
		PreviewPaneDraw();
	ThemePlain();
	DrawControls(d->w);
	DrawGrowIcon(d->w);
	ThemePort();
	DrawStatus(d);
	EndUpdate(d->w);
}

void DocActivate(Doc *d, Boolean active)
{
	SetPort(d->w);
	BlockOff(d);
	d->teOff = false;
	EdActivate(d->ed, active);
	ThemePlain();
	HiliteControl(d->vbar, active ? 0 : 255);
	DrawGrowIcon(d->w);
	ThemePort();
	if (d->pane > 0)
		PreviewPaneActivate(active);
	if (active)
		undo_seal(&d->undo);
}

/* ---- vi's block cursor ----
   In normal mode the character under the cursor is shown inverted, in
   place of TextEdit's caret. It's taken off before every event (DocBlocksOff),
   by drawing that character again, and put back when idle. */

static void BlockOff(Doc *d)
{
	GrafPtr save;

	if (!d->blockOn)
		return;
	d->blockOn = false;
	GetPort(&save);
	SetPort(d->w);
	EdRedrawRect(d->ed, &d->block);
	SetPort(save);
}

void DocBlocksOff(void)
{
	short i;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL)
			BlockOff(gDocs[i]);
}

/* The front post: the block in normal mode, TextEdit's caret otherwise */
static void BlockSync(Doc *d)
{
	long s, e;
	Boolean want;

	EdGetSelection(d->ed, &s, &e);
	want = d->vi.on && d->vi.mode == VI_NORMAL && s == e;
	if (want != d->teOff) {
		BlockOff(d);
		EdActivate(d->ed, !want);
		d->teOff = want;
	}
	if (!want || d->blockOn)
		return;
	EdCharRect(d->ed, s, &d->block);
	if (EmptyRect(&d->block))
		return;                         /* scrolled out of sight */
	InvertRect(&d->block);
	d->blockOn = true;
}

void DocIdle(Doc *d)
{
	SetPort(d->w);
	EdIdle(d->ed);
	BlockSync(d);
	UpdateStatus(d, false);
}

void DocIdleAll(void)
{
	short i;

	for (i = 0; i < TOPICA_MAX_DOCS; i++)
		if (gDocs[i] != NULL) {
			StyleIdle(gDocs[i]);
			RecoveryIdle(gDocs[i]);
		}
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
	ThemePort();                        /* the text, in the theme; the bar, plain */
	EdScrollTo(gScrolling->ed, (short) (value + delta));
	ThemePlain();
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
		ThemePlain();
		if (part == inThumb) {
			(void) TrackControl(c, p, NULL);
			ThemePort();
			EdScrollTo(d->ed, GetControlValue(c));
		} else {
			gScrolling = d;
			(void) TrackControl(c, p, (ControlActionUPP) ScrollAction);
			gScrolling = NULL;
		}
		ThemePort();
		SyncScrollBar(d);
		return;
	}
	if (d->pane > 0) {
		DocPaneRect(d, &v);
		if (p.h >= v.left && p.h < v.left + kDivider && p.v < v.bottom) {
			DragDivider(d);
			return;
		}
		v.bottom += 1;                  /* its scroll bar's end */
		if (PtInRect(p, &v)) {
			PreviewClick(e);
			return;
		}
	}
	ViewRect(d, &v);
	if (PtInRect(p, &v)) {
		if (!(e->modifiers & shiftKey) && DragText(d, e)) {
			SyncScrollBar(d);
			UpdateStatus(d, false);
			return;
		}
		EdClick(d->ed, p, (e->modifiers & shiftKey) != 0);
		ViClicked(d);
		SyncScrollBar(d);
		UpdateStatus(d, false);
	}
}

/* ---- vi ---- */

static void ViUnlock(Doc *d)
{
	if (d->viText != NULL) {
		EdUnlockText(d->ed);
		d->viText = NULL;
	}
}

static const char *ViText(void *ctx, long *n)
{
	Doc *d = (Doc *) ctx;

	if (d->viText == NULL)
		d->viText = EdLockText(d->ed);
	*n = EdLength(d->ed);
	return d->viText;
}

static void ViGetSel(void *ctx, long *s, long *e)
{
	EdGetSelection(((Doc *) ctx)->ed, s, e);
}

static void ViSetSel(void *ctx, long s, long e)
{
	EdSetSelection(((Doc *) ctx)->ed, s, e);
}

static int ViReplace(void *ctx, int kind, long s, long e, const char *ins, long len)
{
	Doc *d = (Doc *) ctx;
	char buf[256];

	/* TextEdit grows the text: it can't be locked, and ins may be in it */
	if (ins >= d->viText && d->viText != NULL && ins < d->viText + EdLength(d->ed)) {
		if (len > (long) sizeof(buf))
			return 0;
		memcpy(buf, ins, (size_t) len);
		ins = buf;
	}
	ViUnlock(d);
	return DocReplace(d, kind == VI_TYPE ? kUndoTyping : kind == VI_DELETE ? kUndoDelete : kUndoChange,
		s, e, ins, len);
}

static long ViVertical(void *ctx, long pos, int lines, long *goal)
{
	return EdVertical(((Doc *) ctx)->ed, pos, lines, goal);
}

static void ViUndo(void *ctx, int redo)
{
	Doc *d = (Doc *) ctx;

	ViUnlock(d);
	DocUndo(d, (Boolean) redo);
}

static void ViGroup(void *ctx, int begin)
{
	undo_group(&((Doc *) ctx)->undo, begin);
}

static void ViFind(void *ctx, int how)
{
	Doc *d = (Doc *) ctx;

	ViUnlock(d);
	if (how == VI_FIND_ASK)
		FindDialog(d);
	else
		FindAgain(d);
	SetPort(d->w);
}

static void ViBeep(void *ctx)
{
	(void) ctx;
	SysBeep(1);
}

/* "a to "z, one set for every window as vi's register is, each made when
   it's first used */
static ViReg *ViNamed(void *ctx, int name)
{
	static ViReg named[26];
	ViReg *r = &named[name - 'a'];

	(void) ctx;
	if (r->buf == NULL) {
		r->buf = NewPtr(kViRegister);
		if (r->buf == NULL)
			return NULL;
		r->cap = kViRegister;
	}
	return r;
}

/* :w :q :wq :x :q!, done after the key (ViDone): closing frees the post */
static int ViCommand(void *ctx, const char *cmd)
{
	static const char *const kCmds[] = { "w", "wq", "x", "q", "q!" };
	static const char kDo[] = "wWxqQ";
	short i;

	for (i = 0; i < (short) (sizeof(kCmds) / sizeof(kCmds[0])); i++)
		if (strcmp(cmd, kCmds[i]) == 0) {
			((Doc *) ctx)->viCmd = kDo[i];
			return 1;
		}
	return 0;
}

static const ViHost kViHost = {
	NULL, ViText, ViGetSel, ViSetSel, ViReplace, ViVertical, ViUndo, ViGroup, ViFind, ViBeep, ViNamed, ViCommand
};

/* The command a key left: true when it closed the post. :q asks to save
   a changed post, as Close does; :q! doesn't. */
static Boolean ViDone(Doc *d)
{
	char cmd = d->viCmd;

	d->viCmd = 0;
	switch (cmd) {
	case 'w':
		(void) DocSave(d);
		break;
	case 'W':
		return DocSave(d) && DocClose(d, false);
	case 'x':
		return (!d->dirty || DocSave(d)) && DocClose(d, false);
	case 'q':
		return DocClose(d, false);
	case 'Q':
		d->dirty = false;
		return DocClose(d, false);
	}
	return false;
}

/* A key for vi: 1 when it used it */
static Boolean ViKey(Doc *d, char c)
{
	ViHost h = kViHost;
	int k = (unsigned char) c;
	Boolean used;

	if (!d->vi.on)
		return false;
	if (k == 0x03)
		k = VI_RETURN;                  /* Enter */
	if (d->vi.mode != VI_INSERT) {
		/* the arrow keys move as h j k l; Home, End and the page keys scroll */
		switch (k) {
		case 0x1C: k = 'h'; break;
		case 0x1D: k = 'l'; break;
		case 0x1E: k = 'k'; break;
		case 0x1F: k = 'j'; break;
		case 0x01: case 0x04: case 0x0B: case 0x0C:
			return false;
		}
	}
	h.ctx = d;
	used = vi_key(&d->vi, &h, k, TickCount()) != 0;
	ViUnlock(d);
	if (used) {
		SetPort(d->w);
		EdShowSelection(d->ed);
	}
	return used;
}

void DocViEscape(Doc *d)
{
	SetPort(d->w);
	if (ViKey(d, VI_ESC)) {
		SyncScrollBar(d);
		UpdateStatus(d, false);
	}
}

Boolean DocViOn(Doc *d)
{
	return d->vi.on;
}

static void ViClicked(Doc *d)
{
	ViHost h = kViHost;

	if (!d->vi.on)
		return;
	h.ctx = d;
	vi_clicked(&d->vi, &h);
	ViUnlock(d);
}

static void ViSettings(Doc *d)
{
	Boolean was = d->vi.on;

	d->vi.on = gPrefs.vi;
	d->vi.jk = gPrefs.viJk;
	if (d->vi.on && !was)
		ViClicked(d);                   /* start in normal mode */
}

/* Option-[ and Option-] type curly quotes, which break code and front
   matter: there they come out straight */
static Boolean Straight(Doc *d, long at)
{
	enum { kLine = 2048 };              /* a paragraph is one line */
	Ptr line = NewPtr(kLine);
	Boolean straight;

	if (line == NULL)
		return false;
	straight = md_straight_at(EdLockText(d->ed), EdLength(d->ed), at, (unsigned char *) line, kLine);
	EdUnlockText(d->ed);
	DisposePtr(line);
	return straight;
}

void DocKey(Doc *d, EventRecord *e)
{
	char c = (char) (e->message & charCodeMask);
	long s, en, len;

	SetPort(d->w);
	ObscureCursor();
	if (ViKey(d, c)) {
		if (ViDone(d))
			return;                     /* :q closed it */
		SetPort(d->w);
		SyncScrollBar(d);
		UpdateStatus(d, false);
		return;
	}
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
		if ((unsigned char) c >= 0xD2 && (unsigned char) c <= 0xD5 && Straight(d, s))
			c = (unsigned char) c <= 0xD3 ? '"' : '\'';
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
	short width = (short) (d->w->portRect.right - d->w->portRect.left);

	SetPort(d->w);
	if (d->pane > 0) {
		/* the pane keeps its width while the post has room */
		if (width - d->pane < kMinWidth)
			d->pane = (short) (width - kMinWidth);
		if (d->pane < d->paneMin)
			d->pane = d->paneMin;
	}
	ViewRect(d, &r);
	EdSetView(d->ed, &r);
	BarRect(d, &r);
	ThemePlain();
	MoveControl(d->vbar, r.left, r.top);
	SizeControl(d->vbar, (short) (r.right - r.left), (short) (r.bottom - r.top));
	ThemePort();
	SyncScrollBar(d);
	InvalRect(&d->w->portRect);
	if (d->pane > 0)
		PreviewPaneResized();
}

/* Drags the line between the post and the pane */
static void DragDivider(Doc *d)
{
	Rect port = d->w->portRect;
	short split = (short) (port.right - d->pane), x = split, nx;
	short lo = (short) (port.left + kMinWidth), hi = (short) (port.right - d->paneMin);
	PenState pen;
	Point m;

	GetPenState(&pen);
	ThemePlain();
	PenMode(patXor);
	PenPat(&qd.gray);
	MoveTo(x, port.top);
	LineTo(x, port.bottom);
	while (StillDown()) {
		GetMouse(&m);
		nx = m.h < lo ? lo : m.h > hi ? hi : m.h;
		if (nx != x) {
			MoveTo(x, port.top);
			LineTo(x, port.bottom);
			x = nx;
			MoveTo(x, port.top);
			LineTo(x, port.bottom);
		}
	}
	MoveTo(x, port.top);
	LineTo(x, port.bottom);
	SetPenState(&pen);
	ThemePort();
	if (x != split) {
		d->pane = (short) (port.right - x);
		Resized(d);
	}
}

void DocSetPane(Doc *d, short width, short min)
{
	Rect s = qd.screenBits.bounds, g;
	short post, total, avail;
	GrafPtr save;

	GetPort(&save);
	SetPort(d->w);
	g = d->w->portRect;
	LocalToGlobal((Point *) &g.top);
	LocalToGlobal((Point *) &g.bottom);
	SetPort(save);
	s.top += 20 + 18;                   /* the menu bar and a title bar */
	InsetRect(&s, 4, 4);
	avail = (short) (s.right - s.left);
	post = (short) (g.right - g.left - d->pane);
	if (width > 0 && post + width > avail) {
		/* not both as they are: the post gives way, to half the screen */
		post = (short) (avail - width);
		if (post < avail / 2)
			post = (short) (avail / 2);
		if (post < kMinWidth)
			post = kMinWidth;
		width = (short) (avail - post);
		if (width < min)
			width = min;
	}
	total = (short) (post + width);
	if (g.left + total > s.right)
		g.left = (short) (s.right - total);
	if (g.left < s.left)
		g.left = s.left;
	d->pane = width;
	d->paneMin = min;
	MoveWindow(d->w, g.left, g.top, false);
	SizeWindow(d->w, total, (short) (g.bottom - g.top), true);
	Resized(d);
}

void DocGrow(Doc *d, Point where)
{
	Rect limits = qd.screenBits.bounds;
	long size;

	limits.left = (short) (kMinWidth + (d->pane > 0 ? d->paneMin : 0));
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
