/*
 * Topica: a Markdown writer for blogs, for the classic Macintosh.
 * The event loop, the menus, desk accessories, and the files the Finder
 * asks us to open. The document windows (docwin.c) do the rest.
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <SegLoad.h>
#include <Devices.h>
#include <string.h>

#include "../core/undo.h"
#include "docwin.h"
#include "fileio.h"
#include "postinfo.h"
#include "util.h"

enum { kAppleMenu = 128, kFileMenu = 129, kEditMenu = 130, kPostMenu = 131 };
enum { kAboutItem = 1 };
enum { kNewItem = 1, kOpenItem = 2, kCloseItem = 4, kSaveItem = 5, kSaveAsItem = 6, kRevertItem = 7, kQuitItem = 9 };
enum { kUndoItem = 1, kRedoItem = 2, kCutItem = 4, kCopyItem = 5, kPasteItem = 6, kClearItem = 7, kSelectAllItem = 9 };
enum { kPostInfoItem = 1, kInsertImageItem = 3 };
/* osEvt's suspend/resume (Inside Macintosh VI) */
enum { kSuspendResumeMessage = 1, kResumeFlag = 1 };

static Boolean gQuit;
static Boolean gInBackground;

static const char *const kUndoNames[kUndoKinds] = {
	"", "Typing", "Delete", "Cut", "Paste", "Clear", "Post Info", "Insert", "Replace"
};

static void Enable(MenuHandle m, short item, Boolean on)
{
	if (on)
		EnableItem(m, item);
	else
		DisableItem(m, item);
}

static void SetUndoItem(MenuHandle m, short item, int kind, Boolean redo)
{
	char s[40];
	Str255 p;

	if (kind == kUndoNone) {
		strcpy(s, redo ? "Can't Redo" : "Can't Undo");
	} else {
		strcpy(s, redo ? "Redo " : "Undo ");
		strcat(s, kUndoNames[kind]);
	}
	PStr(p, s);
	SetMenuItemText(m, item, p);
	Enable(m, item, kind != kUndoNone);
}

static Boolean IsDA(WindowPtr w)
{
	return w != NULL && ((WindowPeek) w)->windowKind < 0;
}

/* What the menus offer depends on the front window */
static void AdjustMenus(void)
{
	WindowPtr front = FrontWindow();
	Doc *d = DocFront();
	Boolean da = IsDA(front);
	MenuHandle file = GetMenuHandle(kFileMenu), edit = GetMenuHandle(kEditMenu), post = GetMenuHandle(kPostMenu);
	Boolean sel = d != NULL && DocHasSelection(d);

	Enable(file, kNewItem, DocCount() < DocMaxCount());
	Enable(file, kOpenItem, DocCount() < DocMaxCount());
	Enable(file, kCloseItem, d != NULL || da);
	Enable(file, kSaveItem, d != NULL && (DocIsDirty(d) || !DocHasFile(d)));
	Enable(file, kSaveAsItem, d != NULL);
	Enable(file, kRevertItem, d != NULL && DocHasFile(d) && DocIsDirty(d));
	if (d != NULL) {
		SetUndoItem(edit, kUndoItem, DocUndoKind(d, false), false);
		SetUndoItem(edit, kRedoItem, DocUndoKind(d, true), true);
	} else {
		SetUndoItem(edit, kUndoItem, kUndoNone, false);
		SetUndoItem(edit, kRedoItem, kUndoNone, true);
		Enable(edit, kUndoItem, da);
	}
	Enable(edit, kCutItem, sel || da);
	Enable(edit, kCopyItem, sel || da);
	Enable(edit, kPasteItem, d != NULL || da);
	Enable(edit, kClearItem, sel || da);
	Enable(edit, kSelectAllItem, d != NULL);
	if (((**post).enableFlags & 1) != (d != NULL)) {     /* the whole menu */
		Enable(post, 0, d != NULL);
		DrawMenuBar();
	}
}

