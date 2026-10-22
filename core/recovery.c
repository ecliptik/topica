/* recovery.c: see recovery.h. */
#include <string.h>

#include "recovery.h"

static const char kMagic[] = "Topica recovery: ";

long recovery_header(const char *title, char *out, long cap)
{
	long m = (long) sizeof(kMagic) - 1, t = 0, n;

	while (title[t] != '\0' && title[t] != '\r' && title[t] != '\n' && t < 255)
		t++;
	n = m + t + 1;
	if (out == NULL)
		return n;
	if (n > cap)
		return -1;
	memcpy(out, kMagic, (size_t) m);
	memcpy(out + m, title, (size_t) t);
	out[m + t] = '\r';
	return n;
}

int recovery_parse(const char *data, long n, char *title, int cap, long *body)
{
	long m = (long) sizeof(kMagic) - 1, i;
	int k = 0;

	if (n < m || memcmp(data, kMagic, (size_t) m) != 0)
		return 0;
	for (i = m; i < n && data[i] != '\r'; i++)
		if (k < cap - 1)
			title[k++] = data[i];
	if (cap > 0)
		title[k] = '\0';
	if (i == n)
		return 0;
	*body = i + 1;
	return 1;
}
