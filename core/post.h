/*
 * post.h: a blog post's name, its template, and its words.
 * Text is Mac Roman (accented letters fold to their base letters in a slug).
 */
#ifndef TOPICA_POST_H
#define TOPICA_POST_H

/* "Hello, Wörld!" -> "hello-world": lowercase ASCII letters and digits
   joined by single hyphens, at most cap - 1 bytes, cut at a hyphen when it
   has to be cut. */
void post_slug(const char *title, char *out, int cap);

/*
 * The post's file name: "2026-10-22-hello-world.md". A Macintosh file name
 * holds 31 characters, so maxLen 31 cuts the slug to fit (at a hyphen when
 * it can); 0 means no limit. An empty title gives "2026-10-22-untitled.md".
 */
void post_filename(int year, int month, int day, const char *title, int maxLen, char *out, int cap);

/* A new post's text: front matter with the usual keys, then a blank line.
   Jekyll: layout (given), title, description, category, tags. Hugo: title,
   date (given; left empty when ""), draft: true, description, categories,
   tags. Returns its length (-1 when it doesn't fit). */
long post_template(int hugo, const char *layout, const char *date, char eol, char *out, long cap);

/* Words in text, front matter left out. */
long post_words(const char *text, long n);

/* Minutes to read words at 200 a minute, at least 1 when there are any. */
long post_minutes(long words);

#endif
