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
#include "ae.h"
#include "docwin.h"
#include "fileio.h"
#include "findwin.h"
#include "headings.h"
#include "net.h"
#include "postinfo.h"
#include "preview.h"
#include "print.h"
#include "publishwin.h"
#include "settings.h"
#include "spellwin.h"
#include "util.h"

enum { kAppleMenu = 128, kFileMenu = 129, kEditMenu = 130, kPostMenu = 131 };
enum { kAboutItem = 1 };
enum { kNewItem = 1, kOpenItem = 2, kCloseItem = 4, kSaveItem = 5, kSaveAsItem = 6, kExportItem = 7, kRevertItem = 8,
	kPageSetupItem = 10, kPrintItem = 11, kQuitItem = 13 };
enum { kUndoItem = 1, kRedoItem = 2, kCutItem = 4, kCopyItem = 5, kPasteItem = 6, kClearItem = 7, kSelectAllItem = 9,
	kFindItem = 11, kFindAgainItem = 12, kReplaceAgainItem = 13, kSpellItem = 14, kPrefsItem = 16 };
enum { kPostInfoItem = 1, kInsertImageItem = 3, kInsertLinkItem = 4, kFormattingItem = 6, kPreviewItem = 7,
	kPublishItem = 9 };
/* osEvt's suspend/resume (Inside Macintosh VI) */
enum { kSuspendResumeMessage = 1, kResumeFlag = 1 };

static Boolean gQuit;
static Boolean gInBackground;
static Boolean gHaveAE;

static const char *const kUndoNames[kUndoKinds] = {
	"", "Typing", "Delete", "Cut", "Paste", "Clear", "Post Info", "Insert", "Replace", "Change"
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
		strcpy(s, redo ? "Can\xD5t Redo" : "Can\xD5t Undo");
	} else {
		strcpy(s, redo ? "Redo " : "Undo ");
		strcat(s, kUndoNames[kind]);
	}
	PStr(p, s);
	SetMenuItemText(m, item, p);
	Enable(m, item, kind != kUndoNone);
}

/* A whole menu on or off: 1 if that changed it */
static Boolean EnableMenu(MenuHandle m, Boolean on)
{
	if (((**m).enableFlags & 1) == (unsigned long) on)
		return false;
	Enable(m, 0, on);
	return true;
}

static Boolean IsDA(WindowPtr w)
{
	return w != NULL && ((WindowPeek) w)->windowKind < 0;
}

/* The post the File and Post menus act on: the front one, or the one the
   preview shows when it's in front */
static Doc *Target(void)
{
	Doc *d = DocFront();

	return d != NULL ? d : PreviewIsFront() ? PreviewDoc() : NULL;
}

/* What the menus offer depends on the front window */
static void AdjustMenus(void)
{
	WindowPtr front = FrontWindow();
	Doc *d = DocFront(), *target = Target();
	Boolean da = IsDA(front);
	MenuHandle file = GetMenuHandle(kFileMenu), edit = GetMenuHandle(kEditMenu), post = GetMenuHandle(kPostMenu);
	Boolean sel = d != NULL && DocHasSelection(d);

	Enable(file, kNewItem, DocCount() < DocMaxCount());
	Enable(file, kOpenItem, DocCount() < DocMaxCount());
	Enable(file, kCloseItem, d != NULL || da || PreviewIsFront());
	Enable(file, kSaveItem, d != NULL && (DocIsDirty(d) || !DocHasFile(d)));
	Enable(file, kSaveAsItem, d != NULL);
	Enable(file, kExportItem, target != NULL);
	Enable(file, kRevertItem, d != NULL && DocHasFile(d) && DocIsDirty(d));
	Enable(file, kPageSetupItem, true);
	Enable(file, kPrintItem, target != NULL);
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
	Enable(edit, kFindItem, d != NULL);
	Enable(edit, kFindAgainItem, d != NULL && FindHasPattern());
	Enable(edit, kReplaceAgainItem, d != NULL && FindHasPattern());
	Enable(edit, kSpellItem, d != NULL);
	Enable(post, kPostInfoItem, d != NULL);
	Enable(post, kInsertImageItem, d != NULL);
	Enable(post, kInsertLinkItem, d != NULL);
	Enable(post, kFormattingItem, d != NULL && DocCanStyle(d));
	SetMenuItemText(post, kFormattingItem, gPrefs.format ? "\pHide Formatting" : "\pShow Formatting");
	SetMenuItemText(post, kPreviewItem, PreviewIsFront() ? "\pHide Preview" : "\pShow Preview");
	Enable(post, kPreviewItem, target != NULL);
	Enable(post, kPublishItem, d != NULL);
	/* whole menus */
	if (EnableMenu(post, target != NULL) | EnableMenu(GetMenuHandle(kHeadingsMenu), d != NULL))
		DrawMenuBar();
}

