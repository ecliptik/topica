/* util.c: see util.h. The dialog helpers follow Geomys's dialogs.c. */
#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Menus.h>
#include <Memory.h>
#include <Resources.h>
#include <ToolUtils.h>
#include <Multiverse.h>
#include <OSUtils.h>
#include <Traps.h>
#include <Gestalt.h>
#include <Errors.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "../core/fmt.h"
#include "editor.h"
#include "util.h"

enum { kAfpAccessDenied = -5000 };    /* AppleShare: no access */

void (*gUpdateHook)(WindowPtr w);

void PStr(Str255 out, const char *s)
{
	size_t n = strlen(s);

	if (n > 255)
		n = 255;
	out[0] = (unsigned char) n;
	memcpy(out + 1, s, n);
}

void CStr(char *out, const unsigned char *p)
{
	memcpy(out, p + 1, p[0]);
	out[p[0]] = '\0';
}

static Boolean TrapExists(short trap, TrapType type)
{
	return NGetTrapAddress(trap, type) != NGetTrapAddress(_Unimplemented, kToolboxTrapType);
}

Boolean HasColorQD(void)
{
	static short has = -1;
	SysEnvRec env;

	if (has < 0)
		has = SysEnvirons(1, &env) == noErr && env.hasColorQD;
	return has;
}

Boolean ColorScreen(void)
{
	static short color = -1;

	if (color < 0) {
		color = 0;
		if (HasColorQD()) {
			GDHandle gd = GetMainDevice();
			color = gd != NULL && (**(**gd).gdPMap).pixelSize >= 4;
		}
	}
	return color;
}

Boolean HasGWorlds(void)
{
	static short has = -1;

	if (has < 0)
		has = HasColorQD() && TrapExists(0xAB1D, kToolboxTrapType);   /* _QDExtensions */
	return has;
}

Boolean HasNotifications(void)
{
	static short has = -1;

	if (has < 0)
		has = TrapExists(0xA05E, kOSTrapType);   /* _NMInstall */
	return has;
}

Boolean HasWaitNextEvent(void)
{
	static short has = -1;

	if (has < 0)
		has = TrapExists(_WaitNextEvent, kToolboxTrapType);
	return has;
}

Boolean IsSystem7(void)
{
	static short is = -1;
	long v;

	if (is < 0)
		is = TrapExists(_Gestalt, kToolboxTrapType) && Gestalt(gestaltSystemVersion, &v) == noErr && v >= 0x0700;
	return is;
}

#define kDlogProcOffset 8           /* the procID, after the DLOG's rect */

void NoDialogHelp(void)
{
	if (IsSystem7())
		(void) HMSetDialogResID(-1);
}

DialogPtr GetModalDialog(short id)
{
	Handle h = NULL;
	DialogPtr d;

	if (IsSystem7()) {
		h = GetResource('DLOG', id);
		if (h != NULL) {
			HNoPurge(h);
			*(short *) (*h + kDlogProcOffset) = movableDBoxProc;
		}
		/* its balloons, when it has some (resources/balloons.txt); the Help
		   Manager reads this while the dialog is up, so it stays set */
		(void) HMSetDialogResID(GetResource('hdlg', id) != NULL ? id : -1);
	}
	d = GetNewDialog(id, NULL, (WindowPtr) -1L);
	DLOG(("GetModalDialog %d -> %lx (ResError %d, sys7 %d)", id, (long) d, ResError(), IsSystem7()));
	if (h != NULL) {
		*(short *) (*h + kDlogProcOffset) = dBoxProc;
		HPurge(h);
	}
	if (d != NULL)
		CenterWindow(d);
	return d;
}

void CenterWindow(WindowPtr w)
{
	Rect s = qd.screenBits.bounds, r = w->portRect;
	short wd = r.right - r.left, ht = r.bottom - r.top;
	short left = (short) ((s.right - s.left - wd) / 2);
	short top = (short) (20 + (s.bottom - s.top - 20 - ht) / 3);

	if (IsSystem7())
		top += 10;                  /* room for the movable dialog's title bar */
	MoveWindow(w, left, top, false);
}

