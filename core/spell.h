/*
 * spell.h: checking a post's spelling. Which words to check (prose: not
 * code, addresses, Liquid, HTML, or front matter but its title and
 * description; not acronyms, words with digits or capitals inside),
 * whether a word is right, and what it might have been.
 *
 * A word is right in the dictionary as it's written, or lowercase when it
 * starts a sentence (a proper noun needs its capital), or any way when it's
 * all capitals; with its possessive 's, and with a curly apostrophe.
 */
#ifndef TOPICA_SPELL_H
#define TOPICA_SPELL_H

#include "dict.h"

/* The writer's own words, and the ones skipped for this check */
typedef int (*SpellUserFn)(void *ctx, const char *word, int len);

typedef struct {
	Dict *common;               /* the commonest words, checked first; may be NULL */
	Dict *main;                 /* all of them */
	SpellUserFn user;           /* may be NULL */
	void *ctx;
} Speller;

/* 1 when the word (len bytes, Mac Roman) is spelled right */
int sp_correct(const Speller *s, const char *word, int len);

/* Up to max words it might have been, best first, into out (each
   NUL-ended): how many */
int sp_suggest(const Speller *s, const char *word, int len, char out[][DICT_WORD], int max);

/* mdscan's classes, made ready for sp_next: the front matter's title and
   description count as text */
void sp_prepare(const char *text, long n, unsigned char *cls);

/* The next word to check that starts at or after from (a word from was in
   the middle of counts from its start): where it starts, and *len; -1
   when there are no more */
long sp_next(const char *text, long n, long from, const unsigned char *cls, int *len);

#endif
