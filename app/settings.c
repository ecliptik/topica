/* settings.c: see settings.h. */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Dialogs.h>
#include <Memory.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fileio.h"
#include "settings.h"
#include "util.h"

TopicaPrefs gPrefs;

#define kPrefsName "Topica Preferences"

/* The dialog's items */
enum {
	kHeading = 4,
	kMonacoRadio = 5, kGenevaRadio, kCourierRadio,
	kSize9 = 8, kSize10, kSize12,
	kJekyllRadio = 11, kHugoRadio,
	kWebRadio = 13, kMacRadio,
	kImageField = 15, kLayoutField, kAutosaveField,
	kViBox = 27, kJkBox
};

static const char *const kFonts[] = { "Monaco", "Geneva", "Courier" };
static const short kSizes[] = { 9, 10, 12 };

void SettingsLoad(void)
{
	FileRef f;
	Ptr data;
	long len;
	SysEnvRec env;

	prefs_default(&gPrefs);
	/* styled TextEdit takes seconds to lay out a long post on a 68000 */
	if (SysEnvirons(1, &env) != noErr || env.processor < env68020)
		gPrefs.format = 0;
	if (FilePrefsFolder(kPrefsName, &f) != noErr || FileRead(&f, &data, &len) != noErr)
		return;
	prefs_read(&gPrefs, data, len);
	DisposePtr(data);
}

void SettingsSave(void)
{
	FileRef f;
	char buf[1024];
	long n = prefs_write(&gPrefs, '\r', buf, sizeof(buf));
	OSErr err;

	if (n < 0 || FilePrefsFolder(kPrefsName, &f) != noErr)
		return;
	err = FileWrite(&f, buf, n, 'TEXT', kCreator);
	if (err != noErr)
		ErrorAlert("Your preferences couldn\xD5t be saved.", err);
}

short SettingsFontNumber(void)
{
	Str255 name;
	short num = 0;

	PStr(name, gPrefs.font);
	GetFNum(name, &num);
	if (num == 0 && strcmp(gPrefs.font, "Chicago") != 0)
		num = kMonaco;
	return num;
}

static void SetControl(DialogPtr d, short item, short value)
{
	short type;
	Handle h;
	Rect r;

	GetDialogItem(d, item, &type, &h, &r);
	SetControlValue((ControlHandle) h, value);
}

static void Radio(DialogPtr d, short first, short count, short on)
{
	short i;

	for (i = 0; i < count; i++)
		SetControl(d, (short) (first + i), (short) (first + i == on));
}

Boolean SettingsDialog(void)
{
	DialogPtr d = GetModalDialog(kPrefsDialog);
	TopicaPrefs p = gPrefs;
	short item, i, font = kMonacoRadio, size = kSize9;
	char num[16];

	if (d == NULL) {
		SysBeep(10);
		return false;
	}
	if (IsSystem7())
		HideDialogItem(d, kHeading);
	OutlineDefault(d, 3);
	for (i = 0; i < 3; i++) {
		if (strcmp(p.font, kFonts[i]) == 0)
			font = (short) (kMonacoRadio + i);
		if (p.size == kSizes[i])
			size = (short) (kSize9 + i);
	}
	Radio(d, kMonacoRadio, 3, font);
	Radio(d, kSize9, 3, size);
	Radio(d, kJekyllRadio, 2, p.profile == kProfileHugo ? kHugoRadio : kJekyllRadio);
	Radio(d, kWebRadio, 2, p.utf8 ? kWebRadio : kMacRadio);
	SetItemText(d, kImageField, p.imagePath);
	SetItemText(d, kLayoutField, p.layout);
	sprintf(num, "%d", p.autosaveMinutes);
	SetItemText(d, kAutosaveField, num);
	SetControl(d, kViBox, p.vi);
	SetControl(d, kJkBox, p.viJk);
	ShowWindow(d);
	SelectDialogItemText(d, kImageField, 0, 32767);
	InitCursor();
	for (;;) {
		ModalDialog((ModalFilterUPP) StandardFilter, &item);
		if (item == kOK || item == kCancel)
			break;
		if (item >= kMonacoRadio && item <= kCourierRadio) {
			font = item;
			Radio(d, kMonacoRadio, 3, font);
		} else if (item >= kSize9 && item <= kSize12) {
			size = item;
			Radio(d, kSize9, 3, size);
		} else if (item == kJekyllRadio || item == kHugoRadio) {
			p.profile = item == kHugoRadio ? kProfileHugo : kProfileJekyll;
			Radio(d, kJekyllRadio, 2, item);
		} else if (item == kWebRadio || item == kMacRadio) {
			p.utf8 = p.lf = item == kWebRadio;
			Radio(d, kWebRadio, 2, item);
		} else if (item == kViBox) {
			p.vi = !p.vi;
			SetControl(d, kViBox, p.vi);
		} else if (item == kJkBox) {
			p.viJk = !p.viJk;
			SetControl(d, kJkBox, p.viJk);
		}
	}
	if (item == kOK) {
		strcpy(p.font, kFonts[font - kMonacoRadio]);
		p.size = kSizes[size - kSize9];
		GetItemText(d, kImageField, p.imagePath, (short) sizeof(p.imagePath));
		GetItemText(d, kLayoutField, p.layout, (short) sizeof(p.layout));
		GetItemText(d, kAutosaveField, num, (short) sizeof(num));
		i = (short) atoi(num);
		p.autosaveMinutes = (short) (i < 0 ? 0 : i > 60 ? 60 : i);
	}
	DisposeDialog(d);
	if (item != kOK || memcmp(&p, &gPrefs, sizeof(p)) == 0)
		return false;
	gPrefs = p;
	SettingsSave();
	return true;
}
