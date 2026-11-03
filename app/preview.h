/*
 * preview.h: the preview, the post laid out like its web page, laid out
 * again a moment after the post changes. Where the screen has room, it's a
 * pane on the right of the post's own window (a divider between them
 * drags); on a small screen, a window of its own that follows whichever
 * post is in front. Double-click a place to go there in the post.
 */
#ifndef TOPICA_PREVIEW_H
#define TOPICA_PREVIEW_H

#include <Windows.h>
#include <Events.h>
#include "docwin.h"

/* Opens the window (or brings it forward) showing d */
void PreviewShow(Doc *d);
void PreviewClose(void);
/* After Post > Theme */
void PreviewApplyTheme(void);
/* The preview's own window is in front */
Boolean PreviewIsFront(void);
/* Its own window, not a pane */
Boolean IsPreview(WindowPtr w);
/* d's preview shows: its pane, or the preview's window in front */
Boolean PreviewShowing(Doc *d);

/* The post the window shows, or NULL */
Doc *PreviewDoc(void);

/* d is closing */
void PreviewForget(Doc *d);

/* Events for the window */
void PreviewUpdate(void);
void PreviewActivate(Boolean active);
void PreviewClick(EventRecord *e);
void PreviewKey(EventRecord *e);
void PreviewGrow(Point where);
void PreviewZoom(Point where, short part);

/* For the pane, from the post's window (docwin.c): it changed size, it
   needs drawing (in an update), it was activated */
void PreviewPaneResized(void);
void PreviewPaneDraw(void);
void PreviewPaneActivate(Boolean active);

/* From the event loop: follows the front post, lays it out when due */
void PreviewIdle(void);

#endif
