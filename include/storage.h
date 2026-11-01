// SD card access and safe text file I/O (docs: storage.md, decisions 020, 035).
//
// All app files are under /DSLeaf and books are under /Books. Paths passed to
// these functions start with "/". On the DS they resolve on the SD card. On a
// PC (for tests) they resolve below a root directory set with storage_set_root.
#ifndef DSLEAF_STORAGE_H
#define DSLEAF_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#define STORAGE_APP_DIR   "/DSLeaf"
#define STORAGE_BOOKS_DIR "/Books"

// Mounts the SD card and makes sure /DSLeaf and /Books exist. Returns false if
// there is no usable card. The app keeps working without one, it just cannot
// save anything.
bool storage_init(void);

// True if storage_init succeeded.
bool storage_available(void);

// Prefix added to every path. Empty on the DS. Used by tests.
void storage_set_root(const char *root);

// Reads a text file into `buf` and adds a terminating NUL. Anything beyond
// cap-1 bytes is cut off. Returns the length, or -1 if the file cannot be read.
// If the file is missing but "<path>.tmp" exists (a save was interrupted
// between removing the old file and renaming the new one), the .tmp is used.
int storage_read_text(const char *path, char *buf, size_t cap);

// Writes `text` safely: to "<path>.tmp" first, then the old file is replaced.
// A failure leaves the old file or the complete .tmp in place, never a
// half-written file under the real name. Returns false on any error.
bool storage_write_text(const char *path, const char *text);

#endif
