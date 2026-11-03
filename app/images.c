/*
 * images.c: see images.h.
 */
#include <Quickdraw.h>
#include <Multiverse.h>
#include <Memory.h>
#include <Files.h>
#include <Gestalt.h>
#include <string.h>

#include "../core/imgpath.h"
#include "fileio.h"
#include "images.h"
#include "util.h"

#ifndef TOPICA_LITE
/* The Component Manager and QuickTime 2.5's graphics importers, which
   Retro68's interfaces leave out: selectors from Inside Macintosh: More
   Macintosh Toolbox (_ComponentDispatch) and QuickTime's ImageCompression.h.
   A component call pushes its parameters' size and selector, then
   _ComponentDispatch with D0 = 0. */
typedef ComponentInstance GraphicsImportComponent;
pascal ComponentInstance OpenDefaultComponent(OSType type, OSType subType) M68K_INLINE(0x7021, 0xA82A);
pascal OSErr CloseComponent(ComponentInstance ci) M68K_INLINE(0x7008, 0xA82A);
pascal long GraphicsImportSetDataFile(GraphicsImportComponent ci, const FSSpec *file)
	M68K_INLINE(0x2F3C, 0x0004, 0x0003, 0x7000, 0xA82A);
pascal long GraphicsImportGetNaturalBounds(GraphicsImportComponent ci, Rect *bounds)
	M68K_INLINE(0x2F3C, 0x0004, 0x000E, 0x7000, 0xA82A);
pascal long GraphicsImportDraw(GraphicsImportComponent ci)
	M68K_INLINE(0x2F3C, 0x0000, 0x000F, 0x7000, 0xA82A);
pascal long GraphicsImportSetGWorld(GraphicsImportComponent ci, CGrafPtr port, GDHandle gd)
	M68K_INLINE(0x2F3C, 0x0008, 0x0010, 0x7000, 0xA82A);
pascal long GraphicsImportSetBoundsRect(GraphicsImportComponent ci, const Rect *bounds)
	M68K_INLINE(0x2F3C, 0x0004, 0x0014, 0x7000, 0xA82A);
#endif

enum { kMaxImages = 16, kMaxLevels = 8 };
enum { kReserve = 96L * 1024 };         /* memory a drawn picture leaves the editor */
enum { kMaxPict = 256L * 1024 };        /* the biggest PICT read without QuickTime */
enum { kUnknown, kMissing, kFound };

typedef struct {
	char url[256];                      /* the address as the post has it */
	short len;
	char state;
	FileRef file;
	OSType type;
	short w, h;                         /* its own size */
	GWorldPtr gw;                       /* drawn at the size shown (QuickTime) */
	Handle pict;                        /* the picture (PICT without QuickTime) */
	unsigned long used;
} Img;

static Img *gImg;
static Boolean gHaveBase;
static short gBaseVRef;                 /* the post's FileRef, as given */
static long gBaseDir;
static short gVRef;                     /* its folder: the volume and the folder's ID */
static long gDir;
static unsigned long gClock;
static Boolean gInverted;

#ifdef TOPICA_LITE
#define Importers() false               /* 512K Macs: no QuickTime */
#else
static Boolean Importers(void)
{
	static signed char have = -1;
	long v;

	if (have < 0) {
		v = 0;
		have = (signed char) (IsSystem7() && HasGWorlds() && Gestalt('qtim', &v) == noErr
				&& (unsigned long) v >= 0x02500000UL);
		DLOG(("Images: QuickTime %lx, importers %d", v, have));
	}
	return have;
}
#endif

static void Free(Img *e)
{
#ifndef TOPICA_LITE
	if (e->gw != NULL)
		DisposeGWorld(e->gw);
#endif
	if (e->pict != NULL)
		DisposeHandle(e->pict);
	e->gw = NULL;
	e->pict = NULL;
}

void ImgFlush(void)
{
	short i;

	if (gImg == NULL)
		return;
	for (i = 0; i < kMaxImages; i++) {
		Free(&gImg[i]);
		gImg[i].len = 0;
		gImg[i].state = kUnknown;
	}
}

void ImgSetBase(const FileRef *post)
{
	if (post == NULL) {
		if (gHaveBase)
			ImgFlush();
		gHaveBase = false;
		return;
	}
	if (gHaveBase && post->vRefNum == gBaseVRef && post->dirID == gBaseDir)
		return;
	ImgFlush();
	gBaseVRef = post->vRefNum;
	gBaseDir = post->dirID;
	gVRef = post->vRefNum;
	gDir = post->dirID;
	gHaveBase = FileRealFolder(&gVRef, &gDir) == noErr;
}

