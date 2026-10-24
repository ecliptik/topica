/*
 * fileio.c: see fileio.h.
 *
 * Every File Manager call here builds its own parameter block, zeroed
 * first, and hands the File Manager a copy of the name. Retro68's glue
 * leaves fields unset: GetFInfo's ioFDirIndex (a leftover makes it look up
 * another file and write that file's name into ours; on a file server that
 * retyped another application) and HOpen's ioMisc (taken as a buffer).
 */
#include <Files.h>
#include <StandardFile.h>
#include <Memory.h>
#include <Events.h>
#include <Errors.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <Traps.h>
#include <string.h>
#include <stdio.h>

#include "fileio.h"
#include "util.h"

#ifndef kOnSystemDisk
#define kOnSystemDisk ((short) 0x8000)
#endif
#ifndef kPreferencesFolderType
#define kPreferencesFolderType 'pref'
#endif
enum { kDirectoryBit = 0x10 };

static void CopyName(Str63 dst, const unsigned char *src)
{
	memcpy(dst, src, (size_t) (src[0] > 63 ? 63 : src[0]) + 1);
	if (dst[0] > 63)
		dst[0] = 63;
}

void FileRefFromFSSpec(const FSSpec *s, FileRef *f)
{
	f->vRefNum = s->vRefNum;
	f->dirID = s->parID;
	CopyName(f->name, s->name);
}

void FileRefSibling(const FileRef *dir, const char *name, FileRef *f)
{
	Str255 p;

	f->vRefNum = dir->vRefNum;
	f->dirID = dir->dirID;
	PStr(p, name);
	CopyName(f->name, p);
}

Boolean FileRefSame(const FileRef *a, const FileRef *b)
{
	return a->vRefNum == b->vRefNum && a->dirID == b->dirID && EqualString(a->name, b->name, false, true);
}

/* A zeroed parameter block for f, with a copy of its name */
static void Block(HParamBlockRec *pb, const FileRef *f, Str63 copy)
{
	CopyName(copy, f->name);
	memset(pb, 0, sizeof(*pb));
	pb->fileParam.ioNamePtr = copy;
	pb->fileParam.ioVRefNum = f->vRefNum;
	pb->fileParam.ioDirID = f->dirID;
}

static OSErr OpenFile(const FileRef *f, SignedByte perm, short *ref)
{
	HParamBlockRec pb;
	Str63 copy;
	OSErr err;

	Block(&pb, f, copy);
	pb.ioParam.ioPermssn = perm;
	pb.ioParam.ioMisc = NULL;
	err = PBHOpenDFSync(&pb);
	if (err == paramErr) {              /* no OpenDF before System 7 */
		Block(&pb, f, copy);
		pb.ioParam.ioPermssn = perm;
		err = PBHOpenSync(&pb);
	}
	*ref = pb.ioParam.ioRefNum;
	return err;
}

static OSErr GetInfo(const FileRef *f, HParamBlockRec *pb, Str63 copy)
{
	Block(pb, f, copy);
	pb->fileParam.ioFDirIndex = 0;
	return PBHGetFInfoSync(pb);
}

Boolean FileIsStationery(const FileRef *f)
{
	HParamBlockRec pb;
	Str63 copy;

	return GetInfo(f, &pb, copy) == noErr && (pb.fileParam.ioFlFndrInfo.fdFlags & 0x0800) != 0;
}

static Boolean Exists(const FileRef *f)
{
	HParamBlockRec pb;
	Str63 copy;

	return GetInfo(f, &pb, copy) == noErr;
}

static OSErr SetTypeCreator(const FileRef *f, OSType type, OSType creator)
{
	HParamBlockRec pb;
	Str63 copy;
	OSErr err = GetInfo(f, &pb, copy);

	if (err != noErr)
		return err;
	pb.fileParam.ioFlFndrInfo.fdType = type;
	pb.fileParam.ioFlFndrInfo.fdCreator = creator;
	pb.fileParam.ioFDirIndex = 0;
	pb.fileParam.ioNamePtr = copy;
	pb.fileParam.ioDirID = f->dirID;    /* PBHGetFInfo put the file number there */
	return PBHSetFInfoSync(&pb);
}

static OSErr CreateFile(const FileRef *f, OSType type, OSType creator)
{
	HParamBlockRec pb;
	Str63 copy;
	OSErr err;

	Block(&pb, f, copy);
	err = PBHCreateSync(&pb);
	if (err != noErr)
		return err;
	return SetTypeCreator(f, type, creator);
}

