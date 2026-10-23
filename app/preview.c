/*
 * preview.c: the Preview window (see preview.h).
 *
 * The page is drawn off screen and copied to the window, so scrolling and
 * a new layout don't flicker: into a GWorld on a color Mac, a plain bitmap
 * on a black-and-white one. When there's no memory for it, the window is
 * drawn directly.
 */
#include <Quickdraw.h>
#include <Multiverse.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <string.h>

#include "docwin.h"
#include "editor.h"
#include "page.h"
#include "preview.h"
#include "util.h"

enum { kScrollWidth = 16, kMargin = 10 };
enum { kMinWidth = 200, kMinHeight = 120 };
enum { kPause = 45 };                   /* ticks of quiet before laying out again */
enum { kArrowStep = 16 };
enum { kUpArrow = 0x1E, kDownArrow = 0x1F, kHomeKey = 0x01, kEndKey = 0x04,
	kPageUp = 0x0B, kPageDown = 0x0C };

static WindowPtr gWin;
static ControlHandle gBar;
static Page *gPage;
static Doc *gDoc;                       /* the post shown */
static unsigned long gSeen;             /* its changes when last looked at */
static Boolean gDue;                    /* lay out again at gDueAt */
static unsigned long gDueAt;
static Boolean gLaidOut;
static Boolean gRefining;               /* some blocks' heights are guesses */
static long gTop;                       /* the page's y at the window's top */
static short gUnit = 1;                 /* pixels per scroll bar step */
#ifdef TOPICA_DEBUG
static unsigned long gRefineTicks;      /* when the last layout began */
#endif

/* off screen */
static GWorldPtr gWorld;
static GrafPort gOffPort;
static Boolean gHaveOffPort;
static Rect gOffRect;

Boolean IsPreview(WindowPtr w)
{
	return w != NULL && w == gWin;
}

Boolean PreviewIsFront(void)
{
	return gWin != NULL && FrontWindow() == gWin;
}

Doc *PreviewDoc(void)
{
	return gWin != NULL ? gDoc : NULL;
}

/* ---- geometry ---- */

static void ContentRect(Rect *r)
{
	*r = gWin->portRect;
	r->right -= kScrollWidth - 1;
}

static void BarRect(Rect *r)
{
	*r = gWin->portRect;
	r->left = r->right - kScrollWidth + 1;
	r->top -= 1;
	r->right += 1;
	r->bottom -= kScrollWidth - 2;
}

static short PageWidth(void)
{
	Rect r;

	ContentRect(&r);
	return (short) (r.right - r.left - 2 * kMargin);
}

static long ViewHeight(void)
{
	Rect r;

	ContentRect(&r);
	return r.bottom - r.top;
}

static long MaxTop(void)
{
	long m = gLaidOut ? PageHeight(gPage) - ViewHeight() : 0;

	return m > 0 ? m : 0;
}

static void SyncBar(void)
{
	long max = MaxTop();

	for (gUnit = 1; max / gUnit > 32000; gUnit *= 2)
		;
	SetControlMaximum(gBar, (short) (max / gUnit));
	SetControlValue(gBar, (short) (gTop / gUnit));
}

/* The grow box, without the lines DrawGrowIcon adds for scroll bars */
static void DrawGrow(void)
{
	Rect r = gWin->portRect;
	RgnHandle save = NewRgn();

	r.left = r.right - kScrollWidth + 1;
	r.top = r.bottom - kScrollWidth + 1;
	GetClip(save);
	ClipRect(&r);
	DrawGrowIcon(gWin);
	SetClip(save);
	DisposeRgn(save);
}

/* ---- off screen ---- */

static void FreeOffscreen(void)
{
	if (gWorld != NULL) {
		DisposeGWorld(gWorld);
		gWorld = NULL;
	}
	if (gHaveOffPort) {
		ClosePort(&gOffPort);
		DisposePtr(gOffPort.portBits.baseAddr);
		gHaveOffPort = false;
	}
}

