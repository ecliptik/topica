/*
 * docwin.h: document windows. Each holds one post: its editor, a scroll
 * bar, a status line (words, reading time, line), its undo history and
 * where it is saved.
 */
#ifndef TOPICA_DOCWIN_H
#define TOPICA_DOCWIN_H

#include <Windows.h>
#include <Events.h>

#include "editor.h"
#include "fileio.h"

typedef struct Doc Doc;

/* A new untitled window; withTemplate: start from the post template */
Doc *DocNew(Boolean withTemplate);
/* Opens a file, or brings its window to the front; NULL (after an alert)
   when it can't */
Doc *DocOpen(const FileRef *f);
/* 0 when the writer cancelled; quitting changes the alert's wording */
Boolean DocClose(Doc *d, Boolean quitting);
Boolean DocSave(Doc *d);
Boolean DocSaveAs(Doc *d);
void DocRevert(Doc *d);

Doc *DocFromWindow(WindowPtr w);   /* NULL if it isn't a document window */
long DocId(Doc *d);                 /* a number for Apple events, unique while Topica runs */
Doc *DocFromId(long id);            /* NULL when there's none */
Doc *DocNth(short n);               /* the nth post window from the front, 1-based */
void DocShowStatus(Doc *d);         /* its status line drawn again (what Publish is doing) */
/* Scrolls the selection into view, with the scroll bar and the status
   line's Line following (for Find and Check Spelling, between events) */
void DocShowSelection(Doc *d);
Doc *DocFront(void);               /* the front document window, or NULL */
short DocCount(void);
short DocMaxCount(void);

/* Events for a document window */
void DocUpdate(Doc *d);
void DocActivate(Doc *d, Boolean active);
void DocIdle(Doc *d);
void DocClick(Doc *d, EventRecord *e);
void DocKey(Doc *d, EventRecord *e);
void DocGrow(Doc *d, Point where);
void DocZoom(Doc *d, Point where, short part);
void DocAdjustCursor(Doc *d, Point where);

/* vi mode: Command-period is Escape */
Boolean DocViOn(Doc *d);
void DocViEscape(Doc *d);

/* The Edit menu */
enum { kEdCut, kEdCopy, kEdPaste, kEdClear, kEdSelectAll };
void DocEdit(Doc *d, short cmd);
void DocUndo(Doc *d, Boolean redo);
/* What Undo (redo = 0) or Redo would reverse: an undo.h kind */
int DocUndoKind(Doc *d, Boolean redo);
Boolean DocHasSelection(Doc *d);
Boolean DocIsDirty(Doc *d);
Boolean DocHasFile(Doc *d);
/* Where it is saved; 0 while it has no file */
Boolean DocFile(Doc *d, FileRef *f);
void DocSealUndo(Doc *d);
/* The preview's pane on the right of the window: width wide (made room
   for, the post giving way down to half the screen), never narrower than
   min; 0 takes it away and the window back to the post's width */
void DocSetPane(Doc *d, short width, short min);
/* Where the pane is (local coordinates) */
void DocPaneRect(Doc *d, Rect *r);
/* The text's area in the window (local coordinates) */
void DocTextRect(Doc *d, Rect *r);
/* Edits between begin and end undo as one (a drag that moves text) */
void DocGroupEdits(Doc *d, Boolean begin);
/* The edits between begin and end undo with the last one, if it was a kind
   edit (a checker's corrections) */
void DocJoinEdits(Doc *d, short kind, Boolean begin);

/* Opens the recovery copies a crash left behind; how many */
short DocRecoverAll(void);
/* After Preferences: the font and size in every window */
void DocApplySettings(void);
/* After Show or Hide Formatting: every window restyled when idle */
void DocRestyleAll(void);
/* After Post > Theme: every post in the new colors */
void DocApplyTheme(void);
Boolean DocCanStyle(Doc *d);
/* Puts the caret at offset, with its line at the top of the window */
void DocShowLine(Doc *d, long offset);
/* Recovery copies for every window, from the event loop */
void DocIdleAll(void);
/* Before an event: vi's block cursors off, back when idle */
void DocBlocksOff(void);

/* For the Post menu's dialogs */
Editor *DocEditor(Doc *d);
WindowPtr DocWindow(Doc *d);
/* Goes up with every change to the text: the preview compares it */
unsigned long DocChanges(Doc *d);
/* The text changed since the last call: [*from, *to) now, and what
   followed it moved by *delta. 0 when nothing did. */
Boolean DocTakeEdits(Doc *d, long *from, long *to, long *delta);
/* Replaces [start, end) with text as one undoable change; 0 (after an
   alert) when the post would be too long */
Boolean DocReplace(Doc *d, int kind, long start, long end, const char *text, long len);

#endif
