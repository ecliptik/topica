/*
 * undo.h: multi-level undo and redo. Each step is one replacement: at pos,
 * the deleted bytes were replaced by the inserted ones. Typing and
 * Delete-key runs join into one step until the stack is sealed (a click,
 * a menu command). The steps live in a caller's buffer; when it is full the
 * oldest steps go, and a step bigger than the whole buffer clears it.
 */
#ifndef TOPICA_UNDO_H
#define TOPICA_UNDO_H

enum {
	kUndoNone = 0,
	kUndoTyping,
	kUndoDelete,        /* the Delete key */
	kUndoCut,
	kUndoPaste,
	kUndoClear,
	kUndoPostInfo,
	kUndoInsert,        /* an Insert menu item */
	kUndoReplace,       /* Find/Replace, vi */
	kUndoKinds
};

typedef struct {
	char *buf;
	long cap;
	long used;          /* bytes of steps */
	int count;          /* steps */
	int done;           /* steps applied; [done, count) can be redone */
	int open;           /* the last step can still grow */
	int lost;           /* history was dropped: not everything can be undone */
	int group;          /* 0, or a group is open: 1 before its first step, 2 after */
} UndoStack;

/* One step to apply: replace [pos, pos + removeLen) with insert. */
typedef struct {
	int kind;
	long pos;
	long removeLen;
	const char *insert;
	long insertLen;
	/* where the selection goes afterwards */
	long selStart, selEnd;
	int more;           /* another step goes with this one: step again */
} UndoApply;

void undo_init(UndoStack *u, char *buf, long cap);
void undo_clear(UndoStack *u);
void undo_seal(UndoStack *u);

/* Steps recorded between begin (1) and end (0) undo and redo as one, as
   vi's changes do: an operator and the typing after it */
void undo_group(UndoStack *u, int begin);

/* Records a change before or after it is made: at pos, del (dlen bytes)
   was replaced by ins (ilen bytes). */
void undo_record(UndoStack *u, int kind, long pos, const char *del, long dlen, const char *ins, long ilen);

/* The kind the next undo (redo = 0) or redo (1) would reverse, or kUndoNone. */
int undo_peek(const UndoStack *u, int redo);

/* Fills a with the change for the next undo or redo and moves past it.
   0 when there is none. The insert bytes stay valid until the next call. */
int undo_step(UndoStack *u, int redo, UndoApply *a);

#endif