/* An offscreen as big as the content; none if there's no memory */
static void MakeOffscreen(void)
{
	Rect r;
	GrafPtr save;
	Ptr bits;
	short rowBytes;

	FreeOffscreen();
	ContentRect(&r);
	OffsetRect(&r, (short) -r.left, (short) -r.top);
	gOffRect = r;
	if (HasGWorlds()) {
		if (NewGWorld(&gWorld, 0, &r, NULL, NULL, 0) != noErr)
			gWorld = NULL;
		return;
	}
	if (HasColorQD())
		return;
	rowBytes = (short) (((r.right + 15) / 16) * 2);
	bits = NewPtr((long) rowBytes * r.bottom);
	if (bits == NULL)
		return;
	GetPort(&save);
	OpenPort(&gOffPort);                /* the screen's bits until set here */
	gOffPort.portBits.baseAddr = bits;
	gOffPort.portBits.rowBytes = rowBytes;
	gOffPort.portBits.bounds = r;
	gOffPort.portRect = r;
	RectRgn(gOffPort.visRgn, &r);
	RectRgn(gOffPort.clipRgn, &r);
	gHaveOffPort = true;
	SetPort(save);
}

/* ---- drawing ---- */

static void DrawEmpty(const Rect *r)
{
	static char kMsg[] = "Open a post to preview it.";
	short w;

	EraseRect(r);
	TextFont(kGeneva);
	TextSize(9);
	w = TextWidth(kMsg, 0, (short) (sizeof(kMsg) - 1));
	MoveTo((short) ((r->left + r->right - w) / 2), (short) (r->top + 40));
	DrawText(kMsg, 0, (short) (sizeof(kMsg) - 1));
}

/* The page, at gTop, into r of the current port */
static void DrawPage(const Rect *r)
{
	Rect pr = *r;
	Editor *ed;

	if (gDoc == NULL || !gLaidOut) {
		DrawEmpty(r);
		return;
	}
	ed = DocEditor(gDoc);
	pr.left += kMargin;
	pr.right -= kMargin;
	PageDraw(gPage, EdLockText(ed), EdLength(ed), gTop, &pr, ColorScreen());
	EdUnlockText(ed);
	if (pr.left > r->left) {
		/* PageDraw erased only its own part */
		Rect m = *r;
		m.right = pr.left;
		EraseRect(&m);
		m = *r;
		m.left = pr.right;
		EraseRect(&m);
	}
}

/* Draws the content, through the offscreen when there is one */
static void Render(void)
{
	Rect content;
	GrafPtr save;

	GetPort(&save);
	SetPort(gWin);
	ContentRect(&content);
	if (gWorld != NULL) {
		CGrafPtr oldPort;
		GDHandle oldDevice;
		PixMapHandle pm = GetGWorldPixMap(gWorld);
		GetGWorld(&oldPort, &oldDevice);
		if (LockPixels(pm)) {
			SetGWorld((CGrafPtr) gWorld, NULL);
			DrawPage(&gOffRect);
			SetGWorld(oldPort, oldDevice);
			ForeColor(blackColor);
			BackColor(whiteColor);
			CopyBits((BitMap *) *pm, &gWin->portBits, &gOffRect, &content, srcCopy, NULL);
			UnlockPixels(pm);
		} else {
			SetGWorld(oldPort, oldDevice);
			DrawPage(&content);
		}
	} else if (gHaveOffPort) {
		SetPort(&gOffPort);
		DrawPage(&gOffRect);
		SetPort(gWin);
		CopyBits(&gOffPort.portBits, &gWin->portBits, &gOffRect, &content, srcCopy, NULL);
	} else {
		RgnHandle clip = NewRgn();
		GetClip(clip);
		ClipRect(&content);
		DrawPage(&content);
		SetClip(clip);
		DisposeRgn(clip);
	}
	SetPort(save);
}

/* Lays out what the window shows exactly, before drawing it */
static void Refine(void)
{
	Editor *ed;
	long shift;
	Boolean redraw;

	if (gDoc == NULL || !gLaidOut)
		return;
	ed = DocEditor(gDoc);
	(void) PageRefine(gPage, EdLockText(ed), EdLength(ed), gTop, gTop + ViewHeight(), 0, &shift, &redraw);
	EdUnlockText(ed);
}

static void SetTop(long top)
{
	long max = MaxTop();

	if (top > max)
		top = max;
	if (top < 0)
		top = 0;
	if (top == gTop)
		return;
	gTop = top;
	SetControlValue(gBar, (short) (gTop / gUnit));
	Refine();
	Render();
}

/* ---- layout ---- */

static void SetTitle(void)
{
	Str255 name, title;

	if (gDoc == NULL) {
		SetWTitle(gWin, "\pPreview");
		return;
	}
	GetWTitle(DocWindow(gDoc), name);
	if (name[0] > 240)
		name[0] = 240;
	memcpy(title, name, (size_t) name[0] + 1);
	memcpy(title + title[0] + 1, " Preview", 8);
	title[0] = (unsigned char) (title[0] + 8);
	SetWTitle(gWin, title);
}

