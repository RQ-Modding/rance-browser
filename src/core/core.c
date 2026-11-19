/* What the program gives the readers: the display's scale and where their messages go (core.h). */
#include <stdarg.h>
#include <stdio.h>

#include "core.h"

struct core_host CORE = { 1.0f, NULL };

void core_status(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	if (!CORE.status)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	CORE.status(buf);
}
