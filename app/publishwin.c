/* publishwin.c: see publishwin.h. */
#include <Quickdraw.h>
#include <Dialogs.h>
#include <Multiverse.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

#include "../core/fmt.h"
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
		tp_sprintf(out, "The bridge at %s refused the connection. Is it running?", bridge);
		break;
	case netNoName: {
		/* the name alone, without http:// or the port */
		char name[64];
		int n = 0;
		if (strncmp(bridge, "http://", 7) == 0)
			bridge += 7;
		while (bridge[n] && bridge[n] != ':' && bridge[n] != '/' && n < (int) sizeof(name) - 1) {
			name[n] = bridge[n];
			n++;
		}
		name[n] = '\0';
		tp_sprintf(out, "There\xD5s no %s. Is the name right?", name);
		break;
	}
	case netNoNameServer:
#ifdef TOPICA_TALKIP
		strcpy(out, "The MacIP gateway gave no name server: give the bridge\xD5s IP address.");
#else
		strcpy(out, "There\xD5s no name server: give the bridge\xD5s IP address, or set one in MacTCP.");
#endif
		break;
	case netNoAnswer:
		strcpy(out, "The name server didn\xD5t answer. Try the bridge\xD5s IP address.");
		break;
	case commandTimeout:
	case connectionTerminated:
		tp_sprintf(out, "Nothing answered at %s. Is the bridge running, and its port open?", bridge);
		break;
	default:
		tp_sprintf(out, "The connection to %s failed (error %d).", bridge, err);
		break;
	}
}

/* ---- the publishing, in the background ----
   One post at a time. The request (the post as it was when Publish was
   chosen) is made at once; then PublishIdle takes it a step further each
   time round the event loop, and the writer goes on writing. */

enum { kLookup = 1, kConnect, kSend, kWait };

typedef struct {
	int step;
	long doc;                       /* the post's DocId, for its status line */
	Handle request;
	long len;
	unsigned long ip;
	unsigned short port;
	char name[64], bridge[64], title[48];
	char reply[kReplyCap];
	long have;
} Job;

static Job *gJob;

static const char *const kSteps[] = { "", "Looking up the bridge\xC9", "Connecting\xC9", "Sending\xC9",
	"Waiting for the bridge\xC9" };

static void ShowStep(int step)
{
	Doc *d;

	gJob->step = step;
	if ((d = DocFromId(gJob->doc)) != NULL)
		DocShowStatus(d);
}

/* A note now, or (Topica in the background) a notification, and the note
   when the writer comes back */
static NMRec gNotice;
static char gPending[300];
static Str255 gNoticeText;

static void Tell(const char *line)
{
	if (AppInBackground() && HasNotifications()) {
		strcpy(gPending, line);
		memset(&gNotice, 0, sizeof(gNotice));
		gNotice.qType = 8;              /* nmType */
		gNotice.nmMark = 1;             /* Topica's mark in the Application menu */
		gNotice.nmSound = (Handle) -1;  /* the system's beep */
		PStr(gNoticeText, "Topica has finished publishing.");
		gNotice.nmStr = gNoticeText;
		if (NMInstall(&gNotice) == noErr)
			return;
		gPending[0] = '\0';
	}
	ShowNote(line);
}

/* Done: what happened, in a note (through the Notification Manager when
   Topica is in the background) */
static void Finish(OSErr err, int done, int status, const char *said)
{
	char line[300];
	Doc *d;

	NetClose();
	if (gJob->request != NULL)
		DisposeHandle(gJob->request);
	if (err == netCanceled) {
		line[0] = '\0';                /* the writer said so: nothing to tell */
	} else if (err != noErr) {
		Problem(err, gJob->bridge, line);
	} else if (done < 0) {
		tp_sprintf(line, "What answered at %s isn\xD5t Topica\xD5s bridge.", gJob->bridge);
	} else if (status != 200) {
		tp_sprintf(line, "Not published: %s", said);
	} else {
		strcpy(line, said);
	}
	gJob->step = 0;
	if ((d = DocFromId(gJob->doc)) != NULL)
		DocShowStatus(d);
	DisposePtr((Ptr) gJob);
	gJob = NULL;
	if (line[0] != '\0')
		Tell(line);
}

Boolean PublishBusy(void)
{
	return gJob != NULL;
}

Boolean PublishMayQuit(void)
{
	Str255 title;

	if (gJob == NULL)
		return true;
	PStr(title, gJob->title);
	ParamText(title, "\p", "\p", "\p");
	InitCursor();
	NoDialogHelp();
	if (CautionAlert(kPublishingAlert, (ModalFilterUPP) StandardFilter) != 1)
		return false;
	PublishStop();
	return true;
}