/* Reads the post's blocks again: all of them (full), or around what changed */
static void Layout(Boolean full)
{
	Editor *ed;
	GrafPtr save;
	const char *text;
	long n, from, to, delta;

	gDue = false;
	if (gDoc == NULL) {
		gLaidOut = false;
	} else {
		ed = DocEditor(gDoc);
		n = EdLength(ed);
		if (!DocTakeEdits(gDoc, &from, &to, &delta) && gLaidOut && !full)
			return;                     /* nothing changed */
		if (full || !gLaidOut) {
			from = 0;
			to = n;
			delta = 0;
		}
		GetPort(&save);
		SetPort(gWin);
		text = EdLockText(ed);
#ifdef TOPICA_DEBUG
		{
			unsigned long t0 = TickCount();
			gLaidOut = PageLayout(gPage, text, n, PageWidth(), from, to, delta);
			DLOG(("Preview: layout %ld chars [%ld, %ld) %ld: %ld ticks", n, from, to, delta,
				(long) (TickCount() - t0)));
			gRefineTicks = TickCount();
		}
#else
		gLaidOut = PageLayout(gPage, text, n, PageWidth(), from, to, delta);
#endif
		EdUnlockText(ed);
		SetPort(save);
		gSeen = DocChanges(gDoc);
		gRefining = gLaidOut;
	}
	SetTitle();
	if (gTop > MaxTop())
		gTop = MaxTop();
	Refine();
	SyncBar();
	Render();
	DLOG(("Preview: on the screen after %ld ticks; heap %ld, free %ld, largest block %ld",
		(long) (TickCount() - gRefineTicks), (long) (ApplicationZone()->bkLim - (Ptr) ApplicationZone()),
		(long) FreeMem(), (long) MaxBlock()));
}

/* Shows d from the top */
static void Follow(Doc *d)
{
	gDoc = d;
	gTop = 0;
	gLaidOut = false;
	Layout(true);
}

/* ---- the window ---- */

static void WindowRect(Rect *r)
{
	Rect s = qd.screenBits.bounds;
	short width;

	s.top += 20 + 18;
	InsetRect(&s, 4, 4);
	width = s.right - s.left;
	if (width >= 2 * 520 + 8) {
		/* a big screen: beside the post */
		SetRect(r, (short) (s.right - 520), s.top, s.right, s.bottom);
		return;
	}
	if (width > 560)
		width = 560;
	SetRect(r, s.left, s.top, (short) (s.left + width), s.bottom);
	OffsetRect(r, 24, 12);
	if (r->right > s.right)
		OffsetRect(r, (short) (s.right - r->right), 0);
	if (r->bottom > s.bottom)
		r->bottom = s.bottom;
}

void PreviewShow(Doc *d)
{
	Rect r;

	if (gWin == NULL) {
		gPage = PageNew();
		if (gPage == NULL) {
			ErrorAlert("The preview couldn\xD5t be opened.", memFullErr);
			return;
		}
		WindowRect(&r);
		if (HasColorQD())
			gWin = NewCWindow(NULL, &r, "\pPreview", false, kZoomDocProc, (WindowPtr) -1L, true, 0);
		else
			gWin = NewWindow(NULL, &r, "\pPreview", false, kZoomDocProc, (WindowPtr) -1L, true, 0);
		if (gWin == NULL) {
			PageDispose(gPage);
			gPage = NULL;
			ErrorAlert("The preview couldn\xD5t be opened.", memFullErr);
			return;
		}
		SetPort(gWin);
		BarRect(&r);
		gBar = NewControl(gWin, &r, "\p", true, 0, 0, 0, scrollBarProc, 0);
		MakeOffscreen();
		gDoc = NULL;
		gLaidOut = false;
		ShowWindow(gWin);
	}
	SelectWindow(gWin);
	if (d != gDoc || !gLaidOut)
		Follow(d);
}

void PreviewClose(void)
{
	if (gWin == NULL)
		return;
	FreeOffscreen();
	DisposeWindow(gWin);
	PageDispose(gPage);
	gWin = NULL;
	gPage = NULL;
	gDoc = NULL;
	gLaidOut = false;
}

void PreviewForget(Doc *d)
{
	if (gWin == NULL || d != gDoc)
		return;
	gDoc = NULL;
	gLaidOut = false;
	SetTitle();
	SyncBar();
	Render();
}

