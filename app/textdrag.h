/*
 * textdrag.h: dragging text in and out of posts (System 7.5's Drag
 * Manager). Press on the selection and move: the text goes as a drag, and
 * moves when dropped in the same post, copies anywhere else (another post,
 * another application, a clipping on the desktop). Text dropped on a post
 * goes where the caret shows. Each drop is one undoable step. Not in
 * Topica Lite, which is for 512K Macs on System 6.
 */
#ifndef TOPICA_TEXTDRAG_H
#define TOPICA_TEXTDRAG_H

#include "docwin.h"

#ifndef TOPICA_LITE
void DragInit(void);                    /* once: is there a Drag Manager? */
void DragInstall(WindowPtr w);          /* a post's window: drops onto it */
void DragRemove(WindowPtr w);           /* before it's disposed of */
/* A press in the view: true when it was on the selection and became a
   drag (otherwise the press is the caller's, a click) */
Boolean DragText(Doc *d, EventRecord *e);
#else
#define DragInit() ((void) 0)
#define DragInstall(w) ((void) 0)
#define DragRemove(w) ((void) 0)
#define DragText(d, e) false
#endif

#endif