#define kTopMargin 12

void DropHeading(DialogPtr d, short heading)
{
	DialogPeek dp = (DialogPeek) d;
	short count = **(short **) dp->items + 1, i, type, top = 32767, dy;
	Handle h;
	Rect r;

	if (!IsSystem7())
		return;
	HideDialogItem(d, heading);
	for (i = 1; i <= count; i++)
		if (i != heading) {
			GetDialogItem(d, i, &type, &h, &r);
			if (r.top < top)
				top = r.top;
		}
	dy = (short) (top - kTopMargin);
	if (dy <= 0)
		return;
	for (i = 1; i <= count; i++) {
		if (i == heading)
			continue;
		GetDialogItem(d, i, &type, &h, &r);
		OffsetRect(&r, 0, (short) -dy);
		SetDialogItem(d, i, type, h, &r);
		if (type & ctrlItem) {
			ControlHandle c = (ControlHandle) h;
			MoveControl(c, (*c)->contrlRect.left, (short) ((*c)->contrlRect.top - dy));
		}
	}
	if (dp->editField >= 0 && dp->textH != NULL) {
		OffsetRect(&(*dp->textH)->destRect, 0, (short) -dy);
		OffsetRect(&(*dp->textH)->viewRect, 0, (short) -dy);
	}
	SizeWindow(d, (short) (d->portRect.right - d->portRect.left), (short) (d->portRect.bottom - d->portRect.top - dy), false);
	CenterWindow(d);
}

static pascal void DrawOutline(DialogPtr d, short item)
{
	short type;
	Handle h;
	Rect r;

	(void) item;
	GetDialogItem(d, kOK, &type, &h, &r);
	InsetRect(&r, -4, -4);
	PenSize(3, 3);
	FrameRoundRect(&r, 16, 16);
	PenNormal();
}

void OutlineDefault(DialogPtr d, short item)
{
	short type;
	Handle h;
	Rect r;

	GetDialogItem(d, item, &type, &h, &r);
	SetDialogItem(d, item, userItem, (Handle) DrawOutline, &r);
}

/* 1 when a dialog's item is a push button */
static Boolean IsButton(DialogPtr d, short item)
{
	short type;
	Handle h;
	Rect r;

	/* the item list starts with the count less one (CountDITL is System 7's) */
	if (item > **(short **) ((DialogPeek) d)->items + 1)
		return false;
	GetDialogItem(d, item, &type, &h, &r);
	return (type & ~itemDisable) == ctrlItem + btnCtrl;
}

void FlashButton(DialogPtr d, short item)
{
	short type;
	Handle h;
	Rect r;
	long t;

	if (!IsButton(d, item))
		return;
	GetDialogItem(d, item, &type, &h, &r);
	HiliteControl((ControlHandle) h, 1);
	Delay(8, &t);
	HiliteControl((ControlHandle) h, 0);
}

pascal Boolean StandardFilter(DialogPtr d, EventRecord *e, short *item)
{
	if (e->what == updateEvt && (WindowPtr) e->message != d) {
		if (gUpdateHook != NULL)
			gUpdateHook((WindowPtr) e->message);
		return false;
	}
	if (e->what == keyDown || e->what == autoKey) {
		char c = (char) (e->message & charCodeMask);
		if (c == '\r' || c == 0x03) {
			FlashButton(d, kOK);
			*item = kOK;
			return true;
		}
		if (c == 0x1B || ((e->modifiers & cmdKey) && c == '.')) {
			short cancelItem = IsButton(d, kCancel) ? kCancel : kOK;   /* OK-only alerts */
			FlashButton(d, cancelItem);
			*item = cancelItem;
			return true;
		}
		if ((e->modifiers & cmdKey) && ((DialogPeek) d)->editField < 0) {
			e->what = nullEvent;            /* an alert: no text to edit */
			return false;
		}
		if (e->modifiers & cmdKey) {
			TEHandle te = ((DialogPeek) d)->textH;
			switch (c) {
			case 'x': case 'X': TECut(te); TEScrapToClipboard(); break;
			case 'c': case 'C': TECopy(te); TEScrapToClipboard(); break;
			case 'v': case 'V':
				if (TEScrapFromClipboard())
					TEPaste(te);
				break;
			case 'a': case 'A':
				SelectDialogItemText(d, ((DialogPeek) d)->editField + 1, 0, 32767);
				break;
			}
			e->what = nullEvent;
			return false;
		}
	}
	return false;
}

