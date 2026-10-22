/*
 * recovery.h: a recovery copy of an unsaved post. The first line says what
 * it is and the post's window title; the post's text follows as it was in
 * the editor (Mac Roman, CR).
 */
#ifndef TOPICA_RECOVERY_H
#define TOPICA_RECOVERY_H

/* The first line for a post titled title; its length, -1 if it doesn't fit */
long recovery_header(const char *title, char *out, long cap);

/* 1 when data is a recovery copy: the title into title (cut to cap) and
   where the text starts into *body */
int recovery_parse(const char *data, long n, char *title, int cap, long *body);

#endif
