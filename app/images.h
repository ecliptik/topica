/*
 * images.h: a post's pictures, for the preview and printing, from files on
 * the Mac. An image's address is looked for from the post's folder: a
 * relative one there, one from the site's top (/assets/...) in each folder
 * above the post (and its static folder, for Hugo), then the file's name
 * beside the post. QuickTime 2.5 or later draws JPEG, GIF, PNG and the
 * rest; without it, PICT files only. Pictures are drawn once at the size
 * they're shown and kept.
 */
#ifndef TOPICA_IMAGES_H
#define TOPICA_IMAGES_H

#include <Quickdraw.h>
#include "fileio.h"

/* Where the post is that addresses are looked for from (NULL: an untitled
   post, nothing is found). Another post forgets what was found. */
void ImgSetBase(const FileRef *post);

/* The picture at url [0, n): its size, 0 when there's none Topica can draw */
Boolean ImgSize(const char *url, long n, short *w, short *h);

/* Draws it into r of the current port, scaled to fit; 0 when it couldn't */
Boolean ImgDraw(const char *url, long n, const Rect *r);

/* The page is drawn to be inverted (Dark without color): pictures are
   drawn inverted, to come out as they are */
void ImgSetInverted(Boolean inverted);

/* Forgets every picture (the memory back) */
void ImgFlush(void);

#endif
