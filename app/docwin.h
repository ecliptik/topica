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

/* The Edit menu */
enum { kEdCut, kEdCopy, kEdPaste, kEdClear, kEdSelectAll };
void DocEdit(Doc *d, short cmd);
void DocUndo(Doc *d, Boolean redo);
/* What Undo (redo = 0) or Redo would reverse: an undo.h kind */
int DocUndoKind(Doc *d, Boolean redo);
Boolean DocHasSelection(Doc *d);
Boolean DocIsDirty(Doc *d);
Boolean DocHasFile(Doc *d);
void DocSealUndo(Doc *d);

/* For the Post menu's dialogs */
Editor *DocEditor(Doc *d);
/* Replaces [start, end) with text as one undoable change; 0 (after an
   alert) when the post would be too long */
Boolean DocReplace(Doc *d, int kind, long start, long end, const char *text, long len);

#endif