static Boolean DoQuit(void)
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
			return false;
	}
	gQuit = true;
	return true;
}

static void DoClose(void)
{
	WindowPtr w = FrontWindow();
	Doc *d = DocFromWindow(w);

	if (IsDA(w))
		CloseDeskAcc(((WindowPeek) w)->windowKind);
	else if (IsPreview(w))
		PreviewClose();
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
			(void) Alert(kAboutAlert, (ModalFilterUPP) StandardFilter);
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
		case kExportItem: if (Target() != NULL) ExportHTML(Target()); break;
		case kRevertItem: if (d != NULL) DocRevert(d); break;
		case kPageSetupItem: PrintPageSetup(); break;
		case kPrintItem: if (Target() != NULL) PrintDoc(Target()); break;
		case kQuitItem: (void) DoQuit(); break;
		}
		break;
	case kEditMenu: {
		/* a desk accessory's Edit menu: Undo, Cut, Copy, Paste, Clear */
		static const short kSystemEdit[] = { -1, 0, -1, -1, 2, 3, 4, 5 };
		if (item < (short) (sizeof(kSystemEdit) / sizeof(kSystemEdit[0])) && kSystemEdit[item] >= 0
				&& SystemEdit(kSystemEdit[item]))
			break;
		if (item == kPrefsItem) {
			if (SettingsDialog())
				DocApplySettings();
			break;
		}
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
		case kFindItem: FindDialog(d); break;
		case kFindAgainItem: FindAgain(d); break;
		case kReplaceAgainItem: ReplaceAndFindAgain(d); break;
		case kSpellItem: SpellCheck(d); break;
		}
		break;
	}
	case kPostMenu:
		if (item == kPreviewItem) {
			if (PreviewIsFront())
				PreviewClose();
			else if (d != NULL)
				PreviewShow(d);
			break;
		}
		if (d == NULL)
			break;
		DocSealUndo(d);
		if (item == kPostInfoItem) {
			(void) PostInfo(d);
		} else if (item == kInsertImageItem) {
			InsertImage(d);
		} else if (item == kInsertLinkItem) {
			InsertLink(d);
		} else if (item == kPublishItem) {
			PublishPost(d);
		} else if (item == kFormattingItem) {
			gPrefs.format = !gPrefs.format;
			SettingsSave();
			DocRestyleAll();
		}
		break;
	case kHeadingsMenu:
		HeadingsGo(d, item);
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
		HeadingsMenu(DocFront());
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
		else if (IsPreview(w))
			PreviewGrow(e->where);
		break;
	case inZoomIn:
	case inZoomOut:
		if (d != NULL)
			DocZoom(d, e->where, part);
		else if (IsPreview(w))
			PreviewZoom(e->where, part);
		break;
	case inGoAway:
		if (!TrackGoAway(w, e->where))
			break;
		if (d != NULL)
			(void) DocClose(d, false);
		else if (IsPreview(w))
			PreviewClose();
		break;
	case inContent:
		if (w != FrontWindow())
			SelectWindow(w);
		else if (d != NULL)
			DocClick(d, e);
		else if (IsPreview(w))
			PreviewClick(e);
		break;
	}
}