const char *PublishStatus(long doc)
{
	return gJob != NULL && gJob->doc == doc ? kSteps[gJob->step] : NULL;
}

void PublishStop(void)
{
	if (gJob == NULL)
		return;
	NetCancel();
	Finish(netCanceled, 0, 0, "");
}

void PublishIdle(void)
{
	OSErr err;
	int status = 0, done;
	char said[256];

	if (gPending[0] != '\0' && !AppInBackground()) {
		(void) NMRemove(&gNotice);
		ShowNote(gPending);
		gPending[0] = '\0';
	}
	if (gJob == NULL)
		return;
	err = NetPoll();
	if (err == netBusy)
		return;
	switch (gJob->step) {
	case kLookup:
		if (err == noErr) {
			gJob->ip = NetAddress();
			ShowStep(kConnect);
			err = NetOpenStart(gJob->ip, gJob->port, kTimeout);
		}
		break;
	case kConnect:
		if (err == noErr) {
			ShowStep(kSend);
			HLock(gJob->request);
			err = NetSendStart(*gJob->request, gJob->len);
		}
		break;
	case kSend:
		if (err == noErr) {
			ShowStep(kWait);
			err = NetRecvStart(gJob->reply, kReplyCap, 60);
		}
		break;
	case kWait:
		gJob->have += NetGot();
		if (((err == connectionClosing || err == connectionTerminated) && gJob->have > 0)
				|| gJob->have == kReplyCap) {
			done = pub_reply(gJob->reply, gJob->have, 1, &status, said, sizeof(said));
			Finish(noErr, done == 0 ? -1 : done, status, said);
			return;
		}
		if (err == noErr) {
			done = pub_reply(gJob->reply, gJob->have, 0, &status, said, sizeof(said));
			if (done) {
				Finish(noErr, done, status, said);
				return;
			}
			err = NetRecvStart(gJob->reply + gJob->have, kReplyCap - gJob->have, 60);
		}
		break;
	}
	if (err != noErr)
		Finish(err, 0, 0, "");
}

/* Starts publishing d to the bridge at ip, or at name (looked up first) when ip is 0 */
static void Send(Doc *d, unsigned long ip, const char *name, unsigned short port, const char *bridge,
		const char *path, const char *title)
{
	Editor *ed = DocEditor(d);
	const char *t;
	char host[64], line[300];
	long n, bodyLen, headLen;
	OSErr err;

	gJob = (Job *) NewPtrClear(sizeof(Job));
	if (gJob == NULL) {
		Problem(memFullErr, bridge, line);
		ShowNote(line);
		return;
	}
	if (ip == 0)
		strcpy(host, name);
	else
		tp_sprintf(host, "%lu.%lu.%lu.%lu", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
	/* the request: its head, then the post as the web wants it (UTF-8, LF) */
	t = EdLockText(ed);
	n = EdLength(ed);
	bodyLen = tc_to_disk(t, n, NULL, 0, 1, 1);
	gJob->request = bodyLen >= 0 ? NewHandle(kHeadCap + bodyLen) : NULL;
	if (gJob->request == NULL) {
		EdUnlockText(ed);
		DisposePtr((Ptr) gJob);
		gJob = NULL;
		Problem(memFullErr, bridge, line);
		ShowNote(line);
		return;
	}
	HLock(gJob->request);
	headLen = pub_request(*gJob->request, kHeadCap, host, port, gPrefs.bridgeKey, path, bodyLen);
	(void) tc_to_disk(t, n, *gJob->request + headLen, bodyLen, 1, 1);
	EdUnlockText(ed);
	HUnlock(gJob->request);
	gJob->len = headLen + bodyLen;
	gJob->doc = DocId(d);
	gJob->ip = ip;
	gJob->port = port;
	strcpy(gJob->name, name);
	strcpy(gJob->bridge, bridge);
	strncpy(gJob->title, title, sizeof(gJob->title) - 1);
	if (ip == 0) {
		ShowStep(kLookup);
		err = NetResolveStart(name);
	} else {
		ShowStep(kConnect);
		err = NetOpenStart(ip, port, kTimeout);
	}
	if (err != noErr)
		Finish(err, 0, 0, "");
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
	char path[PUB_PATH], bridge[64], name[64], key[64], about[400], title[256];
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
	tp_sprintf(about, "Sends \xD2%s\xD3 to the blog\xD5s drafts branch.", title);
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
		if (!pub_bridge(bridge, name, sizeof(name), &ip, &port)) {
			ShowNote("The bridge is its address or name, and port, like 192.0.2.10:8023.");
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
	Send(d, ip, name, port, bridge, path, title);
}
