/* In the place of RQ MOD's include/xsystem4.h: where the game's files are (the game the browser has
 * open) and a path for a message. */
#ifndef SYSTEM4_XSYSTEM4_H
#define SYSTEM4_XSYSTEM4_H

#include "system4/archive.h"

const char *display_utf0(const char *utf);
char *gamedir_path(const char *path);

#define MMAP_IF_64BIT (sizeof(void*) >= 8 ? ARCHIVE_MMAP : 0)

#endif /* SYSTEM4_XSYSTEM4_H */
