/*
 * layout.h: breaks a page block's display text (mdpage) into lines. The
 * app measures its fonts once (QuickDraw's CharWidth, FontInfo) into
 * LayFonts; breaking is then arithmetic, the same for the screen and the
 * printer.
 */
#ifndef TOPICA_LAYOUT_H
#define TOPICA_LAYOUT_H

#include "mdpage.h"

typedef struct {
	unsigned char width[MF_COUNT][256];
	short ascent[MF_COUNT], descent[MF_COUNT], leading[MF_COUNT];
} LayFonts;

typedef struct {
	long start, len;            /* in the display text, without the spaces it broke at */
	short width;                /* in pixels */
	short ascent, height;       /* the baseline from the line's top; the line's height */
} LayLine;

/* The line that starts at start, no wider than w (but at least one
   character): fills line and returns where the next one starts (n at
   the end). Code keeps its spaces; prose drops them where it breaks. */
long lay_line(const LayFonts *f, const MpBlock *b, const char *text, const unsigned char *sty, long n,
		long start, int w, LayLine *line);

/* How wide [start, start + len) is */
int lay_width(const LayFonts *f, const MpBlock *b, const char *text, const unsigned char *sty,
		long start, long len);

#endif
