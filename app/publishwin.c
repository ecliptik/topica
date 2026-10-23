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
	pub_path(gPrefs.profile == kProfileHugo, draft, name, title, date, out, cap);
}

/* What went wrong with the connection, in words that say what to do */
static void Problem(OSErr err, const char *bridge, char *out)
{
	switch (err) {
	case netNoNetwork:
#ifdef TOPICA_TALKIP
		strcpy(out, "Topica can\xD5t reach the network: AppleTalk is off, or there\xD5s no MacIP gateway on it.");
#else
		strcpy(out, "Topica can\xD5t reach the network: MacTCP isn\xD5t installed or isn\xD5t set up.");
#endif
		break;
	case memFullErr:
		strcpy(out, "There isn\xD5t enough memory to publish the post. Close a window and try again.");
		break;
	case openFailed:
	case commandTimeout:
	case connectionTerminated:
		sprintf(out, "Topica couldn\xD5t reach the bridge at %s. Check its address, and that it\xD5s running.", bridge);
		break;
	default:
		sprintf(out, "The connection to the bridge at %s failed (error %d).", bridge, err);
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

/* Sends the post and shows what the bridge said */
static void Send(Doc *d, unsigned long ip, unsigned short port, const char *bridge, const char *path)
{
	Editor *ed = DocEditor(d);
	const char *t;
	char host[20], line[300], reply[kReplyCap];
	long n, bodyLen, headLen, have = 0, got;
	Handle h;
	DialogPtr p;
	OSErr err;
	int status = 0, done = 0;

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

	p = GetNewDialog(kProgressDialog, NULL, (WindowPtr) -1);
	if (p != NULL) {
		CenterWindow(p);
		ShowWindow(p);
		SetPort(p);
	}
	SetCursor(*GetCursor(watchCursor));
	sprintf(line, "Connecting to the bridge at %s\xC9", bridge);
	Progress(p, line);
	err = NetOpen(ip, port, kTimeout);
	if (err == noErr) {
		sprintf(line, "Sending \xD2%s\xD3 (%ldK)\xC9", path, (bodyLen + 1023) / 1024);
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
	DisposeHandle(h);
	if (p != NULL)
		DisposeDialog(p);
	InitCursor();

	if (err != noErr) {
		Problem(err, bridge, line);
	} else if (done < 0) {
		sprintf(line, "Something at %s answered, but not as Topica\xD5s bridge would.", bridge);
	} else if (status != 200) {
		char said[256];
		strcpy(said, line);
		sprintf(line, "The bridge didn\xD5t publish the post: %s", said);
	}
	ShowNote(line);
}

void PublishPost(Doc *d)
{
	DialogPtr dlg;
	char path[PUB_PATH], bridge[64], key[64], about[400], title[256];
	unsigned long ip = 0;
	unsigned short port = 0;
	short item, draft = 0;
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
	sprintf(about, "Topica sends \xD2%s\xD3 to the bridge, which puts it on the blog\xD5s drafts branch.", title);
	SetItemText(dlg, kAbout, about);
	SuggestPath(d, false, path, sizeof(path));
	SetItemText(dlg, kPathField, path);
	SetItemText(dlg, kBridgeField, gPrefs.bridge);
	SetItemText(dlg, kKeyField, gPrefs.bridgeKey);
	if (hugo)
		HiliteControl(ItemControl(dlg, kDraftBox), 255);    /* a Hugo draft says so in its front matter */
	OutlineDefault(dlg, kOutline);
	ShowWindow(dlg);
	SelectDialogItemText(dlg, gPrefs.bridge[0] == '\0' ? kBridgeField : kPathField, 0, 32767);
	InitCursor();
	for (;;) {
		ModalDialog((ModalFilterUPP) StandardFilter, &item);
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
		GetItemText(dlg, kKeyField, key, sizeof(key));
		if (!pub_address(bridge, &ip, &port)) {
			ShowNote("The bridge\xD5s address is its IP address and port, like 192.0.2.10:8023.");
			SelectDialogItemText(dlg, kBridgeField, 0, 32767);
		} else if (strlen(key) < 16) {
			ShowNote("The key is the one the bridge was set up with: at least 16 characters.");
			SelectDialogItemText(dlg, kKeyField, 0, 32767);
		} else if (!pub_path_ok(path)) {
			ShowNote(hugo
				? "A post goes in content/posts, named like content/posts/title.md."
				: "A post goes in _posts, named with its date: _posts/2026-10-23-title.md. A draft goes in _drafts.");
			SelectDialogItemText(dlg, kPathField, 0, 32767);
		} else {
			break;
		}
	}
	DisposeDialog(dlg);
	if (item == kCancel)
		return;
	if (strcmp(bridge, gPrefs.bridge) != 0 || strcmp(key, gPrefs.bridgeKey) != 0) {
		strcpy(gPrefs.bridge, bridge);
		strcpy(gPrefs.bridgeKey, key);
		SettingsSave();
	}
	Send(d, ip, port, bridge, path);
}
