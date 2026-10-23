/* wordsvc.c: see wordsvc.h. */
#include <AppleEvents.h>
#include <Processes.h>
#include <StandardFile.h>
#include <Files.h>
#include <Memory.h>
#include <Menus.h>
#include <TextUtils.h>
#include <stdio.h>
#include <string.h>

#include "../core/undo.h"
#include "editor.h"
#include "settings.h"
#include "util.h"
#include "wordsvc.h"

/* Apple's codes (AERegistry.h, AEObjects.h and Word Services'), which
   Retro68 lacks, under names of Topica's own */
enum {
	kWsClass = 'WSrv', kWsBatch = 'Btch', kWsStop = 'WStp',
	kCore = 'core', kGetData = 'getd', kSetData = 'setd', kCount = 'cnte',
	kObj = 'obj ', kRecord = 'reco', kList = 'list', kText = 'TEXT', kType = 'type', kEnum = 'enum',
	kLong = 'long', kShort = 'shor', kNull = 'null', kRange = 'rang', kCurrent = 'ccnt', kOrdinal = 'abso',
	kWant = 'want', kForm = 'form', kSeld = 'seld', kFrom = 'from', kStart = 'star', kStop = 'stop',
	kData = 'data', kObjClass = 'kocl', kDirect = '----',
	kFormIndex = 'indx', kFormName = 'name', kFormID = 'ID  ', kFormProp = 'prop', kFormRange = 'rang',
	kFirst = 'firs', kLast = 'last', kAll = 'all ',
	kAnyObject = 'cobj', kAppParams = 'appa', kSignature = 'sign',
	kChar = 'cha ', kTextClass = 'ctxt', kDocClass = 'docu', kWinClass = 'cwin', kPropClass = 'prop',
	kContents = 'pcnt', kHilite = 'pBgH',
	kNoSuchObject = -1728, kNotHandled = -1708,
	kNoFileFlags = 0x0800       /* launchNoFileFlags */
};

/* What an object specifier names: a range of a post's text, or one of its
   properties */
typedef struct {
	Doc *d;
	long start, end;
	OSType prop;                /* 0, or the property asked for */
} Target;

static Boolean gHave;
static long gChecking;          /* the post the checker has (DocId), for its "characters of Topica" */
static Boolean gJoin;           /* its corrections so far undo as one */

/* ---- the debug log: every event, in full ---- */

#ifdef TOPICA_DEBUG
static void Dump(const AEDesc *desc, int depth)
{
	AEDesc rec, item;
	AEKeyword key;
	long n, i;
	char line[160];
	int k;

	for (k = 0; k < depth * 2 && k < 40; k++)
		line[k] = ' ';
	line[k] = '\0';
	if (desc->descriptorType == kObj || desc->descriptorType == kRecord || desc->descriptorType == kList
			|| desc->descriptorType == kRange) {
		DebugLog("%s'%.4s' {", line, (char *) &desc->descriptorType);
		if (desc->descriptorType == kList)
			rec = *desc;
		else if (AECoerceDesc((AEDesc *) desc, kRecord, &rec) != noErr)
			return;
		if (AECountItems(&rec, &n) == noErr)
			for (i = 1; i <= n; i++)
				if (AEGetNthDesc(&rec, i, typeWildCard, &key, &item) == noErr) {
					DebugLog("%s  '%.4s':", line, (char *) &key);
					Dump(&item, depth + 2);
					(void) AEDisposeDesc(&item);
				}
		if (rec.dataHandle != desc->dataHandle)
			(void) AEDisposeDesc(&rec);
		DebugLog("%s}", line);
	} else {
		long size = desc->dataHandle != NULL ? GetHandleSize(desc->dataHandle) : 0;
		char data[48];
		long show = size < 40 ? size : 40;
		if (show > 0)
			memcpy(data, *desc->dataHandle, (size_t) show);
		data[show] = '\0';
		if (desc->descriptorType == kLong && size == 4)
			DebugLog("%s'long' %ld", line, *(long *) *desc->dataHandle);
		else if (desc->descriptorType == kShort && size == 2)
			DebugLog("%s'shor' %d", line, *(short *) *desc->dataHandle);
		else
			DebugLog("%s'%.4s' (%ld) %s", line, (char *) &desc->descriptorType, size, data);
	}
}

