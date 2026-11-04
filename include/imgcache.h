// Saved picture sizes of EPUB books (docs: storage.md, decision 108).
//
// To lay out a chapter the program has to know which of its pictures get a page, and that depends on their size, which is in
// the first bytes of each picture file. Reading them is one trip to a different place of a big file for every picture,
// which took seconds on a DS for a book with many pictures. What is found is kept in /DSLeaf/images.txt, one section per book:
//
//   # DSLeaf picture sizes v1
//   [book]
//   path=/Books/Pride and Prejudice.epub
//   size=25607139
//   v=1
//   i=1a2b3c4d,2,640,480,0,3,0,0
//
// An `i` line is the key of the picture (a hash of its path in the zip), then the kind, width, height, progressive, components,
// bit depth and colour type of an `ImageInfo`. A book is known by its path and size, as in `pages.txt`.
// The text functions need no DS code, so they are tested on a PC.
#ifndef DSLEAF_IMGCACHE_H
#define DSLEAF_IMGCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imageinfo.h"

#define IMGCACHE_PATH "/DSLeaf/images.txt"
#define IMGCACHE_FILE_MAX (48 * 1024) // the file never grows past this; the oldest books are dropped first
#define IMGCACHE_PATH_MAX 200         // longest book path that can be stored
#define IMGCACHE_MAX 1024             // pictures kept for one book

typedef struct {
	uint32_t key;
	ImageInfo info;
} ImgEntry;

typedef struct ImgCache {
	ImgEntry *entry;
	int count, cap;
	bool dirty;       // something was added that is not saved yet
	char path[IMGCACHE_PATH_MAX + 1];
	uint32_t size;    // of the book file
} ImgCache;

// The key of a picture: a hash of its path inside the zip.
uint32_t imgcache_key(const char *zip_path);

// The section of this book in `text`: its entries into `out` (at most `cap`). Returns how many, 0 if there is none.
// Lines that make no sense are left out.
int imgcache_find(const char *text, const char *path, uint32_t size, ImgEntry *out, int cap);

// The file text with this book's section replaced by (or added as) a new one at the end, the oldest sections dropped to fit
// `cap`. Returns the length, or -1 if the book cannot be stored.
int imgcache_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size, const ImgEntry *entry, int count);

// A cache for a book, loaded from the file (empty if there is nothing yet). NULL if there is no memory.
ImgCache *imgcache_open(const char *path, uint32_t size);
const ImageInfo *imgcache_get(const ImgCache *c, uint32_t key);
void imgcache_put(ImgCache *c, uint32_t key, const ImageInfo *info);
// Writes the file if something was added. True if there was nothing to write or it was written.
bool imgcache_save(ImgCache *c);
// Saves and frees.
void imgcache_close(ImgCache *c);

#endif
