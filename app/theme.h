/*
 * theme.h: the colors posts and the preview are shown in (Post > Theme).
 * Light and Dark work on every Mac: on one without Color QuickDraw, Dark
 * is white text on a black pattern. The color themes need Color
 * QuickDraw and aren't in Topica Lite. Printing is always black on white.
 */
#ifndef TOPICA_THEME_H
#define TOPICA_THEME_H

#include <Quickdraw.h>
#include <Windows.h>

/* What a color is for */
enum {
	kThBg,              /* the background */
	kThText,            /* the text */
	kThMark,            /* Markdown's marks, URLs, a byline */
	kThCode,
	kThLink,
	kThHeading,
	kThMeta,            /* the front matter */
	kThShade,           /* the preview's code and image boxes */
	kThLine             /* the preview's rules, frames and quote bars */
};

/* The theme gPrefs.theme names (Light when there's none) */
void ThemeInit(void);
short ThemeCount(void);
const char *ThemeName(short i);
/* A color theme without Color QuickDraw isn't */
Boolean ThemeUsable(short i);
short ThemeCurrent(void);
/* Chooses theme i and saves the preference; the caller shows it */
void ThemeChoose(short i);
Boolean ThemeDark(void);
/* A role's color in the theme. On a Mac without Color QuickDraw, Dark
   inverts instead (ThemePort): every role is black. */
void ThemeColor(short role, RGBColor *c);
/* The current port in the theme's colors, for a post: its background and
   text. ThemePlain puts it back to black on white, for the scroll bar
   and the grow box, which draw with the port's colors on System 6. */
void ThemePort(void);
void ThemePlain(void);
/* A window in the theme: its port, and with Color QuickDraw the color the
   Window Manager erases its updates in */
void ThemeWindow(WindowPtr w);
/* The text mode a post's TextEdit draws in: srcBic for Dark without
   Color QuickDraw (white on black), srcOr otherwise */
short ThemeTextMode(void);

#endif