static void DoQuit(void)
{
	Doc *d;

	/* close the documents front to back; a Cancel stops the quit */
	while ((d = DocFront()) != NULL || DocCount() > 0) {
		if (d == NULL) {
			/* a desk accessory is in front: find a document behind it */
			WindowPtr w = FrontWindow();
			while (w != NULL && DocFromWindow(w) == NULL)
				w = (WindowPtr) ((WindowPeek) w)->nextWindow;
			if (w == NULL)
				break;
			d = DocFromWindow(w);
		}
		if (!DocClose(d, true))
			return;
	}
	gQuit = true;
}

static void DoClose(void)
{
	WindowPtr w = FrontWindow();
	Doc *d = DocFromWindow(w);

	if (IsDA(w))
		CloseDeskAcc(((WindowPeek) w)->windowKind);
	else if (d != NULL)
		(void) DocClose(d, false);
}

static void DoOpen(void)
{
	FileRef f;

	if (FileAskOpen(&f))
		(void) DocOpen(&f);
}

static void DoMenu(long choice)
{
	short menu = HiWord(choice), item = LoWord(choice);
	Doc *d = DocFront();

	HiliteMenu(0);                      /* before any dialog or alert the item opens */
	switch (menu) {
	case kAppleMenu:
		if (item == kAboutItem) {
			InitCursor();
			(void) Alert(kAboutAlert, NULL);
		} else {
			Str255 name;
			GetMenuItemText(GetMenuHandle(kAppleMenu), item, name);
			(void) OpenDeskAcc(name);
		}
		break;
	case kFileMenu:
		switch (item) {
		case kNewItem:
			if ((d = DocNew(true)) != NULL)
				(void) PostInfo(d);
			break;
		case kOpenItem: DoOpen(); break;
		case kCloseItem: DoClose(); break;
		case kSaveItem: if (d != NULL) (void) DocSave(d); break;
		case kSaveAsItem: if (d != NULL) (void) DocSaveAs(d); break;
		case kRevertItem: if (d != NULL) DocRevert(d); break;
		case kQuitItem: DoQuit(); break;
		}
		break;
	case kEditMenu: {
		/* a desk accessory's Edit menu: Undo, Cut, Copy, Paste, Clear */
		static const short kSystemEdit[] = { -1, 0, -1, -1, 2, 3, 4, 5 };
		if (item < (short) (sizeof(kSystemEdit) / sizeof(kSystemEdit[0])) && kSystemEdit[item] >= 0
				&& SystemEdit(kSystemEdit[item]))
			break;
		if (d == NULL)
			break;
		switch (item) {
		case kUndoItem: DocUndo(d, false); break;
		case kRedoItem: DocUndo(d, true); break;
		case kCutItem: DocEdit(d, kEdCut); break;
		case kCopyItem: DocEdit(d, kEdCopy); break;
		case kPasteItem: DocEdit(d, kEdPaste); break;
		case kClearItem: DocEdit(d, kEdClear); break;
		case kSelectAllItem: DocEdit(d, kEdSelectAll); break;
		}
		break;
	}
	case kPostMenu:
		if (d == NULL)
			break;
		DocSealUndo(d);
		if (item == kPostInfoItem)
			(void) PostInfo(d);
		else if (item == kInsertImageItem)
			InsertImage(d);
		break;
	}
	HiliteMenu(0);
}

static void DoMouseDown(EventRecord *e)
{
	WindowPtr w;
	short part = FindWindow(e->where, &w);
	Doc *d = DocFromWindow(w);

	switch (part) {
	case inMenuBar:
		AdjustMenus();
		DoMenu(MenuSelect(e->where));
		break;
	case inSysWindow:
		SystemClick(e, w);
		break;
	case inDrag: {
		Rect bounds = qd.screenBits.bounds;
		InsetRect(&bounds, 4, 4);
		DragWindow(w, e->where, &bounds);
		break;
	}
	case inGrow:
		if (d != NULL)
			DocGrow(d, e->where);
		break;
	case inZoomIn:
	case inZoomOut:
		if (d != NULL)
			DocZoom(d, e->where, part);
		break;
	case inGoAway:
		if (d != NULL && TrackGoAway(w, e->where))
			(void) DocClose(d, false);
		break;
	case inContent:
		if (w != FrontWindow())
			SelectWindow(w);
		else if (d != NULL)
			DocClick(d, e);
		break;
	}
}