OSErr FileDelete(const FileRef *f)
{
	HParamBlockRec pb;
	Str63 copy;

	Block(&pb, f, copy);
	return PBHDeleteSync(&pb);
}

static OSErr RenameTo(const FileRef *f, const unsigned char *newName)
{
	HParamBlockRec pb;
	Str63 copy, to;

	Block(&pb, f, copy);
	CopyName(to, newName);
	pb.ioParam.ioMisc = (Ptr) to;
	return PBHRenameSync(&pb);
}

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
	f->vRefNum = r.vRefNum;             /* a working directory */
	f->dirID = 0;
	CopyName(f->name, r.fName);
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
	f->dirID = 0;
	CopyName(f->name, r.fName);
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
	err = OpenFile(f, fsRdPerm, &ref);
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

static FileRef gAppFolder;

void FileRememberAppFolder(void)
{
	memset(&gAppFolder, 0, sizeof(gAppFolder));
	(void) GetVol(NULL, &gAppFolder.vRefNum);   /* at launch: the application's folder */
}

void FileBesideApp(const char *name, FileRef *f)
{
	FileRefSibling(&gAppFolder, name, f);
}

OSErr FileOpenRead(const FileRef *f, short *ref)
{
	return OpenFile(f, fsRdPerm, ref);
}

OSErr FileReadAt(short ref, long offset, void *buf, long len, long *got)
{
	OSErr err = SetFPos(ref, fsFromStart, offset);

	*got = len;
	if (err == noErr)
		err = FSRead(ref, got, buf);
	return err == eofErr ? noErr : err;
}

void FileClose(short ref)
{
	(void) FSClose(ref);
}

/* Files.h's FIDParam and GetVolParms's buffer, which Retro68 lacks */
typedef struct {
	QElemPtr qLink;
	short qType, ioTrap;
	Ptr ioCmdAddr;
	void *ioCompletion;
	OSErr ioResult;
	StringPtr ioNamePtr;
	short ioVRefNum;
	long filler14;
	StringPtr ioDestNamePtr;
	long filler15;
	long ioDestDirID;
	long filler16, filler17;
	long ioSrcDirID;
	short filler18;
	long ioFileID;
} FIDBlock;

typedef struct {
	short vMVersion;
	long vMAttrib;
	Handle vMLocalHand;
	long vMServerAdr;
	long vMVolumeGrade;
	short vMForeignPrivID;
} VolParms;

enum { kHasFileIDs = 6 };               /* vMAttrib (bHasFileIDs): the volume can exchange files */

static Boolean CanExchange(short vRefNum)
{
	HParamBlockRec pb;
	VolParms parms;

	memset(&pb, 0, sizeof(pb));
	memset(&parms, 0, sizeof(parms));
	pb.ioParam.ioVRefNum = vRefNum;
	pb.ioParam.ioBuffer = (Ptr) &parms;
	pb.ioParam.ioReqCount = sizeof(parms);
	return PBHGetVolParmsSync(&pb) == noErr && (parms.vMAttrib & (1L << kHasFileIDs)) != 0;
}

/* a and b trade contents; each keeps its name, Finder information,
   comments and creation date */
static OSErr Exchange(const FileRef *a, const FileRef *b)
{
	union {
		ParamBlockRec pb;
		FIDBlock fid;
	} u;
	Str63 an, bn;

	CopyName(an, a->name);
	CopyName(bn, b->name);
	memset(&u, 0, sizeof(u));
	u.fid.ioNamePtr = an;
	u.fid.ioVRefNum = a->vRefNum;
	u.fid.ioSrcDirID = a->dirID;
	u.fid.ioDestNamePtr = bn;
	u.fid.ioDestDirID = b->dirID;
	return PBExchangeFilesSync(&u.pb);
}

static OSErr WriteNew(const FileRef *f, const char *data, long len, OSType type, OSType creator)
{
	short ref;
	OSErr err, cerr;
	long n = len;

	err = CreateFile(f, type, creator);
	if (err != noErr)
		return err;
	err = OpenFile(f, fsRdWrPerm, &ref);
	if (err != noErr)
		return err;
	if (n > 0)
		err = FSWrite(ref, &n, data);
	if (err == noErr)
		err = SetEOF(ref, len);
	cerr = FSClose(ref);
	return err != noErr ? err : cerr;
}

