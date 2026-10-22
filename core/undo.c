/* undo.c: see undo.h. A step is a 16-byte header, the deleted bytes, then
   the inserted bytes, padded to an even length. */
#include <string.h>

#include "undo.h"

typedef struct {
	long pos, dlen, ilen;
	short kind, pad;
} Hdr;

#define HDR 16L                 /* packed big-endian, the same on every host */

static long StepSize(long dlen, long ilen)
{
	return (HDR + dlen + ilen + 1) & ~1L;
}

static long Get32(const char *p)
{
	const unsigned char *b = (const unsigned char *) p;
	return (long) (((unsigned long) b[0] << 24) | ((unsigned long) b[1] << 16) | ((unsigned long) b[2] << 8) | b[3]);
}

static void Put32(char *p, long v)
{
	p[0] = (char) (v >> 24);
	p[1] = (char) (v >> 16);
	p[2] = (char) (v >> 8);
	p[3] = (char) v;
}

static void GetHdr(const UndoStack *u, long off, Hdr *h)
{
	const char *p = u->buf + off;

	h->pos = Get32(p);
	h->dlen = Get32(p + 4);
	h->ilen = Get32(p + 8);
	h->kind = (short) Get32(p + 12);
	h->pad = 0;
}

static void SetHdr(UndoStack *u, long off, const Hdr *h)
{
	char *p = u->buf + off;

	Put32(p, h->pos);
	Put32(p + 4, h->dlen);
	Put32(p + 8, h->ilen);
	Put32(p + 12, h->kind);
}

/* The byte offset of step i */
static long Offset(const UndoStack *u, int i)
{
	long off = 0;
	Hdr h;

	while (i-- > 0) {
		GetHdr(u, off, &h);
		off += StepSize(h.dlen, h.ilen);
	}
	return off;
}

void undo_init(UndoStack *u, char *buf, long cap)
{
	u->buf = buf;
	u->cap = cap;
	undo_clear(u);
}

void undo_clear(UndoStack *u)
{
	u->used = 0;
	u->count = 0;
	u->done = 0;
	u->open = 0;
	u->lost = 0;
}

void undo_seal(UndoStack *u)
{
	u->open = 0;
}

/* Drops the oldest step; 0 when there is none to drop besides keep (the
   step being grown). */
static int DropOldest(UndoStack *u, int keep)
{
	Hdr h;
	long size;

	if (u->count == 0 || (keep && u->count == 1))
		return 0;
	GetHdr(u, 0, &h);
	size = StepSize(h.dlen, h.ilen);
	memmove(u->buf, u->buf + size, (size_t) (u->used - size));
	u->used -= size;
	u->count--;
	u->done--;
	u->lost = 1;
	return 1;
}

/* Room for need more bytes; 0 when the buffer can never hold them. */
static int MakeRoom(UndoStack *u, long need, int keep)
{
	while (u->used + need > u->cap)
		if (!DropOldest(u, keep))
			return 0;
	return 1;
}

static void Overflow(UndoStack *u)
{
	undo_clear(u);
	u->lost = 1;
}

