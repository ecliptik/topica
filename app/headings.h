/*
 * headings.h: the Headings menu, the front post's headings to jump to. It
 * is rebuilt from the text each time the menu bar is clicked, with a check
 * mark on the heading the caret is under.
 */
#ifndef TOPICA_HEADINGS_H
#define TOPICA_HEADINGS_H

#include "docwin.h"

enum { kHeadingsMenu = 132 };

/* Fills the menu from d's headings (d NULL: empties it) */
void HeadingsMenu(Doc *d);
/* The writer chose a heading: the caret goes there */
void HeadingsGo(Doc *d, short item);

#endif