void PreviewUpdate(void)
{
	SetPort(gWin);
	BeginUpdate(gWin);
	Render();
	DrawControls(gWin);
	DrawGrow();
	EndUpdate(gWin);
}

void PreviewActivate(Boolean active)
{
	SetPort(gWin);
	HiliteControl(gBar, active ? 0 : 255);
	DrawGrow();
}

static pascal void ScrollAction(ControlHandle c, short part)
{
	long page = ViewHeight() - 24;

	(void) c;
	switch (part) {
	case inUpButton: SetTop(gTop - kArrowStep); break;
	case inDownButton: SetTop(gTop + kArrowStep); break;
	case inPageUp: SetTop(gTop - page); break;
	case inPageDown: SetTop(gTop + page); break;
	}
}

void PreviewClick(EventRecord *e)
{
	Point p = e->where;
	ControlHandle c;
	short part;
	static unsigned long lastWhen;
	long at;

	SetPort(gWin);
	GlobalToLocal(&p);
	part = FindControl(p, gWin, &c);
	if (c == gBar) {
		if (part == inThumb) {
			(void) TrackControl(c, p, NULL);
			SetTop((long) GetControlValue(c) * gUnit);
		} else {
			(void) TrackControl(c, p, (ControlActionUPP) ScrollAction);
		}
		return;
	}
	/* a double-click: there in the post */
	if (e->when - lastWhen <= (unsigned long) LMGetDoubleTime() && gDoc != NULL && gLaidOut) {
		at = PageSourceAt(gPage, gTop + p.v);
		if (at >= 0) {
			SelectWindow(DocWindow(gDoc));
			DocShowLine(gDoc, at);
		}
		lastWhen = 0;
		return;
	}
	lastWhen = e->when;
}

void PreviewKey(EventRecord *e)
{
	long page = ViewHeight() - 24;

	switch ((char) (e->message & charCodeMask)) {
	case kUpArrow: SetTop(gTop - kArrowStep); break;
	case kDownArrow: SetTop(gTop + kArrowStep); break;
	case kPageUp: SetTop(gTop - page); break;
	case kPageDown:
	case ' ': SetTop(gTop + page); break;
	case kHomeKey: SetTop(0); break;
	case kEndKey: SetTop(MaxTop()); break;
	}
}

static void Resized(void)
{
	Rect r;

	SetPort(gWin);
	BarRect(&r);
	MoveControl(gBar, r.left, r.top);
	SizeControl(gBar, (short) (r.right - r.left), (short) (r.bottom - r.top));
	InvalRect(&gWin->portRect);
	MakeOffscreen();
	if (gDoc != NULL)
		Layout(true);
}

void PreviewGrow(Point where)
{
	Rect limits;
	long size;

	SetRect(&limits, kMinWidth, kMinHeight, 32767, 32767);
	size = GrowWindow(gWin, where, &limits);
	if (size == 0)
		return;
	SizeWindow(gWin, LoWord(size), HiWord(size), true);
	Resized();
}

void PreviewZoom(Point where, short part)
{
	if (!TrackBox(gWin, where, part))
		return;
	SetPort(gWin);
	EraseRect(&gWin->portRect);
	ZoomWindow(gWin, part, true);
	Resized();
}

void PreviewIdle(void)
{
	Doc *front;
	unsigned long now = TickCount(), changes;

	if (gWin == NULL)
		return;
	front = DocFront();
	if (front != NULL && front != gDoc) {
		Follow(front);
		return;
	}
	if (gDoc == NULL)
		return;
	changes = DocChanges(gDoc);
	if (changes != gSeen) {
		gSeen = changes;
		gDue = true;
		gDueAt = now + kPause;
	}
	if (gDue && now >= gDueAt) {
		Layout(false);
		return;
	}
	if (gRefining) {
		/* the guessed heights, a few blocks at a time */
		Editor *ed = DocEditor(gDoc);
		long shift;
		Boolean redraw;
		GrafPtr save;
		GetPort(&save);
		SetPort(gWin);
		gRefining = !PageRefine(gPage, EdLockText(ed), EdLength(ed), gTop, gTop + ViewHeight(), 3, &shift, &redraw);
		EdUnlockText(ed);
		SetPort(save);
		gTop += shift;
		if (gTop > MaxTop())
			gTop = MaxTop();
		SyncBar();
		if (redraw)
			Render();
		if (!gRefining)
			DLOG(("Preview: all exact after %ld ticks", (long) (TickCount() - gRefineTicks)));
	}
}
