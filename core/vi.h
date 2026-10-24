/*
 * vi.h: vi's normal, insert and visual modes over the editor's text. The
 * app gives it every key while vi mode is on; it reads the text and moves
 * or changes it through ViHost, so it knows nothing of TextEdit.
 *
 * Text is Mac Roman with CR (or LF) line ends. A blog post's paragraph is
 * one long line, so j and k move by the lines on the screen (the host
 * knows where they are), while operators take whole lines: dj deletes
 * this line and the next.
 *
 * Normal mode: counts; h j k l, w b e W B E, 0 ^ $, gg G, { }, f F t T ; ,
 * as motions; d c y with a motion, a text object (iw aw iW aW ip ap i" a"
 * i' a' i( a( ib i[ a[ i{ a{ iB i< a<) or doubled (dd cc yy); x X D C s S
 * Y r J ~ p P; i a I A o O; v V; u, Control-R and U redo; . repeats the
 * last change; / ? n N search through the host. Escape leaves insert and
 * visual mode, and cancels what's half typed.
 *
 * Registers "a to "z for y d c p ("A appends), from the host; marks m{a-z},
 * with ' and ` to go back (motions too); and a command line: :N goes to
 * line N, and :w :q :wq :x :q! (and ZZ ZQ) are the host's.
 */
#ifndef TOPICA_VI_H
#define TOPICA_VI_H

enum { VI_NORMAL, VI_INSERT, VI_VISUAL, VI_VISUAL_LINE };

/* Keys that aren't characters */
enum {
	VI_ESC = 0x1B,              /* Escape, Command-period, or jk */
	VI_BACKSPACE = 0x08,
	VI_RETURN = 0x0D
};

/* What a replace is, for the host's undo: typing and deleting coalesce */
enum { VI_EDIT, VI_TYPE, VI_DELETE };

/* The host's find, for / ? n N */
enum { VI_FIND_ASK, VI_FIND_NEXT, VI_FIND_PREVIOUS };

/* The register y d c and p use: one for every window, as vi's is */
typedef struct ViReg {
	char *buf;
	long len, cap;
	int lines;                  /* it holds whole lines */
} ViReg;

typedef struct {
	void *ctx;
	/* the text; valid until the next replace */
	const char *(*text)(void *ctx, long *len);
	void (*get_sel)(void *ctx, long *start, long *end);
	void (*set_sel)(void *ctx, long start, long end);
	/* [start, end) becomes ins; 0 when it couldn't (the post would be too long) */
	int (*replace)(void *ctx, int kind, long start, long end, const char *ins, long len);
	/* the offset lines screen lines below pos (above when negative) at the
	   same horizontal place: *goal is that place, -1 to take pos's */
	long (*vertical)(void *ctx, long pos, int lines, long *goal);
	void (*undo)(void *ctx, int redo);
	/* begin (1) and end (0) a change that undoes as one */
	void (*group)(void *ctx, int begin);
	void (*find)(void *ctx, int how);
	void (*beep)(void *ctx);
	/* register a to z, made when first asked for; NULL when it can't be */
	ViReg *(*named)(void *ctx, int name);
	/* :w :q :wq :x :q! and the like; 0 when it couldn't */
	int (*command)(void *ctx, const char *cmd);
} ViHost;

#define VI_KEYS 256                 /* the longest change . repeats */

typedef struct {
	int mode;
	int on;                     /* vi mode is on: else every key is the app's */
	int jk;                     /* jk in insert mode is Escape */
	long anchor, cur;           /* a visual selection's ends: where it began, the cursor */
	long goal;                  /* j and k's horizontal place, -1 for none */
	long count, opCount;        /* counts typed before the command and before the operator */
	char op;                    /* d c y, or 0 */
	char pend;                  /* a key that waits for one more: g r f F t T i a (objects) */
	char findCmd, findCh;       /* the last f F t T and its character, for ; and , */
	ViReg *reg;
	/* the change being typed, and the last one, for . */
	unsigned char rec[VI_KEYS], dot[VI_KEYS];
	int recLen, dotLen;
	int recOk;                  /* the change being typed can be repeated */
	int replaying;
	/* an insert: where it began, its count, what started it */
	long insStart;
	long insCount;
	int insMoved;               /* the caret left the insert (arrow keys): no count repeat */
	int grouped;                /* an undo group is open */
	char useReg;                /* the register "x named for this command, or 0 */
	int regHeld;                /* it was just named: the command hasn't begun */
	long markLine[26];          /* m{a-z}: the line (-1 when unset) and column, as vim keeps them */
	long markCol[26];
	char line[34];              /* the command line, ":" and what's typed */
	int lineLen;
	/* jk */
	long jAt;                   /* where the j went, -1 if the last key wasn't one */
	unsigned long jTicks;
} Vi;

/* reg->buf (reg->cap bytes) is the caller's */
void vi_init(Vi *v, ViReg *reg);

/* A key typed (c, its character) at ticks. 1 when vi used it; 0 when the
   app should do what it always does (arrow keys and the like, and every
   key when vi mode is off). */
int vi_key(Vi *v, const ViHost *h, int c, unsigned long ticks);

/* After a click: a selection made with the mouse is a visual one; an
   insert goes on where the click was */
void vi_clicked(Vi *v, const ViHost *h);

/* The mode for the status line: "", "-- INSERT --", "-- VISUAL --" ... */
const char *vi_status(const Vi *v);

#endif