static void DoKey(EventRecord *e)
{
	char c = (char) (e->message & charCodeMask);
	Doc *d = DocFront();

	if (e->modifiers & cmdKey) {
		/* Command-period: vi's Escape */
		if (c == '.' && d != NULL && DocViOn(d)) {
			DocViEscape(d);
			return;
		}
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
	else if (PreviewIsFront())
		PreviewKey(e);
}

static void Update(WindowPtr w)
{
	Doc *d = DocFromWindow(w);

	if (d != NULL) {
		DocUpdate(d);
	} else if (IsPreview(w)) {
		PreviewUpdate();
	} else {
		BeginUpdate(w);
		EndUpdate(w);
	}
}

/* Prints a post, closing it after unless it was already open */
static void PrintFileEvent(const FileRef *f)
{
	short before = DocCount();
	Doc *d = DocOpen(f);

	if (d == NULL)
		return;
	PrintDoc(d);
	if (DocCount() > before)
		(void) DocClose(d, false);
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
		memset(&f, 0, sizeof(f));       /* dirID 0: vRefNum is a working directory */
		f.vRefNum = af.vRefNum;
		memcpy(f.name, af.fName, (size_t) (af.fName[0] > 63 ? 63 : af.fName[0]) + 1);
		if (message == appPrint)
			PrintFileEvent(&f);
		else if (DocOpen(&f) != NULL)
			opened = true;
		ClrAppFiles(i);
	}
	/* with Apple events, the open application event makes the new post */
	if (!opened && message == appOpen && !gHaveAE && DocCount() == 0)
		(void) DocNew(true);
	if (message == appPrint)
		gQuit = true;                   /* the Finder's Print quits when done */
}

static void OpenAppEvent(void)
{
	if (DocCount() == 0)
		(void) DocNew(true);
}

static void OpenFileEvent(const FileRef *f)
{
	(void) DocOpen(f);
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
	FileRememberAppFolder();            /* before Standard File can change the default folder */
	MaxApplZone();
	MoreMasters();
	MoreMasters();
	MoreMasters();
	SetMenuBar(GetNewMBar(128));
	AppendResMenu(GetMenuHandle(kAppleMenu), 'DRVR');
	DrawMenuBar();
	gUpdateHook = Update;
	SettingsLoad();
	gHaveAE = AppleEventsInit(DoQuit, OpenAppEvent, OpenFileEvent, PrintFileEvent);
}

/* Posts a crash left unsaved come back first, before anything else opens */
static void Recover(void)
{
	short n = DocRecoverAll();
	EventRecord e;

	while (GetNextEvent(updateMask, &e))   /* the windows first, then the note */
		Update((WindowPtr) e.message);
	if (n > 0)
		ShowNote(n == 1
			? "Topica found a post that wasn\xD5t saved when it last quit. It\xD5s open now: save it or close it."
			: "Topica found posts that weren\xD5t saved when it last quit. They\xD5re open now: save them or close them.");
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
	Recover();
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
				else if (IsPreview((WindowPtr) e.message))
					PreviewActivate((e.modifiers & activeFlag) != 0);
				if (e.modifiers & activeFlag)
					AdjustMenus();      /* whole menus dim or not with the window */
				break;
			}
			case kHighLevelEvent:
				AppleEventsHandle(&e);
				break;
			case osEvt:
				if (((e.message >> 24) & 0xFF) == kSuspendResumeMessage) {
					gInBackground = (e.message & kResumeFlag) == 0;
					if (front != NULL)
						DocActivate(front, !gInBackground);
					else if (PreviewIsFront())
						PreviewActivate(!gInBackground);
				}
				break;
			}
		}
		DocIdleAll();
		PreviewIdle();
		NetIdle();
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
