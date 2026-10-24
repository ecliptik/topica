/* fmt.c: see fmt.h. */
#include <stdarg.h>
#include <string.h>

#include "fmt.h"

int tp_vsprintf(char *out, const char *f, va_list ap)
{
	char *o = out, digits[24];
	const char *s;
	int left, zero, width, prec, lng, len, n;
	unsigned long u;
	long v;

	for (; *f; f++) {
		if (*f != '%') {
			*o++ = *f;
			continue;
		}
		f++;
		left = zero = 0;
		for (;; f++) {
			if (*f == '-')
				left = 1;
			else if (*f == '0')
				zero = 1;
			else
				break;
		}
		width = 0;
		if (*f == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				left = 1;
				width = -width;
			}
			f++;
		} else {
			while (*f >= '0' && *f <= '9')
				width = width * 10 + (*f++ - '0');
		}
		prec = -1;
		if (*f == '.') {
			f++;
			prec = 0;
			if (*f == '*') {
				prec = va_arg(ap, int);
				f++;
			} else {
				while (*f >= '0' && *f <= '9')
					prec = prec * 10 + (*f++ - '0');
			}
		}
		lng = 0;
		if (*f == 'l') {
			lng = 1;
			f++;
		}
		/* the conversion's text: s points to len characters */
		len = 0;
		s = digits;
		switch (*f) {
		case 'd':
		case 'i':
			v = lng ? va_arg(ap, long) : (long) va_arg(ap, int);
			u = v < 0 ? (unsigned long) -(v + 1) + 1 : (unsigned long) v;
			n = (int) sizeof(digits);
			do
				digits[--n] = (char) ('0' + u % 10);
			while ((u /= 10) > 0);
			if (v < 0) {
				if (zero && !left) {
					*o++ = '-';         /* the sign before the zeros */
					if (width > 0)
						width--;
				} else {
					digits[--n] = '-';
				}
			}
			s = digits + n;
			len = (int) sizeof(digits) - n;
			break;
		case 'u':
		case 'x':
		case 'X':
			u = lng ? va_arg(ap, unsigned long) : (unsigned long) va_arg(ap, unsigned);
			n = (int) sizeof(digits);
			if (*f == 'u') {
				do
					digits[--n] = (char) ('0' + u % 10);
				while ((u /= 10) > 0);
			} else {
				const char *hex = *f == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
				do
					digits[--n] = hex[u & 15];
				while ((u >>= 4) > 0);
			}
			s = digits + n;
			len = (int) sizeof(digits) - n;
			break;
		case 'c':
			digits[0] = (char) va_arg(ap, int);
			len = 1;
			break;
		case 's':
			s = va_arg(ap, const char *);
			if (s == NULL)
				s = "(null)";
			if (prec < 0) {
				len = (int) strlen(s);
			} else {
				for (len = 0; len < prec && s[len]; len++)
					;
			}
			zero = 0;
			break;
		case '%':
			digits[0] = '%';
			len = 1;
			zero = 0;
			break;
		default:                        /* not one of ours: shown as it is */
			*o++ = '%';
			if (*f == '\0')
				f--;
			else
				*o++ = *f;
			continue;
		}
		if (!left)
			for (; width > len; width--)
				*o++ = zero ? '0' : ' ';
		memcpy(o, s, (size_t) len);
		o += len;
		for (; width > len; width--)
			*o++ = ' ';
	}
	*o = '\0';
	return (int) (o - out);
}

int tp_sprintf(char *out, const char *format, ...)
{
	va_list ap;
	int n;

	va_start(ap, format);
	n = tp_vsprintf(out, format, ap);
	va_end(ap);
	return n;
}
