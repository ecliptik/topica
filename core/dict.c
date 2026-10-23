/* dict.c: see dict.h. */
#include <stddef.h>
#include <string.h>

#include "dict.h"

static unsigned long Get32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | p[3];
}

static unsigned Get16(const unsigned char *p)
{
	return ((unsigned) p[0] << 8) | p[1];
}

int dict_header(const unsigned char *head, long *blocks, long *words, long *indexLen)
{
	if (memcmp(head, "TPDC", 4) != 0 || Get16(head + 4) != 1)
		return 0;
	*blocks = (long) Get16(head + 6);
	*words = (long) Get32(head + 8);
	*indexLen = (long) Get32(head + 12);
	return *blocks > 0;
}

int dict_init(Dict *d, const unsigned char *index, long indexLen, long blocks, long words, long *entries)
{
	long i, at = 0, len;

	memset(d, 0, sizeof(*d));
	d->index = index;
	d->entries = entries;
	d->blocks = blocks;
	d->words = words;
	for (i = 0; i < blocks; i++) {
		if (at + 7 > indexLen)
			return 0;
		entries[i] = at;
		len = (long) Get16(index + at + 4);
		if (len > d->maxBlock)
			d->maxBlock = len;
		at += 7 + index[at + 6];
	}
	return at == indexLen;
}

void dict_data(Dict *d, const unsigned char *data)
{
	d->data = data;
}

void dict_reader(Dict *d, DictReadFn read, void *ctx, long base, unsigned char *cache, int slots,
		long *cached, unsigned long *used)
{
	int i;

	d->read = read;
	d->ctx = ctx;
	d->base = base;
	d->cache = cache;
	d->slots = slots;
	d->cached = cached;
	d->used = used;
	for (i = 0; i < slots; i++) {
		cached[i] = -1;
		used[i] = 0;
	}
}

/* Bytes compared, then length: the order mkdict sorts in */
static int Compare(const unsigned char *a, int alen, const unsigned char *b, int blen)
{
	int n = alen < blen ? alen : blen, i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return a[i] - b[i];
	return alen - blen;
}

/* Block k's bytes, or NULL */
static const unsigned char *Block(Dict *d, long k, long *len)
{
	const unsigned char *e = d->index + d->entries[k];
	long off = (long) Get32(e);
	int i, slot = 0;

	*len = (long) Get16(e + 4);
	if (d->data != NULL)
		return d->data + off;
	if (d->read == NULL || d->slots == 0 || *len > d->maxBlock)
		return NULL;
	d->clock++;
	for (i = 0; i < d->slots; i++) {
		if (d->cached[i] == k) {
			d->used[i] = d->clock;
			return d->cache + (long) i * d->maxBlock;
		}
		if (d->used[i] < d->used[slot])
			slot = i;               /* the least recently used */
	}
	if (d->read(d->ctx, d->base + off, d->cache + (long) slot * d->maxBlock, *len) != *len) {
		d->cached[slot] = -1;
		d->failed = 1;
		return NULL;
	}
	d->cached[slot] = k;
	d->used[slot] = d->clock;
	return d->cache + (long) slot * d->maxBlock;
}

int dict_has(Dict *d, const char *word, int len)
{
	const unsigned char *w = (const unsigned char *) word, *b, *e;
	long lo = 0, hi = d->blocks - 1, mid, blen, p;
	int m = 0, k;

	if (len <= 0 || len >= DICT_WORD || d->blocks == 0)
		return 0;
	/* the last block whose first word isn't after the word */
	while (lo < hi) {
		mid = (lo + hi + 1) / 2;
		e = d->index + d->entries[mid];
		if (Compare(e + 7, e[6], w, len) <= 0)
			lo = mid;
		else
			hi = mid - 1;
	}
	b = Block(d, lo, &blen);
	if (b == NULL)
		return d->failed;               /* no dictionary to say no */
	/* Each word shares some bytes with the one before. The one before
	   matched the word's first m bytes and sorted before it: a word that
	   shares fewer comes after it, one that shares more still comes
	   before, and only one that shares exactly m needs looking at. */
	for (p = 0; p + 2 <= blen; p += 2 + b[p + 1]) {
		int shared = b[p], rest = b[p + 1];
		const unsigned char *r = b + p + 2;
		if (p + 2 + rest > blen)
			return 0;                   /* damaged */
		if (shared < m)
			return 0;
		if (shared > m)
			continue;
		for (k = 0; k < rest && m + k < len && r[k] == w[m + k]; k++)
			;
		if (k == rest && m + k == len)
			return 1;
		if (m + k == len || (k < rest && r[k] > w[m + k]))
			return 0;                   /* it comes after the word */
		m += k;
	}
	return 0;
}

void dict_scan(Dict *d, const char *prefix, int plen, DictWordFn fn, void *ctx)
{
	const unsigned char *pre = (const unsigned char *) prefix, *b, *e;
	unsigned char cur[256];
	long lo = 0, hi = d->blocks - 1, mid, blen, p, k;
	int clen = 0, c;

	if (plen <= 0 || plen >= DICT_WORD || d->blocks == 0)
		return;
	while (lo < hi) {
		mid = (lo + hi + 1) / 2;
		e = d->index + d->entries[mid];
		if (Compare(e + 7, e[6], pre, plen) < 0)
			lo = mid;
		else
			hi = mid - 1;
	}
	for (k = lo; k < d->blocks; k++) {
		b = Block(d, k, &blen);
		if (b == NULL)
			return;
		for (p = 0; p + 2 <= blen; p += 2 + b[p + 1]) {
			int shared = b[p], rest = b[p + 1];
			if (shared > clen || p + 2 + rest > blen)
				return;                 /* damaged */
			memcpy(cur + shared, b + p + 2, (size_t) rest);
			clen = shared + rest;
			c = memcmp(cur, pre, (size_t) (clen < plen ? clen : plen));
			if (c > 0)
				return;                 /* past the words with the prefix */
			if (c == 0 && clen >= plen)
				fn(ctx, (const char *) cur, clen);
		}
		clen = 0;
	}
}
