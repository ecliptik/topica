/* postinfo.c: see postinfo.h. */
#include <Quickdraw.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Resources.h>
#include <ToolUtils.h>
#include <string.h>

#include "../core/frontmatter.h"
#include "../core/post.h"
#include "../core/undo.h"
#include "postinfo.h"
#include "settings.h"
#include "util.h"

/* Post Info's items (Insert Image's heading is its item 8) */
enum { kOutline = 3, kFirstField = 4, kFieldCount = 6, kInfoHeading = 16, kImageHeading = 8 };
/* Insert Link's */
enum { kLinkTextField = 4, kLinkAddressField = 5, kLinkHeading = 8 };

enum { kFieldLen = 256 };

/* The fields, in the dialog's order, and their labels (items 10-15) */
static const char *const kKeys[kFieldCount] = { "title", "description", "category", "tags", "layout", "date" };
static const char *const kHugoKeys[kFieldCount] = { "title", "description", "categories", "tags", "draft", "date" };
enum { kFirstLabel = 10 };

/* A key's value from the post's text */
static int Get(Doc *d, const char *key, char *out)
{
	Editor *ed = DocEditor(d);
	const char *t = EdLockText(ed);
	int r = fm_get(t, EdLength(ed), key, out, kFieldLen);

	EdUnlockText(ed);
	return r;
}

static short RunDialog(DialogPtr dlg, short firstField, short heading)
{
	short item;

	if (IsSystem7())
		HideDialogItem(dlg, heading);   /* the movable dialog's title bar says it */
	OutlineDefault(dlg, kOutline);
	ShowWindow(dlg);
	SelectDialogItemText(dlg, firstField, 0, 32767);
	InitCursor();
	do {
		ModalDialog((ModalFilterUPP) StandardFilter, &item);
	} while (item != kOK && item != kCancel);
	return item;
}

Boolean PostInfo(Doc *d)
{
	char (*values)[kFieldLen];
	const char *keys[kFieldCount];
	FmField fields[kFieldCount];
	DialogPtr dlg;
	Editor *ed = DocEditor(d);
	const char *t;
	long start, end, n, len, selStart, selEnd, delta;
	Ptr block;
	short i, count = 0;
	Boolean changed;

	values = (char (*)[kFieldLen]) NewPtrClear(kFieldCount * kFieldLen);
	if (values == NULL) {
		SysBeep(10);
		return false;
	}
	memcpy(keys, gPrefs.profile == kProfileHugo ? kHugoKeys : kKeys, sizeof(keys));
	/* a Jekyll post with "categories" and no "category" edits that */
	if (gPrefs.profile != kProfileHugo && Get(d, "category", values[2]) == kFmAbsent
			&& Get(d, "categories", values[2]) != kFmAbsent)
		keys[2] = "categories";
	dlg = GetModalDialog(kPostInfoDialog);
	if (dlg == NULL) {
		DisposePtr((Ptr) values);
		SysBeep(10);
		return false;
	}
	if (gPrefs.profile == kProfileHugo) {
		SetItemText(dlg, kFirstLabel + 2, "Categories:");
		SetItemText(dlg, kFirstLabel + 4, "Draft:");
	}
	for (i = 0; i < kFieldCount; i++) {
		if (Get(d, keys[i], values[i]) != kFmFound)
			values[i][0] = '\0';
		SetItemText(dlg, (short) (kFirstField + i), values[i]);
	}
	if (RunDialog(dlg, kFirstField, kInfoHeading) != kOK) {
		DisposeDialog(dlg);
		DisposePtr((Ptr) values);
		return false;
	}
	for (i = 0; i < kFieldCount; i++) {
		GetItemText(dlg, (short) (kFirstField + i), values[i], kFieldLen);
		fields[count].key = keys[i];
		fields[count].value = values[i];
		count++;
	}
	DisposeDialog(dlg);

	t = EdLockText(ed);
	len = EdLength(ed);
	if (!fm_find(t, len, &start, &end))
		start = end = 0;
	n = fm_update(t, len, fields, count, '\r', NULL, 0);
	block = NewPtr(n > 0 ? n : 1);
	if (block == NULL) {
		EdUnlockText(ed);
		DisposePtr((Ptr) values);
		SysBeep(10);
		return false;
	}
	(void) fm_update(t, len, fields, count, '\r', block, n);
	changed = n != end - start || memcmp(block, t + start, (size_t) n) != 0;
	EdUnlockText(ed);
	DisposePtr((Ptr) values);
	if (changed) {
		/* the caret stays where it was in the text */
		EdGetSelection(ed, &selStart, &selEnd);
		delta = n - (end - start);
		if (DocReplace(d, kUndoPostInfo, start, end, block, n)) {
			if (selStart >= end) {
				EdSetSelection(ed, selStart + delta, selEnd + delta);
				EdShowSelection(ed);
			}
		}
		DocSealUndo(d);
	}
	DisposePtr(block);
	return true;
}

