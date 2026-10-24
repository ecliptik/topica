/*
 * spell_run: checks a post's spelling on the host, with the dictionary
 * scripts/build.sh makes, read a block at a time as Topica Lite reads it.
 * Prints each word it doesn't know, with suggestions, then how many blocks
 * it read.
 *
 *     spell_run "Topica Dictionary" post.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/dict.h"
#include "../core/mdscan.h"
#include "../core/spell.h"

static FILE *gDictFile;
static int gReads;

static unsigned long Get32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | p[3];
}

static long Read(void *ctx, long offset, unsigned char *buf, long len)
{
	(void) ctx;
	gReads++;
	if (fseek(gDictFile, offset, SEEK_SET) != 0)
		return -1;
	return (long) fread(buf, 1, (size_t) len, gDictFile);
}

/* A section at offset: its header and index in memory, its blocks read */
static int Open(Dict *d, long offset, int inMemory)
{
	unsigned char head[DICT_HEADER], *index, *cache;
	long blocks, words, ilen, *entries, *cached;
	unsigned long *used;

	fseek(gDictFile, offset, SEEK_SET);
	if (fread(head, 1, sizeof(head), gDictFile) != sizeof(head) || !dict_header(head, &blocks, &words, &ilen))
		return 0;
	index = malloc((size_t) ilen);
	entries = malloc((size_t) blocks * sizeof(long));
	if (fread(index, 1, (size_t) ilen, gDictFile) != (size_t) ilen || !dict_init(d, index, ilen, blocks, words, entries))
		return 0;
	if (inMemory) {
		long size = 0, i;
		unsigned char *data;
		for (i = 0; i < blocks; i++) {
			const unsigned char *e = index + entries[i];
			long end = (long) Get32(e) + ((e[4] << 8) | e[5]);
			if (end > size)
				size = end;
		}
		data = malloc((size_t) size);
		if (fread(data, 1, (size_t) size, gDictFile) != (size_t) size)
			return 0;
		dict_data(d, data);
	} else {
		cache = malloc((size_t) (8 * d->maxBlock));
		cached = malloc(8 * sizeof(long));
		used = malloc(8 * sizeof(unsigned long));
		dict_reader(d, Read, NULL, offset + DICT_HEADER + ilen, cache, 8, cached, used);
	}
	return 1;
}

int main(int argc, char **argv)
{
	static char text[200000];
	static unsigned char cls[200000];
	unsigned char head[16];
	char sug[6][DICT_WORD];
	Dict common, all;
	Speller sp;
	MdState st;
	FILE *f;
	long n, at = 0, words = 0, wrong = 0;
	int len, k, i, checkReads = 0;

	if (argc != 3 || (gDictFile = fopen(argv[1], "rb")) == NULL || (f = fopen(argv[2], "rb")) == NULL)
		return 2;
	if (fread(head, 1, 16, gDictFile) != 16 || memcmp(head, "TPSP", 4) != 0)
		return 2;
	if (!Open(&common, 16, 1) || !Open(&all, 16 + (long) Get32(head + 8), 0))
		return 2;
	n = (long) fread(text, 1, sizeof(text), f);
	md_begin(&st);
	md_classify(&st, text, n, n, cls);
	sp_prepare(text, n, cls);
	sp.common = &common;
	sp.main = &all;
	sp.user = NULL;
	sp.ctx = NULL;
	sp.prefixes = NULL;
	if (head[5] >= 2) {
		static unsigned char prefixes[SP_PREFIXES];
		fseek(gDictFile, 16 + (long) Get32(head + 8) + (long) Get32(head + 12), SEEK_SET);
		if (fread(prefixes, 1, sizeof(prefixes), gDictFile) == sizeof(prefixes))
			sp.prefixes = prefixes;
	}
	while ((at = sp_next(text, n, at, cls, &len)) >= 0) {
		words++;
		if (!sp_correct(&sp, text + at, len)) {
			int before = gReads;
			wrong++;
			printf("%.*s:", len, text + at);
			k = sp_suggest(&sp, text + at, len, sug, 6);
			checkReads -= gReads - before;
			for (i = 0; i < k; i++)
				printf(" %s", sug[i]);
			printf("\n");
		}
		at += len;
	}
	checkReads += gReads;
	printf("%ld words, %ld not known; blocks read: %d checking, %d in all\n", words, wrong, checkReads, gReads);
	return 0;
}