OSErr FileWrite(const FileRef *f, const char *data, long len, OSType type, OSType creator)
{
	FileRef temp;
	char t[32];
	OSErr err;

	sprintf(t, "Topica Save %04ld", (long) (TickCount() % 10000));
	FileRefSibling(f, t, &temp);
	(void) FileDelete(&temp);               /* a left-over from a crash */
	err = WriteNew(&temp, data, len, type, creator);
	if (err != noErr) {
		(void) FileDelete(&temp);
		return err;
	}
	/* swapped in where the volume can (System 7's HFS), so the post keeps
	   its Finder comments and dates; else the old one goes, the new is renamed */
	if (Exists(f) && f->vRefNum == temp.vRefNum && CanExchange(f->vRefNum)
			&& Exchange(&temp, f) == noErr) {
		(void) FileDelete(&temp);           /* now the old post */
		(void) FlushVol(NULL, f->vRefNum);
		return noErr;
	}
	if (Exists(f)) {
		err = FileDelete(f);
		if (err != noErr) {
			(void) FileDelete(&temp);
			return err;
		}
	}
	err = RenameTo(&temp, f->name);
	if (err != noErr)
		return err;                         /* the post is safe under the temporary name */
	(void) FlushVol(NULL, f->vRefNum);
	return noErr;
}

/* The System Folder: a working directory from SysEnvirons */
static short SystemFolder(void)
{
	SysEnvRec env;

	if (SysEnvirons(1, &env) != noErr)
		return 0;
	return env.sysVRefNum;
}

OSErr FilePrefsFolder(const char *name, FileRef *f)
{
	FileRef dir;
	long attr;

	dir.vRefNum = SystemFolder();
	dir.dirID = 0;
	dir.name[0] = 0;
	if (NGetTrapAddress(_Gestalt, kToolboxTrapType) != NGetTrapAddress(_Unimplemented, kToolboxTrapType)
			&& Gestalt(gestaltFindFolderAttr, &attr) == noErr && (attr & 1)) {
		short v;
		long d;
		if (FindFolder(kOnSystemDisk, kPreferencesFolderType, true, &v, &d) == noErr) {
			dir.vRefNum = v;
			dir.dirID = d;
		}
	}
	FileRefSibling(&dir, name, f);
	return noErr;
}

OSErr FileSystemSubfolder(const char *folder, FileRef *dir)
{
	CInfoPBRec cpb;
	HParamBlockRec hpb;
	Str255 p;
	Str63 name;
	short sys = SystemFolder();
	OSErr err;

	PStr(p, folder);
	CopyName(name, p);
	memset(&cpb, 0, sizeof(cpb));
	cpb.dirInfo.ioNamePtr = name;
	cpb.dirInfo.ioVRefNum = sys;
	cpb.dirInfo.ioDrDirID = 0;
	cpb.dirInfo.ioFDirIndex = 0;
	err = PBGetCatInfoSync(&cpb);
	dir->vRefNum = sys;
	dir->name[0] = 0;
	if (err == noErr) {
		if (!(cpb.dirInfo.ioFlAttrib & kDirectoryBit))
			return dupFNErr;            /* a file has the folder's name */
		dir->dirID = cpb.dirInfo.ioDrDirID;
		return noErr;
	}
	if (err != fnfErr)
		return err;
	CopyName(name, p);
	memset(&hpb, 0, sizeof(hpb));
	hpb.fileParam.ioNamePtr = name;
	hpb.fileParam.ioVRefNum = sys;
	hpb.fileParam.ioDirID = 0;
	err = PBDirCreateSync(&hpb);
	if (err == noErr)
		dir->dirID = hpb.fileParam.ioDirID;
	return err;
}

OSErr FileIndexed(const FileRef *dir, short index, FileRef *f)
{
	CInfoPBRec pb;
	Str63 name;
	OSErr err;

	memset(&pb, 0, sizeof(pb));
	name[0] = 0;
	pb.hFileInfo.ioNamePtr = name;
	pb.hFileInfo.ioVRefNum = dir->vRefNum;
	pb.hFileInfo.ioDirID = dir->dirID;
	pb.hFileInfo.ioFDirIndex = index;
	err = PBGetCatInfoSync(&pb);
	if (err != noErr)
		return err;
	f->vRefNum = dir->vRefNum;
	f->dirID = dir->dirID;
	CopyName(f->name, name);
	return (pb.hFileInfo.ioFlAttrib & kDirectoryBit) ? dirNFErr : noErr;
}