static void LogEvent(const AppleEvent *e)
{
	OSType cls = 0, id = 0;
	DescType type;
	Size size;
	AEDesc all;

	(void) AEGetAttributePtr((AppleEvent *) e, keyEventClassAttr, typeType, &type, &cls, sizeof(cls), &size);
	(void) AEGetAttributePtr((AppleEvent *) e, keyEventIDAttr, typeType, &type, &id, sizeof(id), &size);
	DebugLog("AE '%.4s'/'%.4s'", (char *) &cls, (char *) &id);
	if (AEGetParamDesc((AppleEvent *) e, kDirect, typeWildCard, &all) == noErr) {
		DebugLog(" direct:");
		Dump(&all, 1);
		(void) AEDisposeDesc(&all);
	}
	if (AEGetParamDesc((AppleEvent *) e, kData, typeWildCard, &all) == noErr) {
		DebugLog(" data:");
		Dump(&all, 1);
		(void) AEDisposeDesc(&all);
	}
}
#else
#define LogEvent(e) ((void) 0)
#endif

/* ---- object specifiers ---- */

static OSErr Key(const AEDesc *rec, OSType key, OSType type, AEDesc *out)
{
	return AEGetParamDesc((AppleEvent *) rec, key, type, out);
}

static Boolean KeyLong(const AEDesc *rec, OSType key, long *v)
{
	AEDesc d;
	Boolean ok = Key(rec, key, kLong, &d) == noErr;

	if (ok) {
		*v = *(long *) *d.dataHandle;
		(void) AEDisposeDesc(&d);
	}
	return ok;
}

static Boolean KeyType(const AEDesc *rec, OSType key, OSType *v)
{
	AEDesc d;
	Boolean ok = Key(rec, key, typeWildCard, &d) == noErr;

	if (ok) {
		ok = GetHandleSize(d.dataHandle) == 4;
		if (ok)
			*v = *(OSType *) *d.dataHandle;
		(void) AEDisposeDesc(&d);
	}
	return ok;
}

static OSErr Resolve(const AEDesc *spec, const Target *container, Target *t);

/* The nth character of the container (1-based; -1 is the last), or an ordinal */
static Boolean Position(const AEDesc *seld, const Target *c, long *at)
{
	long n = 0, len = c->end - c->start;
	OSType ord;

	if (seld->descriptorType == kOrdinal || seld->descriptorType == kEnum) {
		ord = *(OSType *) *seld->dataHandle;
		if (ord == kFirst)
			n = 1;
		else if (ord == kLast)
			n = len;
		else
			return false;
	} else {
		AEDesc l;
		if (AECoerceDesc((AEDesc *) seld, kLong, &l) != noErr)
			return false;
		n = *(long *) *l.dataHandle;
		(void) AEDisposeDesc(&l);
		if (n < 0)
			n = len + 1 + n;
	}
	if (n < 1 || n > len)
		return false;
	*at = c->start + n - 1;
	return true;
}

static Doc *DocByName(const AEDesc *seld)
{
	AEDesc s;
	Str255 title;
	short i;
	Doc *found = NULL;

	if (AECoerceDesc((AEDesc *) seld, kText, &s) != noErr)
		return NULL;
	for (i = 1; i <= DocCount() && found == NULL; i++) {
		Doc *d = DocNth(i);
		GetWTitle(DocWindow(d), title);
		if (title[0] == GetHandleSize(s.dataHandle) && memcmp(title + 1, *s.dataHandle, title[0]) == 0)
			found = d;
	}
	(void) AEDisposeDesc(&s);
	return found;
}

