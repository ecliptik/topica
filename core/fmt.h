/*
 * fmt.h: sprintf for Topica's own needs, a few hundred bytes where
 * newlib's is 20K on a 68000 (most of it the floating-point printing
 * Topica never uses). Conversions: d i u x X c s %, with l, a width (or *)
 * and the flags - and 0, and a precision for s (.N or .*). No floats.
 */
#ifndef TOPICA_FMT_H
#define TOPICA_FMT_H

#include <stdarg.h>

/* Writes the formatted text and a NUL into out; returns its length */
int tp_sprintf(char *out, const char *format, ...);
int tp_vsprintf(char *out, const char *format, va_list ap);

#endif
