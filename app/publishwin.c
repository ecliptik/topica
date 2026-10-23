/* publishwin.c: see publishwin.h. */
#include <Quickdraw.h>
#include <Dialogs.h>
#include <Multiverse.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

#include "../core/frontmatter.h"
#include "../core/publish.h"
#include "../core/textconv.h"
#include "editor.h"
#include "mactcp.h"
#include "net.h"
#include "publishwin.h"
#include "settings.h"
#include "util.h"

/* the dialog's items */
enum {
	kPublishButton = 1, kOutline = 3, kPathField = 4, kDraftBox = 5, kBridgeField = 6, kKeyField = 7,
	kHeading = 11, kAbout = 12
};

enum { kHeadCap = 600, kReplyCap = 1024, kTimeout = 20 };
enum { kProgressCancel = 2 };   /* the progress dialog's Cancel button */

static DialogPtr gProgress;

/* The key as typed: its field shows a bullet for each character */
static char gKey[64];

static void Value(Doc *d, const char *key, char *out, long cap)
{
	Editor *ed = DocEditor(d);
	const char *t = EdLockText(ed);

	if (fm_get(t, EdLength(ed), key, out, cap) != kFmFound)
		out[0] = '\0';
	EdUnlockText(ed);
}

static ControlHandle ItemControl(DialogPtr dlg, short item)
{
	short type;
	Handle h;
	Rect r;

	GetDialogItem(dlg, item, &type, &h, &r);
	return (ControlHandle) h;
}

/* The path the post would have, as a draft or not */
static void SuggestPath(Doc *d, Boolean draft, char *out, int cap)
{
	char title[256], date[64], name[64];
	Str255 w;

	Value(d, "title", title, sizeof(title));
	Value(d, "date", date, sizeof(date));
	if (date[0] == '\0')
		Today(date);
	GetWTitle(DocWindow(d), w);
	CStr(name, w);
	pub_path(gPrefs.profile == kProfileHugo, draft, gPrefs.yearFolders, name, title, date, out, cap);
}

/* What went wrong with the connection, in words that say what to do */
static void Problem(OSErr err, const char *bridge, char *out)
{
	switch (err) {
	case netNoAddress:
		strcpy(out, "No address from the MacIP gateway. Is it running?");
		break;
	case netNoNetwork:
#ifdef TOPICA_TALKIP
		strcpy(out, "No network: AppleTalk is off, or there\xD5s no MacIP gateway.");
#else
		strcpy(out, "No network: MacTCP isn\xD5t installed or set up.");
#endif
		break;
	case memFullErr:
		strcpy(out, "Not enough memory to publish. Close a window and try again.");
		break;
	case openFailed:
		sprintf(out, "The bridge at %s refused the connection. Is it running?", bridge);
		break;
	case commandTimeout:
	case connectionTerminated:
		sprintf(out, "Nothing answered at %s. Is the bridge running, and its port open?", bridge);
		break;
	default:
		sprintf(out, "The connection to %s failed (error %d).", bridge, err);
		break;
	}
}

static void Progress(DialogPtr p, const char *text)
{
	if (p == NULL)
		return;
	SetItemText(p, 1, text);
	DrawDialog(p);
}

/* Asked while the network waits: Command-period, Escape or Cancel ends
   the publishing. Windows are redrawn meanwhile. */
static Boolean Canceled(void)
{
	EventRecord e;
	DialogPtr d;
	WindowPtr w;
	short item;
	char c;

	if (!GetNextEvent(keyDownMask | mDownMask | updateMask, &e))
		return false;
	switch (e.what) {
	case keyDown:
		c = (char) (e.message & charCodeMask);
		if (c == 0x1B || ((e.modifiers & cmdKey) && c == '.')) {
			if (gProgress != NULL)
				FlashButton(gProgress, kProgressCancel);
			return true;
		}
		break;
	case mouseDown:
		if (gProgress != NULL && FindWindow(e.where, &w) == inContent && w == gProgress
				&& DialogSelect(&e, &d, &item) && item == kProgressCancel)
			return true;
		break;
	case updateEvt:
		w = (WindowPtr) e.message;
		if (w == gProgress) {
			BeginUpdate(w);
			DrawDialog(w);
			EndUpdate(w);
		} else if (gUpdateHook != NULL) {
			gUpdateHook(w);
		}
		break;
	}
	return false;
}

