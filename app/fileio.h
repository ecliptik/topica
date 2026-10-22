/*
 * fileio.h: posts on disk, on a local disk or an AppleShare volume.
 * A file is named by a volume (or working directory) reference, a
 * directory ID (0 with a working directory) and a name, so Standard File's
 * replies, the Finder's AppFiles and System 7's FSSpecs all fit.
 */
#ifndef TOPICA_FILEIO_H
#define TOPICA_FILEIO_H

#include <Files.h>

#define kCreator 'TPCA'
#define kPostType 'TEXT'

typedef struct {
	short vRefNum;
	long dirID;
	Str63 name;
} FileRef;

void FileRefFromFSSpec(const FSSpec *s, FileRef *f);
/* f in the same folder as dir, named name (a C string) */
void FileRefSibling(const FileRef *dir, const char *name, FileRef *f);
Boolean FileRefSame(const FileRef *a, const FileRef *b);

/* Standard File: 1 when the writer chose a file */
Boolean FileAskOpen(FileRef *f);
Boolean FileAskSave(const char *prompt, const char *defaultName, FileRef *f);

/* Reads a whole file into a new pointer (the caller disposes of it) */
OSErr FileRead(const FileRef *f, Ptr *data, long *len);

/*
 * Writes a whole file safely: into a temporary file beside it first, then
 * in place of the old one, so a full disk or a dropped AppleShare link
 * leaves the old file whole. The file gets type and creator.
 */
OSErr FileWrite(const FileRef *f, const char *data, long len, OSType type, OSType creator);

OSErr FileDelete(const FileRef *f);

/* Where preferences go: the Preferences folder on System 7, the System
   Folder before it. f gets that folder, with name set to name. */
OSErr FilePrefsFolder(const char *name, FileRef *f);
/* A folder inside the System Folder (made if missing), as a FileRef
   whose name is empty: for recovery copies */
OSErr FileSystemSubfolder(const char *folder, FileRef *dir);
/* The index-th file (1-based) in a folder; fnfErr past the last */
OSErr FileIndexed(const FileRef *dir, short index, FileRef *f);

#endif