static OSErr Resolve(const AEDesc *spec, const Target *container, Target *t)
{
	AEDesc rec, from, seld;
	OSType want = 0, form = 0;
	Target c;
	OSErr err = kNoSuchObject;
	long n;

	if (spec->descriptorType == kCurrent && container != NULL) {   /* inside a range: its container */
		*t = *container;
		return noErr;
	}
	if (spec->descriptorType != kObj || AECoerceDesc((AEDesc *) spec, kRecord, &rec) != noErr)
		return kNoSuchObject;
	if (!KeyType(&rec, kWant, &want) || !KeyType(&rec, kForm, &form) || Key(&rec, kSeld, typeWildCard, &seld) != noErr) {
		(void) AEDisposeDesc(&rec);
		return kNoSuchObject;
	}
	memset(&c, 0, sizeof(c));
	if (Key(&rec, kFrom, typeWildCard, &from) == noErr) {
		if (from.descriptorType != kNull)
			err = Resolve(&from, container, &c);
		else
			err = noErr;                /* the application */
		(void) AEDisposeDesc(&from);
	}
	if (err == noErr && c.d == NULL && want != kDocClass && want != kWinClass
			&& (c.d = DocFromId(gChecking)) != NULL)
		c.end = EdLength(DocEditor(c.d));   /* text of the application: the post being checked */
	if (err == noErr) {
		err = kNoSuchObject;
		if ((want == kDocClass || want == kWinClass) && c.d == NULL) {
			Doc *d = NULL;
			if (form == kFormID && KeyLong(&rec, kSeld, &n))
				d = DocFromId(n);
			else if (form == kFormIndex && KeyLong(&rec, kSeld, &n))
				d = DocNth((short) n);
			else if (form == kFormName)
				d = DocByName(&seld);
			if (d != NULL) {
				t->d = d;
				t->start = 0;
				t->end = EdLength(DocEditor(d));
				t->prop = 0;
				err = noErr;
			}
		} else if (c.d != NULL && form == kFormProp) {
			*t = c;
			err = KeyType(&rec, kSeld, &t->prop) ? noErr : kNoSuchObject;
		} else if (c.d != NULL && (want == kChar || want == kTextClass || want == kObj || want == kAnyObject)) {
			long at;
			Boolean whole = want == kObj || want == kAnyObject;      /* object 1 of a post: all its text */
			if (whole && form == kFormIndex && KeyLong(&rec, kSeld, &n) && n == 1) {
				*t = c;
				err = noErr;
			} else if (whole) {
				;
			} else if (form == kFormIndex && seld.descriptorType == kEnum && *(OSType *) *seld.dataHandle == kAll) {
				*t = c;
				err = noErr;
			} else if (form == kFormIndex && Position(&seld, &c, &at)) {
				*t = c;
				t->start = at;
				t->end = at + 1;
				err = noErr;
			} else if (form == kFormRange) {
				AEDesc r, a, b;
				Target ta, tb;
				if (AECoerceDesc((AEDesc *) &seld, kRecord, &r) == noErr) {
					if (Key(&r, kStart, typeWildCard, &a) == noErr) {
						if (Key(&r, kStop, typeWildCard, &b) == noErr) {
							if (Resolve(&a, &c, &ta) == noErr && Resolve(&b, &c, &tb) == noErr
									&& ta.start <= tb.end) {
								*t = c;
								t->start = ta.start;
								t->end = tb.end;
								err = noErr;
							}
							(void) AEDisposeDesc(&b);
						}
						(void) AEDisposeDesc(&a);
					}
					(void) AEDisposeDesc(&r);
				}
			}
		}
	}
	(void) AEDisposeDesc(&seld);
	(void) AEDisposeDesc(&rec);
	return err;
}

/* ---- the events the checker sends ---- */

static pascal OSErr GetData(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	AEDesc spec;
	Target t;
	OSErr err;
	const char *text;

	(void) refcon;
	LogEvent(e);
	memset(&t, 0, sizeof(t));
	if (AEGetParamDesc((AppleEvent *) e, kDirect, typeWildCard, &spec) != noErr)
		return kNotHandled;
	err = Resolve(&spec, NULL, &t);
	(void) AEDisposeDesc(&spec);
	DLOG(("GetData: resolve %d: %ld-%ld prop '%.4s'", err, t.start, t.end, (char *) &t.prop));
	if (err != noErr)
		return err;
	if (t.prop != 0 && t.prop != kContents)
		return kNoSuchObject;
	text = EdLockText(DocEditor(t.d));
	err = AEPutParamPtr(reply, kDirect, kText, text + t.start, t.end - t.start);
	EdUnlockText(DocEditor(t.d));
	return err;
}