/* The entry for url, made if need be (in place of the least used) */
static Img *Lookup(const char *url, long n)
{
	short i, pick = -1;
	Img *e;

	if (n <= 0 || n >= (long) sizeof(gImg->url))
		return NULL;
	if (gImg == NULL && (gImg = (Img *) NewPtrClear(kMaxImages * sizeof(Img))) == NULL)
		return NULL;
	for (i = 0; i < kMaxImages; i++) {
		e = &gImg[i];
		if (e->len == n && memcmp(e->url, url, (size_t) n) == 0) {
			e->used = ++gClock;
			return e;
		}
		if (pick < 0 || e->len == 0 || (gImg[pick].len != 0 && e->used < gImg[pick].used))
			pick = i;
	}
	e = &gImg[pick];
	Free(e);
	memcpy(e->url, url, (size_t) n);
	e->url[n] = '\0';
	e->len = (short) n;
	e->state = kUnknown;
	e->used = ++gClock;
	return e;
}

#ifndef TOPICA_LITE
/* An importer for the file, its data set; NULL if there's none for it */
static GraphicsImportComponent Importer(Img *e)
{
	GraphicsImportComponent gi;
	FSSpec s;

	if (FSMakeFSSpec(e->file.vRefNum, e->file.dirID, e->file.name, &s) != noErr)
		return NULL;
	gi = OpenDefaultComponent('grip', e->type);
	if (gi != NULL && GraphicsImportSetDataFile(gi, &s) != noErr) {
		CloseComponent(gi);
		gi = NULL;
	}
	return gi;
}
#endif

/* Its size, from the importer or the PICT's frame; 0 if it can't be drawn */
static Boolean Measure(Img *e)
{
	Rect b;

#ifndef TOPICA_LITE
	if (Importers()) {
		GraphicsImportComponent gi = Importer(e);
		long err;
		if (gi == NULL)
			return false;
		err = GraphicsImportGetNaturalBounds(gi, &b);
		CloseComponent(gi);
		if (err != noErr)
			return false;
	} else
#endif
	if (e->type == 'PICT') {
		unsigned char f[10];
		short ref;
		long got = 0;
		if (FileOpenRead(&e->file, &ref) != noErr)
			return false;
		(void) FileReadAt(ref, 512, f, 10, &got);   /* after the 512-byte header: size, frame */
		FileClose(ref);
		if (got != 10)
			return false;
		SetRect(&b, (short) (f[4] << 8 | f[5]), (short) (f[2] << 8 | f[3]),
			(short) (f[8] << 8 | f[9]), (short) (f[6] << 8 | f[7]));
	} else {
		return false;
	}
	e->w = (short) (b.right - b.left);
	e->h = (short) (b.bottom - b.top);
	return e->w > 0 && e->h > 0;
}

/* Is the file at path from folder dir? Then it's e's, measured. */
static Boolean Try(Img *e, long dir, const char *path)
{
	OSType type;
	unsigned long ext;

	OSErr err = FileFind(gVRef, dir, path, &e->file, &type);

	DLOG(("Images: %s from %ld: %d", path, dir, err));
	if (err != noErr)
		return false;
	ext = ip_type(path);
	e->type = ext != 0 ? (OSType) ext : type;
	e->state = Measure(e) ? kFound : kMissing;
	DLOG(("Images: type %.4s, importers %d, %d x %d, state %d", (char *) &e->type, Importers(), e->w, e->h, e->state));
	return true;
}

static void Resolve(Img *e)
{
	char path[256], alt[272];
	long here, dir, id, parent;
	short level;
	int kind = ip_path(e->url, e->len, path, (int) sizeof(path));

	e->state = kMissing;
	DLOG(("Images: %s is %d; base %d (%d, %ld)", e->url, kind, gHaveBase, gVRef, gDir));
	if (!gHaveBase || kind == IP_NONE)
		return;
	if (FileFolder(gVRef, gDir, &here, &parent, NULL) != noErr)
		return;
	if (kind == IP_HERE) {
		if (Try(e, here, path))
			return;
	} else {
		/* the site's top is the post's folder or one above it (Jekyll's
		   _posts, Hugo's content); Hugo keeps pictures in static */
		for (dir = here, level = 0; level < kMaxLevels && dir > 1; level++) {
			if (Try(e, dir, path))
				return;
			strcpy(alt, ":static");
			strcat(alt, path);
			if (strlen(alt) <= 255 && Try(e, dir, alt))
				return;
			if (FileFolder(gVRef, dir, &id, &parent, NULL) != noErr)
				break;
			dir = parent;
		}
	}
	ip_leaf(path, alt, (int) sizeof(alt));
	(void) Try(e, here, alt);
}

Boolean ImgSize(const char *url, long n, short *w, short *h)
{
	Img *e = Lookup(url, n);

	if (e == NULL)
		return false;
	if (e->state == kUnknown)
		Resolve(e);
	if (e->state != kFound)
		return false;
	*w = e->w;
	*h = e->h;
	return true;
}