void GetItemText(DialogPtr d, short item, char *out, short cap)
{
	short type;
	Handle h;
	Rect r;
	Str255 s;

	GetDialogItem(d, item, &type, &h, &r);
	GetDialogItemText(h, s);
	if (s[0] >= cap)
		s[0] = (unsigned char) (cap - 1);
	CStr(out, s);
}

void SetItemText(DialogPtr d, short item, const char *text)
{
	short type;
	Handle h;
	Rect r;
	Str255 s;

	GetDialogItem(d, item, &type, &h, &r);
	PStr(s, text);
	SetDialogItemText(h, s);
}

static const char *Reason(OSErr err)
{
	switch (err) {
	case dskFulErr: return "The disk is full.";
	case wPrErr:
	case vLckdErr: return "The disk is locked.";
	case fLckdErr: return "The file is locked.";
	case permErr:
	case wrPermErr:
	case kAfpAccessDenied: return "You don\xD5t have permission to change it.";
	case fBsyErr:
	case opWrErr: return "The file is in use.";
	case fnfErr:
	case dirNFErr: return "It couldn\xD5t be found.";
	case nsvErr:
	case volOffLinErr: return "The disk isn\xD5t available.";
	case ioErr: return "There was a problem reading or writing the disk.";
	case memFullErr: return "There isn\xD5t enough memory. Close a window and try again.";
	case bdNamErr: return "The name can\xD5t be used.";
	}
	return NULL;
}

void ErrorAlert(const char *what, OSErr err)
{
	char msg[256];
	const char *why = Reason(err);
	Str255 p;

	if (why != NULL)
		tp_sprintf(msg, "%s %s", what, why);
	else
		tp_sprintf(msg, "%s (error %d)", what, (int) err);
	PStr(p, msg);
	ParamText(p, "\p", "\p", "\p");
	InitCursor();
	NoDialogHelp();
	StopAlert(kErrorAlert, (ModalFilterUPP) StandardFilter);
}

void ShowNote(const char *msg)
{
	Str255 p;

	PStr(p, msg);
	ParamText(p, "\p", "\p", "\p");
	InitCursor();
	NoDialogHelp();
	NoteAlert(kErrorAlert, (ModalFilterUPP) StandardFilter);
}

#ifdef TOPICA_DEBUG
void DebugLog(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	short ref;
	long n;

	va_start(ap, fmt);
	tp_vsprintf(line, fmt, ap);
	va_end(ap);
	strcat(line, "\r");
	if (FSOpen("\pTopica Log", 0, &ref) != noErr) {
		if (Create("\pTopica Log", 0, 'ttxt', 'TEXT') != noErr || FSOpen("\pTopica Log", 0, &ref) != noErr)
			return;
	}
	(void) SetFPos(ref, fsFromLEOF, 0);
	n = (long) strlen(line);
	(void) FSWrite(ref, &n, line);
	(void) FSClose(ref);
	(void) FlushVol(NULL, 0);
}
#endif

Boolean ClockDate(DateTimeRec *dt)
{
	unsigned long secs;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"   /* the clock is a low-memory global */
	GetDateTime(&secs);
#pragma GCC diagnostic pop
	SecondsToDate(secs, dt);
	/* System 6's Control Panel takes a year in two digits, 20 to 19: a
	   year set as 26 is 1926 to the Mac. The clock runs out in 2040. */
	if (dt->year >= 1920 && dt->year < 1940)
		dt->year += 100;
	return dt->year >= 1990;
}

void Today(char out[16])
{
	DateTimeRec dt;

	if (!ClockDate(&dt))
		out[0] = '\0';
	else
		tp_sprintf(out, "%04d-%02d-%02d", dt.year % 10000, dt.month % 100, dt.day % 100);
}