static pascal OSErr SetData(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	AEDesc spec, data;
	Target t;
	OSErr err;

	(void) refcon;
	(void) reply;
	LogEvent(e);
	memset(&t, 0, sizeof(t));
	if (AEGetParamDesc((AppleEvent *) e, kDirect, typeWildCard, &spec) != noErr)
		return kNotHandled;
	err = Resolve(&spec, NULL, &t);
	(void) AEDisposeDesc(&spec);
	DLOG(("SetData: resolve %d: %ld-%ld prop '%.4s'", err, t.start, t.end, (char *) &t.prop));
	if (err != noErr)
		return err;
	if (t.prop == kHilite) {                /* the word being checked, shown as the selection */
		Target w;
		if (AEGetParamDesc((AppleEvent *) e, kData, typeWildCard, &data) != noErr)
			return kNotHandled;
		if (Resolve(&data, &t, &w) == noErr && w.d == t.d) {
			EdSetSelection(DocEditor(w.d), w.start, w.end);
			DocShowLine(w.d, w.start);
		}
		(void) AEDisposeDesc(&data);      /* anything else (false): nothing to show */
		return noErr;
	}
	if ((t.prop != 0 && t.prop != kContents) || AEGetParamDesc((AppleEvent *) e, kData, kText, &data) != noErr)
		return kNotHandled;
	HLock(data.dataHandle);
	if (gJoin && DocId(t.d) == gChecking)
		DocJoinEdits(t.d, kUndoSpelling, true);
	if (!DocReplace(t.d, kUndoSpelling, t.start, t.end, *data.dataHandle, GetHandleSize(data.dataHandle)))
		err = kNotHandled;
	DocJoinEdits(t.d, kUndoSpelling, false);
	gJoin = DocId(t.d) == gChecking;
	(void) AEDisposeDesc(&data);
	return err;
}

#ifdef TOPICA_DEBUG
static pascal OSErr Other(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) reply;
	(void) refcon;
	LogEvent(e);
	return kNotHandled;
}
#endif

static pascal OSErr Stop(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) reply;
	(void) refcon;
	LogEvent(e);
	return noErr;
}

Boolean WordServicesInit(void)
{
	if (!IsSystem7())
		return false;
	gHave = AEInstallEventHandler(kCore, kGetData, NewAEEventHandlerUPP(GetData), 0, false) == noErr
			&& AEInstallEventHandler(kCore, kSetData, NewAEEventHandlerUPP(SetData), 0, false) == noErr;
	(void) AEInstallEventHandler(kWsClass, kWsStop, NewAEEventHandlerUPP(Stop), 0, false);
#ifdef TOPICA_DEBUG
	(void) AEInstallEventHandler(typeWildCard, typeWildCard, NewAEEventHandlerUPP(Other), 0, false);
#endif
	return gHave;
}

/* ---- the checker ---- */

/* A full path ("Disk:Folder:App") for the preferences */
static void FullPath(const FSSpec *s, char *out, long cap)
{
	char path[256], part[64];
	CInfoPBRec pb;
	Str63 name;
	long dir = s->parID;

	CStr(path, s->name);
	while (dir != 0 && dir != 1) {
		memset(&pb, 0, sizeof(pb));
		pb.dirInfo.ioNamePtr = name;
		pb.dirInfo.ioVRefNum = s->vRefNum;
		pb.dirInfo.ioFDirIndex = -1;
		pb.dirInfo.ioDrDirID = dir;
		if (PBGetCatInfoSync(&pb) != noErr)
			break;
		CStr(part, name);
		if (strlen(part) + 1 + strlen(path) >= sizeof(path))
			break;
		memmove(path + strlen(part) + 1, path, strlen(path) + 1);
		memcpy(path, part, strlen(part));
		path[strlen(part)] = ':';
		dir = pb.dirInfo.ioDrParID;
	}
	strncpy(out, path, (size_t) cap - 1);
	out[cap - 1] = '\0';
}

static Boolean Checker(FSSpec *s)
{
	Str255 p;

	if (gPrefs.wordService[0] == '\0')
		return false;
	PStr(p, gPrefs.wordService);
	return FSMakeFSSpec(0, 0, p, s) == noErr;
}

static Boolean Choose(FSSpec *s)
{
	StandardFileReply r;
	SFTypeList types;

	types[0] = 'APPL';
	StandardGetFile(NULL, 1, types, &r);
	if (!r.sfGood)
		return false;
	*s = r.sfFile;
	FullPath(s, gPrefs.wordService, sizeof(gPrefs.wordService));
	SettingsSave();
	return true;
}

/* The checker's process, if it's running */
static Boolean Running(const FSSpec *s, ProcessSerialNumber *psn)
{
	ProcessInfoRec info;
	FSSpec spec;

	psn->highLongOfPSN = 0;
	psn->lowLongOfPSN = kNoProcess;
	while (GetNextProcess(psn) == noErr) {
		memset(&info, 0, sizeof(info));
		info.processInfoLength = sizeof(info);
		info.processAppSpec = &spec;
		if (GetProcessInformation(psn, &info) == noErr && spec.vRefNum == s->vRefNum && spec.parID == s->parID
				&& EqualString(spec.name, s->name, false, false))
			return true;
	}
	return false;
}

