/*
 * ae.h: System 7's required Apple events: open application, open and print
 * documents, quit. On System 6 there are none, and main.c reads the
 * Finder's AppFiles instead.
 */
#ifndef TOPICA_AE_H
#define TOPICA_AE_H

#include <Events.h>

#include "fileio.h"

/* 1 when the Apple Event Manager is there and the handlers are in.
   quit returns 0 when the writer cancelled. */
Boolean AppleEventsInit(Boolean (*quit)(void), void (*openApp)(void), void (*openFile)(const FileRef *f));
void AppleEventsHandle(EventRecord *e);

#endif