static void DoKey(EventRecord *e)
{
	char c = (char) (e->message & charCodeMask);
	Doc *d = DocFront();

	if (e->modifiers & cmdKey) {
		/* Command-Shift-Z: Redo (MenuKey doesn't see Shift) */
		if ((c == 'z' || c == 'Z') && (e->modifiers & shiftKey) && d != NULL) {
			DocUndo(d, true);
			return;
		}
		if (e->what == autoKey)
			return;                     /* no repeating menu commands */
		AdjustMenus();
		DoMenu(MenuKey(c));
		return;
	}
	if (d != NULL)
		DocKey(d, e);
}

static void Update(WindowPtr w)
{
	Doc *d = DocFromWindow(w);

	if (d != NULL) {
		DocUpdate(d);
	} else {
		BeginUpdate(w);
		EndUpdate(w);
	}
}

/* The files the Finder asked us to open (System 6, and System 7 without
   Apple events); none: a new post */
static void OpenFinderFiles(void)
{
	short message, count, i;
	AppFile af;
	FileRef f;
	Boolean opened = false;

	CountAppFiles(&message, &count);
	for (i = 1; i <= count; i++) {
		GetAppFiles(i, &af);
		f.vRefNum = af.vRefNum;
		memcpy(f.name, af.fName, (size_t) (af.fName[0] > 63 ? 63 : af.fName[0]) + 1);
		if (DocOpen(&f) != NULL)
			opened = true;
		ClrAppFiles(i);
	}
	if (!opened && message == appOpen)
		(void) DocNew(true);
	if (message == appPrint)
		gQuit = true;                   /* nothing to print with yet */
}

static void Setup(void)
{
	InitGraf(&qd.thePort);
	InitFonts();
	FlushEvents(everyEvent, 0);
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	InitCursor();
	MaxApplZone();
	MoreMasters();
	MoreMasters();
	MoreMasters();
	SetMenuBar(GetNewMBar(128));
	AppendResMenu(GetMenuHandle(kAppleMenu), 'DRVR');
	DrawMenuBar();
	gUpdateHook = Update;
}

static Boolean GetEvent(EventRecord *e, long sleep)
{
	if (HasWaitNextEvent())
		return WaitNextEvent(everyEvent, e, sleep, NULL);
	SystemTask();
	return GetNextEvent(everyEvent, e);
}

int main(void)
{
	EventRecord e;

	Setup();
	OpenFinderFiles();
	while (!gQuit) {
		Doc *front = DocFront();
		if (GetEvent(&e, gInBackground ? 60 : 10)) {
			switch (e.what) {
			case mouseDown:
				DoMouseDown(&e);
				break;
			case keyDown:
			case autoKey:
				DoKey(&e);
				break;
			case updateEvt:
				Update((WindowPtr) e.message);
				break;
			case activateEvt: {
				Doc *d = DocFromWindow((WindowPtr) e.message);
				if (d != NULL)
					DocActivate(d, (e.modifiers & activeFlag) != 0);
				break;
			}
			case osEvt:
				if (((e.message >> 24) & 0xFF) == kSuspendResumeMessage) {
					gInBackground = (e.message & kResumeFlag) == 0;
					if (front != NULL)
						DocActivate(front, !gInBackground);
				}
				break;
			}
		}
		front = DocFront();
		if (front != NULL && !gInBackground) {
			DocIdle(front);
			DocAdjustCursor(front, e.where);
		} else if (!gInBackground && !IsDA(FrontWindow())) {
			InitCursor();
		}
	}
	return 0;
}