/* Launches the checker. Its first event is Terminate Batch Check, which
   has nothing to stop: given Open Application it would ask for a file,
   and Excalibur drops a Batch Check that comes first. */
static OSErr Launch(const FSSpec *s, ProcessSerialNumber *psn)
{
	FInfo fi;
	AEDesc target, params;
	AppleEvent e;
	LaunchParamBlockRec lp;
	OSErr err;

	err = FSpGetFInfo((FSSpec *) s, &fi);
	if (err == noErr)
		err = AECreateDesc(kSignature, &fi.fdCreator, sizeof(fi.fdCreator), &target);
	if (err != noErr)
		return err;
	err = AECreateAppleEvent(kWsClass, kWsStop, &target, kAutoGenerateReturnID, kAnyTransactionID, &e);
	(void) AEDisposeDesc(&target);
	if (err != noErr)
		return err;
	err = AECoerceDesc(&e, kAppParams, &params);
	(void) AEDisposeDesc(&e);
	if (err != noErr)
		return err;
	HLock(params.dataHandle);
	memset(&lp, 0, sizeof(lp));
	lp.launchBlockID = extendedBlock;
	lp.launchEPBLength = extendedBlockLen;
	lp.launchControlFlags = launchContinue | kNoFileFlags;
	lp.launchAppSpec = (FSSpecPtr) s;
	lp.launchAppParameters = (AppParametersPtr) *params.dataHandle;
	err = LaunchApplication(&lp);
	*psn = lp.launchProcessSN;
	(void) AEDisposeDesc(&params);
	return err;
}

/* The post as an object specifier: document <its number> of Topica */
static OSErr PostSpec(Doc *d, AEDesc *spec)
{
	AEDesc rec;
	OSType want = kDocClass, form = kFormID;
	long id = DocId(d);
	OSErr err;

	err = AECreateList(NULL, 0, true, &rec);
	if (err != noErr)
		return err;
	err = AEPutParamPtr(&rec, kWant, kType, &want, sizeof(want));
	if (err == noErr)
		err = AEPutParamPtr(&rec, kFrom, kNull, NULL, 0);
	if (err == noErr)
		err = AEPutParamPtr(&rec, kForm, kEnum, &form, sizeof(form));
	if (err == noErr)
		err = AEPutParamPtr(&rec, kSeld, kLong, &id, sizeof(id));
	if (err == noErr)
		err = AECoerceDesc((AEDesc *) &rec, kObj, spec);
	(void) AEDisposeDesc(&rec);
	return err;
}

void WordServicesCheck(Doc *d, Boolean choose)
{
	FSSpec s;
	ProcessSerialNumber psn;
	AEDesc target, spec;
	AppleEvent e, reply;
	OSErr err;

	if (!gHave)
		return;
	if ((choose || !Checker(&s)) && !Choose(&s))
		return;
	err = Running(&s, &psn) ? noErr : Launch(&s, &psn);
	if (err == noErr)
		err = AECreateDesc(typeProcessSerialNumber, &psn, sizeof(psn), &target);
	if (err != noErr) {
		ShowNote("Topica couldn\xD5t start the spelling checker.");
		return;
	}
	err = AECreateAppleEvent(kWsClass, kWsBatch, &target, kAutoGenerateReturnID, kAnyTransactionID, &e);
	(void) AEDisposeDesc(&target);
	if (err == noErr) {
		gChecking = DocId(d);
		gJoin = false;
		if (PostSpec(d, &spec) == noErr) {
			(void) AEPutParamDesc(&e, kDirect, &spec);
			(void) AEDisposeDesc(&spec);
		}
		reply.descriptorType = kNull;
		reply.dataHandle = NULL;
		err = AESend(&e, &reply, kAENoReply | kAECanInteract, kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
		(void) AEDisposeDesc(&e);
	}
	if (err != noErr)
		ShowNote("The spelling checker didn\xD5t take the post.");
	else
		(void) SetFrontProcess(&psn);
}

void WordServicesItemText(Str255 s)
{
	const char *name = strrchr(gPrefs.wordService, ':');
	char text[96];

	if (name == NULL || name[1] == '\0')
		strcpy(text, "Check Spelling With\xC9");
	else
		sprintf(text, "Check Spelling with %.60s", name + 1);
	PStr(s, text);
}
