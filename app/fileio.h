/*
 * fileio.h: posts on disk, on a local disk or an AppleShare volume.
 * A file is named by its directory (a working-directory or volume
 * reference, as Standard File and the Finder give it) and its name.
 */
#ifndef TOPICA_FILEIO_H
#define TOPICA_FILEIO_H

#include <Files.h>

#define kCreator 'TPCA'
#define kPostType 'TEXT'

typedef struct {
	short vRefNum;
	Str63 name;
} FileRef;

/* Standard File: 1 when the writer chose a file */
Boolean FileAskOpen(FileRef *f);
Boolean FileAskSave(const char *prompt, const char *defaultName, FileRef *f);

/* Reads a whole file into a new pointer (the caller disposes of it) */
OSErr FileRead(const FileRef *f, Ptr *data, long *len);

/*
 * Writes a whole file safely: into a temporary file beside it first, then
 * in place of the old one, so a full disk or a dropped AppleShare link
 * leaves the old file whole. The file becomes a Topica text file.
 */
OSErr FileWrite(const FileRef *f, const char *data, long len);

#endif
