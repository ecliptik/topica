/* fileio.c: see fileio.h. */
#include <Files.h>
#include <StandardFile.h>
#include <Memory.h>
#include <Events.h>
#include <Errors.h>
#include <string.h>
#include <stdio.h>

#include "fileio.h"
#include "util.h"

/* Shows text files, and files named like posts whatever their type (a
   file server may not know a .md file is text). TRUE hides a file. */
static pascal Boolean OpenFilter(CInfoPBPtr pb)
{
	const unsigned char *n = pb->hFileInfo.ioNamePtr;
	static const char *const kExt[] = { ".md", ".markdown", ".mdown", ".txt", NULL };
	int i;

	if (pb->hFileInfo.ioFlFndrInfo.fdType == kPostType)
		return false;
	if (n == NULL)
		return true;
	for (i = 0; kExt[i] != NULL; i++) {
		size_t len = strlen(kExt[i]), k;
		if (n[0] < len)
			continue;
		for (k = 0; k < len; k++) {
			char c = (char) n[n[0] - len + 1 + k];
			if (c >= 'A' && c <= 'Z')
				c = (char) (c + 32);
			if (c != kExt[i][k])
				break;
		}
		if (k == len)
			return false;
	}
	return true;
}

/* Where Standard File's dialogs go: near the top, centred */
static Point DialogSpot(short width)
{
	Point p;

	p.h = (short) ((qd.screenBits.bounds.right - width) / 2);
	p.v = 50;
	return p;
}

Boolean FileAskOpen(FileRef *f)
{
	SFReply r;

	SFGetFile(DialogSpot(348), "\p", (FileFilterUPP) OpenFilter, -1, NULL, NULL, &r);
	if (!r.good)
		return false;
	f->vRefNum = r.vRefNum;
	memcpy(f->name, r.fName, (size_t) r.fName[0] + 1);
	return true;
}

Boolean FileAskSave(const char *prompt, const char *defaultName, FileRef *f)
{
	SFReply r;
	Str255 p, n;

	PStr(p, prompt);
	PStr(n, defaultName);
	if (n[0] > 31)
		n[0] = 31;
	SFPutFile(DialogSpot(304), p, n, NULL, &r);
	if (!r.good)
		return false;
	f->vRefNum = r.vRefNum;
	memcpy(f->name, r.fName, (size_t) r.fName[0] + 1);
	return true;
}

OSErr FileRead(const FileRef *f, Ptr *data, long *len)
{
	short ref;
	OSErr err, cerr;
	long n;
	Ptr p;

	*data = NULL;
	*len = 0;
	err = FSOpen(f->name, f->vRefNum, &ref);
	if (err != noErr)
		return err;
	err = GetEOF(ref, &n);
	if (err == noErr) {
		p = NewPtr(n > 0 ? n : 1);
		if (p == NULL) {
			err = memFullErr;
		} else {
			*len = n;
			err = n > 0 ? FSRead(ref, len, p) : noErr;
			if (err == eofErr)
				err = noErr;
			if (err == noErr)
				*data = p;
			else
				DisposePtr(p);
		}
	}
	cerr = FSClose(ref);
	return err != noErr ? err : cerr;
}

static OSErr WriteNew(const unsigned char *name, short vRefNum, const char *data, long len)
{
	short ref;
	OSErr err, cerr;
	long n = len;

	err = Create(name, vRefNum, kCreator, kPostType);
	if (err != noErr)
		return err;
	err = FSOpen(name, vRefNum, &ref);
	if (err != noErr)
		return err;
	if (n > 0)
		err = FSWrite(ref, &n, data);
	if (err == noErr)
		err = SetEOF(ref, len);
	cerr = FSClose(ref);
	return err != noErr ? err : cerr;
}

/*
 * Finder info without Retro68's GetFInfo and SetFInfo glue: it leaves
 * ioFDirIndex unset, and a non-zero leftover makes the File Manager look up
 * the Nth file in the folder instead, write that file's name into ours, and
 * hand back (or change) its Finder info. On a file server that retyped
 * another application. Here the parameter block starts zeroed, and the File
 * Manager gets a copy of the name, never the caller's.
 */
static OSErr GetInfo(const unsigned char *name, short vRefNum, ParamBlockRec *pb, Str63 copy)
{
	memcpy(copy, name, (size_t) (name[0] > 63 ? 63 : name[0]) + 1);
	memset(pb, 0, sizeof(*pb));
	pb->fileParam.ioNamePtr = copy;
	pb->fileParam.ioVRefNum = vRefNum;
	pb->fileParam.ioFDirIndex = 0;
	return PBGetFInfoSync(pb);
}

static Boolean Exists(const unsigned char *name, short vRefNum)
{
	ParamBlockRec pb;
	Str63 copy;

	return GetInfo(name, vRefNum, &pb, copy) == noErr;
}

static OSErr SetTypeCreator(const unsigned char *name, short vRefNum, OSType type, OSType creator)
{
	ParamBlockRec pb;
	Str63 copy;
	OSErr err = GetInfo(name, vRefNum, &pb, copy);

	if (err != noErr)
		return err;
	pb.fileParam.ioFlFndrInfo.fdType = type;
	pb.fileParam.ioFlFndrInfo.fdCreator = creator;
	pb.fileParam.ioFDirIndex = 0;
	pb.fileParam.ioNamePtr = copy;
	return PBSetFInfoSync(&pb);
}

OSErr FileWrite(const FileRef *f, const char *data, long len)
{
	Str255 temp;
	char t[32];
	OSErr err;

	sprintf(t, "Topica Save %04ld", (long) (TickCount() % 10000));
	PStr(temp, t);
	(void) FSDelete(temp, f->vRefNum);          /* a left-over from a crash */
	err = WriteNew(temp, f->vRefNum, data, len);
	if (err != noErr) {
		(void) FSDelete(temp, f->vRefNum);
		return err;
	}
	if (Exists(f->name, f->vRefNum)) {
		err = FSDelete(f->name, f->vRefNum);
		if (err != noErr) {
			(void) FSDelete(temp, f->vRefNum);
			return err;
		}
	}
	err = Rename(temp, f->vRefNum, f->name);
	if (err != noErr)
		return err;                     /* the post is safe under the temporary name */
	(void) SetTypeCreator(f->name, f->vRefNum, kPostType, kCreator);
	(void) FlushVol(NULL, f->vRefNum);
	return noErr;
}
