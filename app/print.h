/*
 * print.h: File › Page Setup, Print and Export HTML. Printing lays the
 * post out like the preview does, a page at a time, with page numbers;
 * Export HTML writes the post as a web page of its own (core/mdhtml).
 */
#ifndef TOPICA_PRINT_H
#define TOPICA_PRINT_H

#include "docwin.h"

void PrintPageSetup(void);
void PrintDoc(Doc *d);
void ExportHTML(Doc *d);

#endif
