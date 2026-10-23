/* theme.c: see theme.h. */
#include <Quickdraw.h>
#include <Windows.h>
#include <Memory.h>
#include <string.h>

#include "settings.h"
#include "theme.h"
#include "util.h"

/* A theme: its background, text, marks, code, links, headings and front
   matter as 0xRRGGBB, from each one's published palette. The preview's
   boxes and lines are mixed from the background and the text. */
typedef struct {
	const char *name;
	unsigned char color;        /* needs Color QuickDraw */
	unsigned char dark;
	unsigned long rgb[7];
} Theme;

static const Theme kThemes[] = {
	{ "Light", 0, 0, { 0xFFFFFF, 0x000000, 0x606060, 0x006000, 0x0000C0, 0x000080, 0x703800 } },
	{ "Dark", 0, 1, { 0x000000, 0xFFFFFF, 0x999999, 0x66CC66, 0x6699FF, 0x99CCFF, 0xFFCC66 } },
#ifndef TOPICA_LITE
	{ "Solarized Light", 1, 0, { 0xFDF6E3, 0x657B83, 0x93A1A1, 0x859900, 0x6C71C4, 0x268BD2, 0xCB4B16 } },
	{ "Solarized Dark", 1, 1, { 0x002B36, 0x839496, 0x586E75, 0x859900, 0x6C71C4, 0x268BD2, 0xCB4B16 } },
	{ "TokyoNight Day", 1, 0, { 0xE1E2E7, 0x3760BF, 0x848CB5, 0x587539, 0x2E7DE9, 0x9854F1, 0x8C6C3E } },
	{ "TokyoNight", 1, 1, { 0x1A1B26, 0xC0CAF5, 0x565F89, 0x9ECE6A, 0x7AA2F7, 0xBB9AF7, 0xE0AF68 } },
	{ "Amber CRT", 1, 1, { 0x000000, 0xFFB000, 0x996600, 0xFFCC66, 0xFF9900, 0xFFD966, 0xCC7A00 } },
	{ "System 7", 1, 0, { 0xFFFFFF, 0x000000, 0x808080, 0x006411, 0x0000D4, 0x4600A5, 0x90713A } },
	{ "Dracula", 1, 1, { 0x282A36, 0xF8F8F2, 0x6272A4, 0x50FA7B, 0x8BE9FD, 0xBD93F9, 0xFFB86C } },
	{ "Nord", 1, 1, { 0x2E3440, 0xD8DEE9, 0x616E88, 0xA3BE8C, 0x88C0D0, 0x81A1C1, 0xEBCB8B } },
	{ "Green Screen", 1, 1, { 0x000000, 0x33FF33, 0x008800, 0x99FF99, 0x66FF66, 0xCCFFCC, 0x00CC00 } },
	{ "Classic", 1, 0, { 0xC0C0C0, 0x000000, 0x555555, 0x006600, 0x0000EE, 0x000080, 0x551A8B } },
	{ "Monokai", 1, 1, { 0x272822, 0xF8F8F2, 0x75715E, 0xA6E22E, 0x66D9EF, 0xF92672, 0xE6DB74 } },
	{ "Gruvbox", 1, 1, { 0x282828, 0xEBDBB2, 0x928374, 0xB8BB26, 0x83A598, 0xFABD2F, 0xFE8019 } },
#endif
};

#define kThemeCount ((short) (sizeof(kThemes) / sizeof(kThemes[0])))

static short gTheme;

void ThemeInit(void)
{
	short i;

	gTheme = 0;
	for (i = 0; i < kThemeCount; i++)
		if (strcmp(kThemes[i].name, gPrefs.theme) == 0 && ThemeUsable(i))
			gTheme = i;
}

short ThemeCount(void)
{
	return kThemeCount;
}

const char *ThemeName(short i)
{
	return kThemes[i].name;
}

Boolean ThemeUsable(short i)
{
	return !kThemes[i].color || HasColorQD();
}

short ThemeCurrent(void)
{
	return gTheme;
}

void ThemeChoose(short i)
{
	if (i < 0 || i >= kThemeCount || !ThemeUsable(i))
		return;
	gTheme = i;
	strcpy(gPrefs.theme, kThemes[i].name);
	SettingsSave();
}

Boolean ThemeDark(void)
{
	return kThemes[gTheme].dark;
}

static unsigned short Channel(unsigned long rgb, short shift)
{
	return (unsigned short) (((rgb >> shift) & 0xFF) * 257);
}

/* a, toward b by n/16 */
static unsigned short Mix(unsigned short a, unsigned short b, long n)
{
	return (unsigned short) (a + ((long) b - (long) a) * n / 16);
}

void ThemeColor(short role, RGBColor *c)
{
	const Theme *t = &kThemes[gTheme];
	unsigned long rgb;

	if (!HasColorQD()) {
		c->red = c->green = c->blue = 0;
		return;
	}
	if (role == kThShade || role == kThLine) {
		RGBColor bg, fg;
		long n = role == kThShade ? 1 : 6;
		ThemeColor(kThBg, &bg);
		ThemeColor(kThText, &fg);
		c->red = Mix(bg.red, fg.red, n);
		c->green = Mix(bg.green, fg.green, n);
		c->blue = Mix(bg.blue, fg.blue, n);
		return;
	}
	rgb = t->rgb[role];
	c->red = Channel(rgb, 16);
	c->green = Channel(rgb, 8);
	c->blue = Channel(rgb, 0);
}

void ThemePort(void)
{
	RGBColor c;

	if (HasColorQD()) {
		ThemeColor(kThBg, &c);
		RGBBackColor(&c);
		ThemeColor(kThText, &c);
		RGBForeColor(&c);
	} else if (ThemeDark()) {
		BackPat(&qd.black);
		TextMode(srcBic);
		return;
	}
	BackPat(&qd.white);
	TextMode(srcOr);
}

void ThemePlain(void)
{
	ForeColor(blackColor);
	BackColor(whiteColor);
	BackPat(&qd.white);
	TextMode(srcOr);
}

short ThemeTextMode(void)
{
	return !HasColorQD() && ThemeDark() ? srcBic : srcOr;
}

void ThemeWindow(WindowPtr w)
{
	GrafPtr save;
	CTabHandle t;

	if (HasColorQD()) {
		/* a table of its own (DisposeWindow may dispose of it), holding just
		   the content color */
		t = (CTabHandle) NewHandleClear(sizeof(ColorTable));
		if (t != NULL) {
			RGBColor bg;
			ThemeColor(kThBg, &bg);
			(**t).ctSize = 0;
			(**t).ctTable[0].value = wContentColor;
			(**t).ctTable[0].rgb = bg;
			SetWinColor(w, t);
		}
	}
	GetPort(&save);
	SetPort(w);
	ThemePort();
	SetPort(save);
}
