/*
 * print.c: Page Setup, Print and Export HTML (see print.h).
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Memory.h>
#include <Multiverse.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

#include "../core/mdhtml.h"
#include "../core/textconv.h"
#include "editor.h"
#include "fileio.h"
#include "page.h"
#include "print.h"
#include "util.h"

enum { kMargin = 36, kFooter = 18 };

static THPrint gPrint;

/* The print record, made once and kept for the session */
static Boolean PrintRecord(void)
{
	if (gPrint == NULL) {
		gPrint = (THPrint) NewHandleClear(sizeof(TPrint));
		if (gPrint == NULL)
			return false;
		PrintDefault(gPrint);
	}
	return true;
}

/* Opens the Printing Manager; an alert when it can't */
static Boolean OpenPrinting(void)
{
	PrOpen();
	if (PrError() != noErr) {
		PrClose();
		ShowNote("Topica can\xD5t print: choose a printer in the Chooser first.");
		return false;
	}
	if (!PrintRecord()) {
		PrClose();
		ErrorAlert("The post couldn\xD5t be printed.", memFullErr);
		return false;
	}
	return true;
}

void PrintPageSetup(void)
{
	if (!OpenPrinting())
		return;
	(void) PrStlDialog(gPrint);
	PrClose();
}

static void Footer(const Rect *r, short number)
{
	char s[16];
	short w;

	sprintf(s, "%d", number);
	TextFont(kGeneva);
	TextSize(9);
	TextFace(0);
	w = TextWidth(s, 0, (short) strlen(s));
	MoveTo((short) ((r->left + r->right - w) / 2), (short) (r->bottom - 4));
	DrawText(s, 0, (short) strlen(s));
}

void PrintDoc(Doc *d)
{
	Editor *ed = DocEditor(d);
	TPPrPort port;
	TPrStatus status;
	Page *pg;
	GrafPtr save;
	Rect r, body, clip;
	const char *text;
	long n, top, bottom, height, shift;
	Boolean redraw;
	short number = 1;
	OSErr err;

	if (!OpenPrinting())
		return;
	(void) PrValidate(gPrint);
	if (!PrJobDialog(gPrint)) {
		PrClose();
		return;
	}
	pg = PageNew();
	if (pg == NULL) {
		PrClose();
		ErrorAlert("The post couldn\xD5t be printed.", memFullErr);
		return;
	}
	GetPort(&save);
	SetCursor(*GetCursor(watchCursor));
	text = EdLockText(ed);
	n = EdLength(ed);
	port = PrOpenDoc(gPrint, NULL, NULL);
	if (PrError() == noErr) {
		SetPort(&port->gPort);
		r = (**gPrint).prInfo.rPage;
		InsetRect(&r, kMargin, kMargin / 2);
		body = r;
		body.bottom -= kFooter;
		if (PageLayout(pg, text, n, (short) (r.right - r.left), 0, n, 0)
				&& PageRefine(pg, text, n, 0, 0, -1, &shift, &redraw)) {
			height = PageHeight(pg);
			for (top = 0; top < height && PrError() == noErr; top = bottom, number++) {
				bottom = PageBreak(pg, text, n, top, top + (body.bottom - body.top));
				PrOpenPage(port, NULL);
				if (PrError() == noErr) {
					clip = body;
					clip.bottom = (short) (body.top + (bottom - top));
					ClipRect(&clip);
					PageDraw(pg, text, n, top, &clip, false);
					ClipRect(&r);
					Footer(&r, number);
				}
				PrClosePage(port);
			}
		} else {
			PrSetError(memFullErr);
		}
	}
	PrCloseDoc(port);
	if (PrError() == noErr && (**gPrint).prJob.bJDocLoop == bSpoolLoop)
		PrPicFile(gPrint, NULL, NULL, NULL, &status);
	err = PrError();
	PrClose();
	EdUnlockText(ed);
	PageDispose(pg);
	SetPort(save);
	InitCursor();
	if (err != noErr && err != iPrAbort)
		ErrorAlert("The post couldn\xD5t be printed.", err);
}

/* ---- Export HTML ---- */

/* The window's name with .html for .md: "post.md" is "post.html" */
static void HtmlName(Doc *d, char *out)
{
	Str255 title;
	char *dot;

	GetWTitle(DocWindow(d), title);
	CStr(out, title);
	dot = strrchr(out, '.');
	if (dot != NULL && (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0 || strcmp(dot, ".txt") == 0))
		*dot = '\0';
	out[26] = '\0';
	strcat(out, ".html");
}

void ExportHTML(Doc *d)
{
	Editor *ed = DocEditor(d);
	FileRef f;
	char name[64];
	const char *text;
	unsigned char *cls;
	Ptr html = NULL, out = NULL;
	long n = EdLength(ed), len, size = 0;
	OSErr err = memFullErr;

	HtmlName(d, name);
	if (!FileAskSave("Export the post as HTML:", name, &f))
		return;
	SetCursor(*GetCursor(watchCursor));
	cls = (unsigned char *) NewPtr(n > 0 ? n : 1);
	if (cls != NULL) {
		text = EdLockText(ed);
		len = mh_page(text, n, cls, NULL, 0);
		html = NewPtr(len);
		if (html != NULL)
			(void) mh_page(text, n, cls, html, len);
		EdUnlockText(ed);
		DisposePtr((Ptr) cls);
	}
	if (html != NULL) {
		/* UTF-8 with LF line ends, as the page's charset says */
		size = tc_to_disk(html, len, NULL, 0, 1, 1);
		out = NewPtr(size > 0 ? size : 1);
		if (out != NULL)
			(void) tc_to_disk(html, len, out, size, 1, 1);
		DisposePtr(html);
	}
	if (out != NULL) {
		err = FileWrite(&f, out, size, 'TEXT', 'ttxt');
		DisposePtr(out);
	}
	InitCursor();
	if (err != noErr) {
		char what[128];
		CStr(name, f.name);
		sprintf(what, "\xD2%s\xD3 couldn\xD5t be saved.", name);
		ErrorAlert(what, err);
	}
}
