/*
 * prefs.h: Topica's preferences, kept as "key: value" lines in a text file
 * (Topica Preferences, in the System Folder), so they can be read and fixed
 * in TeachText. Unknown keys are ignored; a missing key keeps its default.
 */
#ifndef TOPICA_PREFS_H
#define TOPICA_PREFS_H

enum { kProfileJekyll = 0, kProfileHugo = 1 };

typedef struct {
	char font[32];              /* "Monaco", "Geneva", "Courier" */
	short size;                 /* 9, 10, 12 */
	char imagePath[64];         /* where the blog keeps a post's images */
	char layout[32];            /* the new post's layout (Jekyll) */
	short profile;              /* kProfileJekyll, kProfileHugo */
	short utf8;                 /* write UTF-8 (else Mac Roman) */
	short lf;                   /* LF line ends (else CR) */
	short autosaveMinutes;      /* recovery copies; 0: none */
	short format;               /* Markdown styled in place */
	short vi;                   /* vi keys */
	short viJk;                 /* jk in insert mode is Escape */
	char bridge[64];            /* Publish's bridge: "192.0.2.10:8023" */
	char bridgeKey[64];         /* the key it asks for */
	short yearFolders;          /* a post goes in a folder for its year: _posts/2026/ */
	char wordService[160];      /* the Word Services checker: "Disk:Folder:Excalibur" */
	char theme[32];             /* the colors: "Light", "Dark", "Solarized Dark"... */
} TopicaPrefs;

void prefs_default(TopicaPrefs *p);
/* Reads "key: value" lines (CR or LF) over the defaults already in p */
void prefs_read(TopicaPrefs *p, const char *text, long n);
/* Writes them; returns the length, -1 when it doesn't fit (out NULL only measures) */
long prefs_write(const TopicaPrefs *p, char eol, char *out, long cap);

#endif
