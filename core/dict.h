/*
 * dict.h: a section of the spelling dictionary (tools/mkdict.py writes
 * them): words sorted and front-coded in blocks, with an index of each
 * block's first word. The index stays in memory; the blocks can too, or
 * be read from the file a few at a time into a cache, as a 512K Mac does.
 */
#ifndef TOPICA_DICT_H
#define TOPICA_DICT_H

enum { DICT_HEADER = 16, DICT_WORD = 32 };     /* a section's header; the longest word, with room */

/* Reads len bytes at offset of the file into buf: how many it read */
typedef long (*DictReadFn)(void *ctx, long offset, unsigned char *buf, long len);

typedef struct {
	const unsigned char *index;     /* the index, in memory */
	long *entries;                  /* where each block's entry starts in it */
	long blocks, words;
	long maxBlock;                  /* the longest block */
	/* the blocks: in memory, or read with read from base on */
	const unsigned char *data;
	DictReadFn read;
	void *ctx;
	long base;
	unsigned char *cache;           /* slots of maxBlock bytes */
	long *cached;                   /* which block each slot holds, -1 none */
	unsigned long *used;            /* when each was last used */
	int slots;
	unsigned long clock;
	int failed;                     /* a read failed: words count as right */
} Dict;

/* From a section's header: its blocks, its words and its index's length.
   0 if it isn't one. */
int dict_header(const unsigned char *head, long *blocks, long *words, long *indexLen);

/* With the index (indexLen bytes) and room for its entries (blocks of
   them). 0 if the index is damaged. */
int dict_init(Dict *d, const unsigned char *index, long indexLen, long blocks, long words, long *entries);

/* The blocks are all in memory, from data on */
void dict_data(Dict *d, const unsigned char *data);

/* The blocks are read from base on, into cache: slots blocks of maxBlock
   bytes (dict_init sets it), with cached and used slots long */
void dict_reader(Dict *d, DictReadFn read, void *ctx, long base, unsigned char *cache, int slots,
		long *cached, unsigned long *used);

/* 1 when the word (exactly these bytes) is in the section */
int dict_has(Dict *d, const char *word, int len);

/* Calls fn with each word that starts with prefix, in order */
typedef void (*DictWordFn)(void *ctx, const char *word, int len);
void dict_scan(Dict *d, const char *prefix, int plen, DictWordFn fn, void *ctx);

#endif
