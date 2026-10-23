/*
 * preview.h: the Preview window, the post laid out like its web page. One
 * window follows whichever post is in front, and lays it out again a
 * moment after it changes. Double-click a place to go there in the post.
 */
#ifndef TOPICA_PREVIEW_H
#define TOPICA_PREVIEW_H

#include <Windows.h>
#include <Events.h>
#include "docwin.h"

/* Opens the window (or brings it forward) showing d */
void PreviewShow(Doc *d);
void PreviewClose(void);
Boolean PreviewIsFront(void);
Boolean IsPreview(WindowPtr w);

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

/* From the event loop: follows the front post, lays it out when due */
void PreviewIdle(void);

#endif
