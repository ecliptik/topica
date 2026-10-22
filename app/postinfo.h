/*
 * postinfo.h: the Post menu's dialogs. Post Info edits the front matter's
 * usual fields; Insert Image writes a Markdown image placeholder.
 */
#ifndef TOPICA_POSTINFO_H
#define TOPICA_POSTINFO_H

#include "docwin.h"

/* 1 when the writer clicked OK (and the front matter changed if needed) */
Boolean PostInfo(Doc *d);
void InsertImage(Doc *d);

#endif
