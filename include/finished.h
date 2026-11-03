// The list of books that have been read to the end (docs: decision 086). A plain text file, one book path
// per line, kept in memory as text so that nothing needs to be allocated. The functions that work on the
// text have no DS code and are tested on a PC.
#ifndef DSLEAF_FINISHED_H
#define DSLEAF_FINISHED_H

#include <stdbool.h>
#include <stddef.h>

#define FINISHED_PATH "/DSLeaf/finished.txt"
#define FINISHED_CAP 16384 // bytes of text kept: about 400 books; the oldest are dropped first

// Reads the file into `text` (NUL ended). Returns the length, 0 if there is no file.
int finished_load(char *text, size_t cap);
bool finished_save(const char *text);

// Whether `path` is in the list.
bool finished_has(const char *text, const char *path);

// Adds a path at the end (if it is not there yet), dropping the oldest lines when it does not fit.
// Returns false if the path cannot be stored (empty, a line break in it, too long).
bool finished_add(char *text, size_t cap, const char *path);

// Takes a path out. Returns true if it was there.
bool finished_remove(char *text, const char *path);

#endif
