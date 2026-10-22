/*
 * findwin.h: the Find dialog (Find, Replace, Replace All), Find Again, and
 * Replace & Find Again, over core/find.h.
 */
#ifndef TOPICA_FINDWIN_H
#define TOPICA_FINDWIN_H

#include "docwin.h"

void FindDialog(Doc *d);
void FindAgain(Doc *d);
void ReplaceAndFindAgain(Doc *d);
/* 1 when there is something to find again */
Boolean FindHasPattern(void);

#endif