/* Insert Image's items */
enum { kAltField = 4, kFileField = 5 };

void InsertImage(Doc *d)
{
	Editor *ed = DocEditor(d);
	char alt[kFieldLen], file[kFieldLen], category[kFieldLen], prefix[kFieldLen];
	char *md;
	Str255 where;
	DialogPtr dlg;
	long s, e, n;
	const char *t;
	char *sp;

	/* the folder: the blog's image path and the post's category */
	strncpy(prefix, gPrefs.imagePath, sizeof(prefix) - 1);
	prefix[sizeof(prefix) - 1] = '\0';
	if (Get(d, "category", category) != kFmFound && Get(d, "categories", category) != kFmFound)
		category[0] = '\0';
	if ((sp = strchr(category, ' ')) != NULL)
		*sp = '\0';
	if (category[0] != '\0' && strlen(prefix) + strlen(category) + 1 < sizeof(prefix)) {
		strcat(prefix, category);
		strcat(prefix, "/");
	}
	/* the selected text, as the description */
	alt[0] = '\0';
	EdGetSelection(ed, &s, &e);
	if (e > s && e - s < 200) {
		t = EdLockText(ed);
		if (memchr(t + s, '\r', (size_t) (e - s)) == NULL) {
			memcpy(alt, t + s, (size_t) (e - s));
			alt[e - s] = '\0';
		}
		EdUnlockText(ed);
	}
	PStr(where, prefix);
	ParamText(where, "\p", "\p", "\p");
	dlg = GetModalDialog(kImageDialog);
	if (dlg == NULL) {
		SysBeep(10);
		return;
	}
	SetItemText(dlg, kAltField, alt);
	if (RunDialog(dlg, kAltField, kImageHeading) != kOK) {
		DisposeDialog(dlg);
		return;
	}
	GetItemText(dlg, kAltField, alt, sizeof(alt));
	GetItemText(dlg, kFileField, file, sizeof(file));
	DisposeDialog(dlg);
	if (file[0] == '\0') {              /* a placeholder name from the description */
		post_slug(alt[0] ? alt : "image", file, 40);
		strcat(file, ".jpg");
	}
	md = (char *) NewPtr(4 * kFieldLen);
	if (md == NULL) {
		SysBeep(10);
		return;
	}
	strcpy(md, "![");
	strcat(md, alt);
	strcat(md, "](");
	strcat(md, prefix);
	strcat(md, file);
	strcat(md, ")");
	n = (long) strlen(md);
	(void) DocReplace(d, kUndoInsert, s, e, md, n);
	DocSealUndo(d);
	DisposePtr((Ptr) md);
}

static Boolean IsAddress(const char *s)
{
	return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0 || strncmp(s, "mailto:", 7) == 0
		|| s[0] == '/';
}

void InsertLink(Doc *d)
{
	Editor *ed = DocEditor(d);
	char text[kFieldLen], address[kFieldLen];
	char *md;
	DialogPtr dlg;
	long s, e, n;
	const char *t;

	/* the selection: the link's text, or its address if it looks like one */
	text[0] = address[0] = '\0';
	EdGetSelection(ed, &s, &e);
	if (e > s && e - s < kFieldLen) {
		t = EdLockText(ed);
		if (memchr(t + s, '\r', (size_t) (e - s)) == NULL) {
			memcpy(text, t + s, (size_t) (e - s));
			text[e - s] = '\0';
		}
		EdUnlockText(ed);
	}
	if (IsAddress(text)) {
		strcpy(address, text);
		text[0] = '\0';
	}
	dlg = GetModalDialog(kLinkDialog);
	if (dlg == NULL) {
		SysBeep(10);
		return;
	}
	SetItemText(dlg, kLinkTextField, text);
	SetItemText(dlg, kLinkAddressField, address);
	if (RunDialog(dlg, text[0] != '\0' ? kLinkAddressField : kLinkTextField, kLinkHeading) != kOK) {
		DisposeDialog(dlg);
		return;
	}
	GetItemText(dlg, kLinkTextField, text, sizeof(text));
	GetItemText(dlg, kLinkAddressField, address, sizeof(address));
	DisposeDialog(dlg);
	if (text[0] == '\0' && address[0] == '\0')
		return;
	md = (char *) NewPtr(2 * kFieldLen + 8);
	if (md == NULL) {
		SysBeep(10);
		return;
	}
	if (text[0] == '\0') {             /* just the address: an automatic link */
		strcpy(md, "<");
		strcat(md, address);
		strcat(md, ">");
	} else {
		strcpy(md, "[");
		strcat(md, text);
		strcat(md, "](");
		strcat(md, address);
		strcat(md, ")");
	}
	n = (long) strlen(md);
	if (DocReplace(d, kUndoInsert, s, e, md, n) && address[0] == '\0')
		EdSetSelection(ed, s + n - 1, s + n - 1);      /* in the ( ), to type the address */
	DocSealUndo(d);
	DisposePtr((Ptr) md);
}