#ifndef TOPICA_LITE
/* The least used picture's drawing given back, other than keep's */
static Boolean FreeOne(Img *keep)
{
	short i, pick = -1;

	for (i = 0; i < kMaxImages; i++)
		if (&gImg[i] != keep && (gImg[i].gw != NULL || gImg[i].pict != NULL)
				&& (pick < 0 || gImg[i].used < gImg[pick].used))
			pick = i;
	if (pick < 0)
		return false;
	Free(&gImg[pick]);
	return true;
}

/* e drawn by QuickTime into an offscreen world size big */
static Boolean Render(Img *e, const Rect *size)
{
	GWorldPtr gw = NULL;
	GraphicsImportComponent gi;
	CGrafPtr oldPort;
	GDHandle oldDevice;
	PixMapHandle pm;
	long bytes, err;
	short depth = (**(**GetMainDevice()).gdPMap).pixelSize;

	bytes = ((long) (size->right * depth + 31) / 32) * 4 * size->bottom;
	for (;;) {
		/* temporary memory first, so the post keeps the application's */
		if (NewGWorld(&gw, 0, (Rect *) size, NULL, NULL, useTempMem) == noErr)
			break;
		gw = NULL;
		if (MaxBlock() > bytes + kReserve && NewGWorld(&gw, 0, (Rect *) size, NULL, NULL, 0) == noErr)
			break;
		gw = NULL;
		if (!FreeOne(e))
			return false;
	}
	gi = Importer(e);
	if (gi == NULL) {
		DisposeGWorld(gw);
		e->state = kMissing;
		return false;
	}
	pm = GetGWorldPixMap(gw);
	GetGWorld(&oldPort, &oldDevice);
	err = -1;
	if (LockPixels(pm)) {
		SetGWorld(gw, NULL);
		EraseRect(size);
		if (GraphicsImportSetGWorld(gi, gw, NULL) == noErr && GraphicsImportSetBoundsRect(gi, size) == noErr)
			err = GraphicsImportDraw(gi);
		SetGWorld(oldPort, oldDevice);
		UnlockPixels(pm);
	}
	CloseComponent(gi);
	if (err != noErr) {
		DisposeGWorld(gw);
		return false;
	}
	e->gw = gw;
	return true;
}

static Boolean DrawQT(Img *e, const Rect *r)
{
	Rect size;
	RGBColor fg, bg;
	PixMapHandle pm;
	Boolean drawn = false;

	SetRect(&size, 0, 0, (short) (r->right - r->left), (short) (r->bottom - r->top));
	if (e->gw != NULL && !EqualRect(&e->gw->portRect, &size)) {
		DisposeGWorld(e->gw);
		e->gw = NULL;
	}
	if (e->gw == NULL && !Render(e, &size))
		return false;
	pm = GetGWorldPixMap(e->gw);
	if (LockPixels(pm)) {
		/* black and white, or CopyBits colors it */
		GetForeColor(&fg);
		GetBackColor(&bg);
		ForeColor(blackColor);
		BackColor(whiteColor);
		CopyBits((BitMap *) *pm, &qd.thePort->portBits, &size, r, srcCopy, NULL);
		RGBForeColor(&fg);
		RGBBackColor(&bg);
		UnlockPixels(pm);
		drawn = true;
	}
	return drawn;
}
#endif

/* Without QuickTime: the PICT read in once, then drawn */
static Boolean DrawPict(Img *e, const Rect *r)
{
	if (e->pict == NULL) {
		short ref;
		long eof = 0, got = 0;
		Handle h;
		if (FileOpenRead(&e->file, &ref) != noErr)
			return false;
		if (GetEOF(ref, &eof) != noErr || eof <= 512 + 10 || eof - 512 > kMaxPict
				|| MaxBlock() < eof - 512 + kReserve || (h = NewHandle(eof - 512)) == NULL) {
			FileClose(ref);
			return false;
		}
		HLock(h);
		(void) FileReadAt(ref, 512, *h, eof - 512, &got);
		HUnlock(h);
		FileClose(ref);
		if (got != eof - 512) {
			DisposeHandle(h);
			return false;
		}
		e->pict = h;
	}
	DrawPicture((PicHandle) e->pict, r);
	return true;
}

void ImgSetInverted(Boolean inverted)
{
	gInverted = inverted;
}

Boolean ImgDraw(const char *url, long n, const Rect *r)
{
	Img *e = Lookup(url, n);
	Boolean drawn;

	if (e == NULL || r->right <= r->left || r->bottom <= r->top)
		return false;
	if (e->state == kUnknown)
		Resolve(e);
	if (e->state != kFound)
		return false;
#ifdef TOPICA_LITE
	drawn = DrawPict(e, r);
#else
	drawn = Importers() ? DrawQT(e, r) : DrawPict(e, r);
#endif
	if (drawn && gInverted)
		InvertRect(r);                  /* back the right way when the page is inverted */
	return drawn;
}
