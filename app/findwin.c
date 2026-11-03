/* findwin.c: see findwin.h. */
#include <Quickdraw.h>
#include <Dialogs.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <string.h>

#include "../core/find.h"
#include "../core/undo.h"
#include "findwin.h"
#include "util.h"

/* The dialog's items */
enum {
	kFindButton = 1, kOutline = 3, kHeading = 4, kFindField = 5, kReplaceField = 6,
	kCaseBox = 7, kWordBox = 8, kReplaceButton = 9, kReplaceAllButton = 10
};

static char gFind[256];
static char gReplace[256];
static int gFlags;

Boolean FindHasPattern(void)
{
	return gFind[0] != '\0';
}

/* Selects the next match after the selection; 0 (with a beep) if none */
static Boolean FindNext(Doc *d)
{
	Editor *ed = DocEditor(d);
	long s, e, at;
	const char *t;

	if (gFind[0] == '\0')
		return false;
	EdGetSelection(ed, &s, &e);
	t = EdLockText(ed);
	at = find_next(t, EdLength(ed), gFind, (long) strlen(gFind), e, gFlags);
	EdUnlockText(ed);
	if (at < 0) {
		SysBeep(10);
		return false;
	}
	EdSetSelection(ed, at, at + (long) strlen(gFind));
	EdShowSelection(ed);
	DocSealUndo(d);
	return true;
}

/* Replaces the selection when it is a match */
static Boolean ReplaceSelection(Doc *d)
{
	Editor *ed = DocEditor(d);
	long s, e;
	const char *t;
	int match;

	EdGetSelection(ed, &s, &e);
	t = EdLockText(ed);
	match = e - s == (long) strlen(gFind) && find_is_match(t, EdLength(ed), gFind, e - s, s, gFlags);
	EdUnlockText(ed);
	if (!match)
		return false;
	if (!DocReplace(d, kUndoReplace, s, e, gReplace, (long) strlen(gReplace)))
		return false;
	DocSealUndo(d);
	return true;
}

/* Every match replaced, as one undoable change of just the span that differs */
static void ReplaceAll(Doc *d)
{
	Editor *ed = DocEditor(d);
	long n = EdLength(ed), len, count, a, b, c;
	const char *t;
	Ptr out;

	t = EdLockText(ed);
	len = find_replace_all(t, n, gFind, (long) strlen(gFind), gReplace, (long) strlen(gReplace), gFlags,
		NULL, 0, &count);
	EdUnlockText(ed);
	if (count == 0) {
		SysBeep(10);
		return;
	}
	if (len > EdMaxLength()) {
		ShowNote("Replacing them all would make the post too long.");
		return;
	}
	out = NewPtr(len > 0 ? len : 1);
	if (out == NULL) {
		SysBeep(10);
		return;
	}
	t = EdLockText(ed);
	(void) find_replace_all(t, n, gFind, (long) strlen(gFind), gReplace, (long) strlen(gReplace), gFlags,
		out, len, &count);
	for (a = 0; a < n && a < len && t[a] == out[a]; a++)
		;
	for (b = n, c = len; b > a && c > a && t[b - 1] == out[c - 1]; b--, c--)
		;
	EdUnlockText(ed);
	(void) DocReplace(d, kUndoReplace, a, b, out + a, c - a);
	DocSealUndo(d);
	DisposePtr(out);
}

static void SetBox(DialogPtr dlg, short item, Boolean on)
{
	short type;
	Handle h;
	Rect r;

	GetDialogItem(dlg, item, &type, &h, &r);
	SetControlValue((ControlHandle) h, on ? 1 : 0);
}

void FindDialog(Doc *d)
{
	DialogPtr dlg = GetModalDialog(kFindDialog);
	short item;
	int flags = gFlags;

	if (dlg == NULL) {
		SysBeep(10);
		return;
	}
	DropHeading(dlg, kHeading);
	OutlineDefault(dlg, kOutline);
	SetItemText(dlg, kFindField, gFind);
	SetItemText(dlg, kReplaceField, gReplace);
	SetBox(dlg, kCaseBox, (flags & kFindCase) != 0);
	SetBox(dlg, kWordBox, (flags & kFindWord) != 0);
	ShowWindow(dlg);
	SelectDialogItemText(dlg, kFindField, 0, 32767);
	InitCursor();
	for (;;) {
		ModalDialog((ModalFilterUPP) StandardFilter, &item);
		if (item == kCaseBox || item == kWordBox) {
			flags ^= item == kCaseBox ? kFindCase : kFindWord;
			SetBox(dlg, item, (flags & (item == kCaseBox ? kFindCase : kFindWord)) != 0);
			continue;
		}
		if (item == kFindButton || item == kCancel || item == kReplaceButton || item == kReplaceAllButton)
			break;
	}
	if (item != kCancel) {
		GetItemText(dlg, kFindField, gFind, (short) sizeof(gFind));
		GetItemText(dlg, kReplaceField, gReplace, (short) sizeof(gReplace));
		gFlags = flags;
	}
	DisposeDialog(dlg);
	if (item == kCancel || gFind[0] == '\0')
		return;
	SetPort(FrontWindow());
	if (item == kFindButton)
		(void) FindNext(d);
	else if (item == kReplaceButton)
		ReplaceAndFindAgain(d);
	else
		ReplaceAll(d);
}

void FindAgain(Doc *d)
{
	(void) FindNext(d);
}

void ReplaceAndFindAgain(Doc *d)
{
	(void) ReplaceSelection(d);
	(void) FindNext(d);
}