/* Joins a change to the last step when it continues it; 1 if it did. */
static int Coalesce(UndoStack *u, int kind, long pos, const char *del, long dlen, const char *ins, long ilen)
{
	long off, grow;
	Hdr h;

	if (!u->open || u->count == 0 || u->done != u->count)
		return 0;
	off = Offset(u, u->count - 1);
	GetHdr(u, off, &h);
	if (kind == kUndoTyping && h.kind == kUndoTyping && dlen == 0 && pos == h.pos + h.ilen) {
		grow = StepSize(h.dlen, h.ilen + ilen) - StepSize(h.dlen, h.ilen);
		if (!MakeRoom(u, grow, 1)) {
			Overflow(u);
			return 1;
		}
		off = Offset(u, u->count - 1);
		memcpy(u->buf + off + HDR + h.dlen + h.ilen, ins, (size_t) ilen);
		h.ilen += ilen;
		SetHdr(u, off, &h);
		u->used += grow;
		return 1;
	}
	if (kind == kUndoDelete && h.kind == kUndoTyping && ilen == 0 && dlen <= h.ilen
			&& pos + dlen == h.pos + h.ilen) {
		/* Delete after typing takes back what was typed */
		u->used -= StepSize(h.dlen, h.ilen) - StepSize(h.dlen, h.ilen - dlen);
		h.ilen -= dlen;
		SetHdr(u, off, &h);
		if (h.ilen == 0 && h.dlen == 0) {    /* all taken back: the step goes */
			u->used = off;
			u->count--;
			u->done--;
			u->open = 0;                    /* the step before it is sealed */
		}
		return 1;
	}
	if (kind == kUndoDelete && h.kind == kUndoDelete && ilen == 0 && h.ilen == 0
			&& (pos + dlen == h.pos || pos == h.pos)) {
		grow = StepSize(h.dlen + dlen, 0) - StepSize(h.dlen, 0);
		if (!MakeRoom(u, grow, 1)) {
			Overflow(u);
			return 1;
		}
		off = Offset(u, u->count - 1);
		if (pos == h.pos) {                     /* forward delete: append */
			memcpy(u->buf + off + HDR + h.dlen, del, (size_t) dlen);
		} else {                                /* backspace: prepend */
			memmove(u->buf + off + HDR + dlen, u->buf + off + HDR, (size_t) h.dlen);
			memcpy(u->buf + off + HDR, del, (size_t) dlen);
			h.pos = pos;
		}
		h.dlen += dlen;
		SetHdr(u, off, &h);
		u->used += grow;
		return 1;
	}
	return 0;
}

void undo_record(UndoStack *u, int kind, long pos, const char *del, long dlen, const char *ins, long ilen)
{
	long size, off;
	Hdr h;

	if (dlen == 0 && ilen == 0)
		return;
	if (u->done < u->count) {           /* a new change ends the redo steps */
		u->used = Offset(u, u->done);
		u->count = u->done;
		u->open = 0;
	}
	if (Coalesce(u, kind, pos, del, dlen, ins, ilen))
		return;
	size = StepSize(dlen, ilen);
	if (size > u->cap || !MakeRoom(u, size, 0)) {
		Overflow(u);
		return;
	}
	off = u->used;
	h.pos = pos;
	h.dlen = dlen;
	h.ilen = ilen;
	h.kind = (short) kind;
	h.pad = 0;
	SetHdr(u, off, &h);
	if (dlen > 0)
		memcpy(u->buf + off + HDR, del, (size_t) dlen);
	if (ilen > 0)
		memcpy(u->buf + off + HDR + dlen, ins, (size_t) ilen);
	u->used += size;
	u->count++;
	u->done = u->count;
	u->open = (kind == kUndoTyping || kind == kUndoDelete);
}

int undo_peek(const UndoStack *u, int redo)
{
	Hdr h;

	if (redo ? u->done >= u->count : u->done <= 0)
		return kUndoNone;
	GetHdr(u, Offset(u, redo ? u->done : u->done - 1), &h);
	return h.kind;
}

int undo_step(UndoStack *u, int redo, UndoApply *a)
{
	long off;
	Hdr h;

	if (redo ? u->done >= u->count : u->done <= 0)
		return 0;
	off = Offset(u, redo ? u->done : u->done - 1);
	GetHdr(u, off, &h);
	a->kind = h.kind;
	a->pos = h.pos;
	if (redo) {
		a->removeLen = h.dlen;
		a->insert = u->buf + off + HDR + h.dlen;
		a->insertLen = h.ilen;
		a->selStart = a->selEnd = h.pos + h.ilen;
		u->done++;
	} else {
		a->removeLen = h.ilen;
		a->insert = u->buf + off + HDR;
		a->insertLen = h.dlen;
		a->selStart = h.pos;
		a->selEnd = h.pos + h.dlen;
		u->done--;
	}
	u->open = 0;
	return 1;
}
