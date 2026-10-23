/*
 * editor.h: the text editor inside a document window. Callers see only this
 * interface; te_editor.c puts TextEdit behind it (32,000 characters at
 * most), and a custom engine can replace it later.
 *
 * Every change to the text goes through EdReplace, so undo, vi mode and
 * styling see each edit the same way. Text is Mac Roman with CR line ends.
 */
#ifndef TOPICA_EDITOR_H
#define TOPICA_EDITOR_H

#include <Quickdraw.h>
#include <Events.h>

typedef struct Editor Editor;

/* A new, empty editor drawing into the current port's view rectangle */
Editor *EdNew(const Rect *view, short font, short size);
void EdDispose(Editor *ed);

/* Styles: core/mdscan.h's classes, one per character of [start, end); the
   editor decides how each looks. 0 is plain text. */
Boolean EdCanStyle(Editor *ed);
void EdSetClasses(Editor *ed, long start, long end, const unsigned char *cls);

/* The most text the editor can hold */
long EdMaxLength(void);

void EdSetView(Editor *ed, const Rect *view);  /* after the window is resized */
void EdSetFont(Editor *ed, short font, short size);   /* in the window's port */
void EdDraw(Editor *ed);                       /* in an update */
void EdActivate(Editor *ed, Boolean active);
void EdIdle(Editor *ed);                       /* blinks the caret */
/* A click in the view; extend: Shift was down */
void EdClick(Editor *ed, Point local, Boolean extend);

/* For drag and drop: the offset nearest a point in the view, where the
   caret for an offset would be, and the selection's outline (a new region,
   local coordinates, clipped to the view; empty with no selection) */
long EdOffsetAt(Editor *ed, Point local);
void EdCaretRect(Editor *ed, long offset, Rect *r);
RgnHandle EdSelectionRgn(Editor *ed);
/* Arrow, Home, End, Page Up and Page Down keys: 1 if it was one */
Boolean EdNavigate(Editor *ed, char c, short modifiers);

long EdLength(Editor *ed);
/* The text: valid until the next change; don't change it */
const char *EdLockText(Editor *ed);
void EdUnlockText(Editor *ed);

void EdGetSelection(Editor *ed, long *start, long *end);
void EdSetSelection(Editor *ed, long start, long end);

/* Replaces [start, end) with text; 0 when the result would be too long.
   The caret goes after the new text. */
Boolean EdReplace(Editor *ed, long start, long end, const char *text, long len);
/* Replaces all the text (no undo); 0 when it is too long */
Boolean EdSetText(Editor *ed, const char *text, long len);

/* Scrolling, in lines: the first line shown, the most it can be, and
   how many lines fit */
void EdScrollInfo(Editor *ed, short *value, short *max, short *page);
void EdScrollTo(Editor *ed, short value);
void EdShowSelection(Editor *ed);

/* The line a text offset is on (0-based) */
long EdLineOf(Editor *ed, long offset);

/* The offset lines screen lines below pos (above when negative), at the
   horizontal place *goal; *goal < 0 takes pos's own (for vi's j and k) */
long EdVertical(Editor *ed, long pos, int lines, long *goal);

/* Puts the selection on the Clipboard */
void EdCopySelection(Editor *ed);
/* The Clipboard's text: a handle valid until the next Clipboard call, and
   its length (0 when the Clipboard holds no text) */
Handle EdClipboard(long *len);

/* TextEdit's own scrap to the Clipboard and back (dialogs use them too) */
void TEScrapToClipboard(void);
Boolean TEScrapFromClipboard(void);

#endif
