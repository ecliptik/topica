/* textdrag.c: see textdrag.h. */
#include <Quickdraw.h>
#include <Events.h>
#include <Windows.h>
#include <Memory.h>
#include <Gestalt.h>
#include <ToolUtils.h>

#include "../core/undo.h"
#include "dragmgr.h"
#include "editor.h"
#include "textdrag.h"

static Boolean gHave;
static Doc *gSource;            /* the post a drag Topica started came from */
static long gFrom, gTo;         /* and its text */
static WindowPtr gCaretWin;     /* the drop caret, drawn in XOR while it shows */
static Rect gCaret;

void DragInit(void)
{
	long r;

	gHave = Gestalt(kDragGestalt, &r) == noErr && (r & 1) != 0;
}

static void ToggleCaret(void)
{
	GrafPtr save;

	if (gCaretWin == NULL)
		return;
	GetPort(&save);
	SetPort(gCaretWin);
	PenMode(patXor);
	PaintRect(&gCaret);
	PenNormal();
	SetPort(save);
}

static void HideCaret(void)
{
	ToggleCaret();
	gCaretWin = NULL;
}

/* The drop point in d's text, from the drag's mouse; -1 outside the text */
static long DropOffset(Doc *d, DragReference drag)
{
	Point mouse, pinned;
	Rect view;

	GetDragMouse(drag, &mouse, &pinned);
	SetPort(DocWindow(d));
	GlobalToLocal(&mouse);
	DocTextRect(d, &view);
	if (!PtInRect(mouse, &view))
		return -1;
	return EdOffsetAt(DocEditor(d), mouse);
}

static pascal OSErr Tracking(DragTrackingMessage message, WindowPtr w, void *refCon, DragReference drag)
{
	Doc *d = DocFromWindow(w);
	GrafPtr save;
	long at;
	Rect r;

	(void) refCon;
	if (d == NULL)
		return noErr;
	GetPort(&save);
	if (message == kDragTrackingInWindow) {
		at = DropOffset(d, drag);
		if (at < 0 || (d == gSource && at > gFrom && at < gTo)) {
			HideCaret();                /* outside the text, or on the text being dragged */
		} else {
			EdCaretRect(DocEditor(d), at, &r);
			if (gCaretWin != w || !EqualRect(&r, &gCaret)) {
				HideCaret();
				gCaretWin = w;
				gCaret = r;
				ToggleCaret();
			}
		}
	} else if (message == kDragTrackingLeaveWindow) {
		HideCaret();
	}
	SetPort(save);
	return noErr;
}

static pascal OSErr Receive(WindowPtr w, void *refCon, DragReference drag)
{
	Doc *d = DocFromWindow(w);
	unsigned short count;
	ItemReference item;
	Size size;
	Ptr data;
	long at, len;
	GrafPtr save;

	(void) refCon;
	HideCaret();
	if (d == NULL || CountDragItems(drag, &count) != noErr || count < 1
			|| GetDragItemReferenceNumber(drag, 1, &item) != noErr
			|| GetFlavorDataSize(drag, item, 'TEXT', &size) != noErr || size <= 0)
		return kDragNotAccepted;
	GetPort(&save);
	at = DropOffset(d, drag);
	if (at < 0 || (data = NewPtr(size)) == NULL) {
		SetPort(save);
		return kDragNotAccepted;
	}
	if (GetFlavorData(drag, item, 'TEXT', data, &size, 0) != noErr) {
		DisposePtr(data);
		SetPort(save);
		return kDragNotAccepted;
	}
	len = size;
	if (d == gSource) {
		/* a move within the post, as one undoable step; onto itself, nothing */
		if (at < gFrom || at > gTo) {
			DocGroupEdits(d, true);
			if (at > gTo) {             /* insert first, so the cut doesn't shift the drop point */
				(void) DocReplace(d, kUndoDrag, at, at, data, len);
				(void) DocReplace(d, kUndoDrag, gFrom, gTo, "", 0);
				at -= gTo - gFrom;
			} else {
				(void) DocReplace(d, kUndoDrag, gFrom, gTo, "", 0);
				(void) DocReplace(d, kUndoDrag, at, at, data, len);
			}
			DocGroupEdits(d, false);
			EdSetSelection(DocEditor(d), at, at + len);
		}
	} else if (DocReplace(d, kUndoDrag, at, at, data, len)) {
		EdSetSelection(DocEditor(d), at, at + len);
	}
	DisposePtr(data);
	SetPort(save);
	return noErr;
}

void DragInstall(WindowPtr w)
{
	if (gHave) {
		(void) InstallTrackingHandler(Tracking, w, NULL);
		(void) InstallReceiveHandler(Receive, w, NULL);
	}
}

void DragRemove(WindowPtr w)
{
	if (gHave) {
		(void) RemoveTrackingHandler(Tracking, w);
		(void) RemoveReceiveHandler(Receive, w);
	}
}

/* After a press on the selection: true once the mouse moves (a drag),
   false when the button comes up first (a click) */
static Boolean Moved(Point start)
{
	Point p;

	while (StillDown()) {
		GetMouse(&p);
		LocalToGlobal(&p);
		if (p.h - start.h > 2 || start.h - p.h > 2 || p.v - start.v > 2 || start.v - p.v > 2)
			return true;
	}
	return false;
}

Boolean DragText(Doc *d, EventRecord *e)
{
	Editor *ed = DocEditor(d);
	DragReference drag;
	RgnHandle sel, inner;
	Point local = e->where, offset;
	long s, t;
	Rect bounds;
	const char *text;

	if (!gHave)
		return false;
	SetPort(DocWindow(d));
	GlobalToLocal(&local);
	EdGetSelection(ed, &s, &t);
	if (s >= t)
		return false;
	sel = EdSelectionRgn(ed);
	if (sel == NULL)
		return false;
	if (!PtInRgn(local, sel) || !Moved(e->where) || NewDrag(&drag) != noErr) {
		DisposeRgn(sel);
		return false;
	}
	text = EdLockText(ed);
	(void) AddDragItemFlavor(drag, 1, 'TEXT', text + s, t - s, 0);   /* the Drag Manager keeps a copy */
	EdUnlockText(ed);
	/* the outline the Drag Manager draws: the selection's, in global coordinates */
	offset.h = offset.v = 0;
	LocalToGlobal(&offset);
	OffsetRgn(sel, offset.h, offset.v);
	bounds = (**sel).rgnBBox;
	(void) SetDragItemBounds(drag, 1, &bounds);
	inner = NewRgn();
	if (inner != NULL) {
		CopyRgn(sel, inner);
		InsetRgn(inner, 1, 1);
		DiffRgn(sel, inner, sel);
		DisposeRgn(inner);
	}
	gSource = d;
	gFrom = s;
	gTo = t;
	(void) TrackDrag(drag, e, sel);
	gSource = NULL;
	(void) DisposeDrag(drag);
	DisposeRgn(sel);
	return true;
}
