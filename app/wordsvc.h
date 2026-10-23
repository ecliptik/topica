/*
 * wordsvc.h: Word Services (Working Software's Apple event suite, System
 * 7). Edit > Check Spelling With hands the post to a spelling checker of
 * the writer's choosing (Excalibur, Spellswell...), which reads and
 * corrects it through Apple events: Get Data and Set Data on the post, its
 * characters and ranges of them. Each correction is an undoable edit.
 */
#ifndef TOPICA_WORDSVC_H
#define TOPICA_WORDSVC_H

#include "docwin.h"

#ifndef TOPICA_LITE
/* Installs the handlers; false without Apple events (System 6) */
Boolean WordServicesInit(void);
/* Sends d to the checker; choose (or no checker yet) asks which one first */
void WordServicesCheck(Doc *d, Boolean choose);
/* The menu item's text: "Check Spelling with Excalibur", or "Check
   Spelling With..." before one is chosen */
void WordServicesItemText(Str255 s);
#else
#define WordServicesInit() false
#define WordServicesCheck(d, choose) ((void) 0)
#define WordServicesItemText(s) ((void) ((s)[0] = 0))
#endif

#endif
