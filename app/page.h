/*
 * page.h: a post laid out like its web page, in the current port: the
 * preview window draws a part of it at a time, printing a page at a time.
 * Built on core/mdpage (the blocks and their text) and core/layout (the
 * lines).
 *
 * A 68000 lays out a long post slowly, so layout comes in two steps.
 * PageLayout reads the blocks and guesses their heights; PageRefine lays
 * blocks out exactly, the ones on the screen first and the rest a few at
 * a time. After an edit, the blocks it didn't touch keep their heights.
 */
#ifndef TOPICA_PAGE_H
#define TOPICA_PAGE_H

#include <Quickdraw.h>

typedef struct Page Page;

Page *PageNew(void);
void PageDispose(Page *pg);

/* Reads text's blocks for a page width pixels wide, measuring the fonts in
   the current port the first time. [from, to) is what changed since the
   last layout (in the text as it is now), and what came after it moved by
   delta: blocks outside it keep their heights. from = 0, to = n for all.
   0 when memory ran out. */
Boolean PageLayout(Page *pg, const char *text, long n, short width, long from, long to, long delta);

/* Lays blocks out exactly: those between y top and bottom first, then
   others in order for up to ticks (0: none, -1: all of them). *shift is
   how far what was at top moved, *redraw whether anything between top and
   bottom changed. 1 when every block is exact. */
Boolean PageRefine(Page *pg, const char *text, long n, long top, long bottom, long ticks,
		long *shift, Boolean *redraw);

/* The page's height in pixels */
long PageHeight(const Page *pg);

/* Draws the page from y top down into r of the current port, its left
   edge at r->left. color: draw in color (a deep enough screen). */
void PageDraw(Page *pg, const char *text, long n, long top, const Rect *r, Boolean color);

/* Where a printed page that starts at top should end: the bottom of the
   last line that fits by limit */
long PageBreak(Page *pg, const char *text, long n, long top, long limit);

/* The source offset of what's drawn at y, for going there in the post;
   -1 above the body */
long PageSourceAt(const Page *pg, long y);

#endif