/* Sends the post and shows what the bridge said */
static void Send(Doc *d, unsigned long ip, unsigned short port, const char *bridge, const char *path)
{
	Editor *ed = DocEditor(d);
	const char *t;
	char host[20], line[300], reply[kReplyCap];
	long n, bodyLen, headLen, have = 0, got;
	Handle h;
	DialogPtr p;
	GrafPtr save;
	OSErr err;
	int status = 0, done = 0;

	GetPort(&save);
	sprintf(host, "%lu.%lu.%lu.%lu", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
	/* the request: its head, then the post as the web wants it (UTF-8, LF) */
	t = EdLockText(ed);
	n = EdLength(ed);
	bodyLen = tc_to_disk(t, n, NULL, 0, 1, 1);
	h = bodyLen >= 0 ? NewHandle(kHeadCap + bodyLen) : NULL;
	if (h == NULL) {
		EdUnlockText(ed);
		Problem(memFullErr, bridge, line);
		ShowNote(line);
		return;
	}
	HLock(h);
	headLen = pub_request(*h, kHeadCap, host, port, gPrefs.bridgeKey, path, bodyLen);
	(void) tc_to_disk(t, n, *h + headLen, bodyLen, 1, 1);
	EdUnlockText(ed);

	NoDialogHelp();
	p = GetNewDialog(kProgressDialog, NULL, (WindowPtr) -1);
	if (p != NULL) {
		CenterWindow(p);
		ShowWindow(p);
		SetPort(p);
	}
	gProgress = p;
	NetSetCancel(Canceled);
	SetCursor(*GetCursor(watchCursor));
	sprintf(line, "Connecting to the bridge at %s\xC9", bridge);
	Progress(p, line);
	err = NetOpen(ip, port, kTimeout);
	if (err == noErr) {
		sprintf(line, "Sending the post (%ldK)\xC9", (bodyLen + 1023) / 1024);
		Progress(p, line);
		err = NetSend(*h, headLen + bodyLen);
	}
	if (err == noErr) {
		Progress(p, "Waiting for the bridge to push the post\xC9");
		while (!done && err == noErr) {
			err = NetRecv(reply + have, kReplyCap - have, &got, 60);
			have += got;
			if (((err == connectionClosing || err == connectionTerminated) && have > 0) || have == kReplyCap) {
				done = pub_reply(reply, have, 1, &status, line, 256);
				err = noErr;
				if (done == 0)
					done = -1;
			} else if (err == noErr) {
				done = pub_reply(reply, have, 0, &status, line, 256);
			}
		}
	}
	NetClose();
	NetSetCancel(NULL);
	gProgress = NULL;
	DisposeHandle(h);
	if (p != NULL)
		DisposeDialog(p);
	SetPort(save);                  /* not the dialog just disposed of */
	InitCursor();

	if (err == netCanceled)
		return;                     /* the writer said so: nothing to tell */
	if (err != noErr) {
		Problem(err, bridge, line);
	} else if (done < 0) {
		sprintf(line, "What answered at %s isn\xD5t Topica\xD5s bridge.", bridge);
	} else if (status != 200) {
		char said[256];
		strcpy(said, line);
		sprintf(line, "Not published: %s", said);
	}
	ShowNote(line);
}

static void ShowKey(DialogPtr dlg)
{
	char dots[sizeof(gKey)];
	short n = (short) strlen(gKey);

	memset(dots, 0xA5, (size_t) n);         /* a bullet, in Mac Roman */
	dots[n] = '\0';
	SetItemText(dlg, kKeyField, dots);
	SelectDialogItemText(dlg, kKeyField, n, n);
}

/* Typing in the Key field changes gKey and shows bullets; the rest is StandardFilter's */
static pascal Boolean PublishFilter(DialogPtr d, EventRecord *e, short *item)
{
	if ((e->what == keyDown || e->what == autoKey) && ((DialogPeek) d)->editField == kKeyField - 1) {
		char c = (char) (e->message & charCodeMask);
		TEHandle te = ((DialogPeek) d)->textH;
		size_t n = strlen(gKey);

		if (e->modifiers & cmdKey) {
			if (c != '.') {                 /* no copying, cutting or pasting a hidden key */
				SysBeep(10);
				e->what = nullEvent;
				return false;
			}
		} else if (c != '\r' && c != 0x03 && c != 0x1B && c != '\t') {
			if (n > 0 && (**te).selStart == 0 && (**te).selEnd >= (short) n) {
				gKey[0] = '\0';             /* all of it selected: typing replaces it */
				if (c == 0x08)
					c = 0;
			}
			n = strlen(gKey);
			if (c == 0x08 && n > 0)
				gKey[n - 1] = '\0';
			else if (c >= 0x20 && c < 0x7F && n < sizeof(gKey) - 1)
				gKey[n] = c, gKey[n + 1] = '\0';
			else if (c != 0)
				SysBeep(10);
			ShowKey(d);
			e->what = nullEvent;
			return false;
		}
	}
	return StandardFilter(d, e, item);
}

void PublishPost(Doc *d)
{
	DialogPtr dlg;
	char path[PUB_PATH], bridge[64], key[64], about[400], title[256];
	unsigned long ip = 0;
	unsigned short port = 0;
	short item, draft = 0, years;
	Boolean hugo = gPrefs.profile == kProfileHugo;

	dlg = GetModalDialog(kPublishDialog);
	if (dlg == NULL) {
		SysBeep(10);
		return;
	}
	if (IsSystem7())
		HideDialogItem(dlg, kHeading);
	Value(d, "title", title, 120);
	if (title[0] == '\0') {
		Str255 w;
		GetWTitle(DocWindow(d), w);
		CStr(title, w);
		title[120] = '\0';
	}
	if (strlen(title) > 40)
		strcpy(title + 39, "\xC9");
	sprintf(about, "Sends \xD2%s\xD3 to the blog\xD5s drafts branch.", title);
	SetItemText(dlg, kAbout, about);
	SuggestPath(d, false, path, sizeof(path));
	SetItemText(dlg, kPathField, path);
	SetItemText(dlg, kBridgeField, gPrefs.bridge);
	strcpy(gKey, gPrefs.bridgeKey);
	ShowKey(dlg);
	if (hugo)
		HiliteControl(ItemControl(dlg, kDraftBox), 255);    /* a Hugo draft says so in its front matter */
	OutlineDefault(dlg, kOutline);
	ShowWindow(dlg);
	SelectDialogItemText(dlg, gPrefs.bridge[0] == '\0' ? kBridgeField : kPathField, 0, 32767);
	InitCursor();
	for (;;) {
		ModalDialog((ModalFilterUPP) PublishFilter, &item);
		if (item == kCancel)
			break;
		if (item == kDraftBox) {
			draft = !draft;
			SetControlValue(ItemControl(dlg, kDraftBox), draft);
			SuggestPath(d, draft, path, sizeof(path));
			SetItemText(dlg, kPathField, path);
			SelectDialogItemText(dlg, kPathField, 0, 32767);
			continue;
		}
		if (item != kPublishButton)
			continue;
		GetItemText(dlg, kPathField, path, sizeof(path));
		GetItemText(dlg, kBridgeField, bridge, sizeof(bridge));
		strcpy(key, gKey);
		if (!pub_address(bridge, &ip, &port)) {
			ShowNote("The bridge\xD5s address is its IP address and port, like 192.0.2.10:8023.");
			SelectDialogItemText(dlg, kBridgeField, 0, 32767);
		} else if (strlen(key) < 16) {
			ShowNote("The key is the one the bridge was set up with: at least 16 characters.");
			SelectDialogItemText(dlg, kKeyField, 0, 32767);
		} else if (!pub_path_ok(path)) {
			ShowNote(hugo
				? "A post goes in content/posts, like content/posts/title.md."
				: "A post goes in _posts, named with its date: _posts/2026-10-23-title.md. A draft goes in "
				"_drafts.");
			SelectDialogItemText(dlg, kPathField, 0, 32767);
		} else {
			break;
		}
	}
	DisposeDialog(dlg);
	SetPort(DocWindow(d));          /* not the dialog just disposed of */
	if (item == kCancel)
		return;
	/* the next post goes where this one did, in a folder for its year or not */
	years = strncmp(path, "_posts/", 7) == 0 ? pub_path_years(path) : gPrefs.yearFolders;
	if (strcmp(bridge, gPrefs.bridge) != 0 || strcmp(key, gPrefs.bridgeKey) != 0 || years != gPrefs.yearFolders) {
		strcpy(gPrefs.bridge, bridge);
		strcpy(gPrefs.bridgeKey, key);
		gPrefs.yearFolders = years;
		SettingsSave();
	}
	Send(d, ip, port, bridge, path);
}
