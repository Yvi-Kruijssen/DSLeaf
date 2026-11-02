// Saved page counts (docs: storage.md, decision 055).
//
// The number of pages of each chapter depends on the page size and the paragraph
// style, and is only known once the chapter has been laid out. To make the page
// numbers of a book exact from the start of a later session, the counts are
// kept in /DSLeaf/pages.txt, one section per book, for the layout last used:
//
//   # DSLeaf page counts v1
//   [book]
//   path=/Books/Moby Dick.epub
//   size=481234
//   layout=30,23,1,0
//   p=12,0,65535,9
//
// `layout` is columns, rows, paragraph gap and indent. `p` has one number for
// each chapter: its pages, 0 if not known yet, 65535 for a chapter without text.
// The text functions need no DS code, so they are tested on a PC.
#ifndef DSLEAF_PAGECACHE_H
#define DSLEAF_PAGECACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGECACHE_PATH "/DSLeaf/pages.txt"
#define PAGECACHE_FILE_MAX (48 * 1024) // the file never grows past this; the oldest books are dropped first
#define PAGECACHE_PATH_MAX 200         // longest book path that can be stored
#define PAGECACHE_CHAPTERS_MAX 2000    // a book with more chapters is not stored

typedef struct {
	int cols, rows, gap, indent;
} PageLayout;

// Finds the book's section in `text` and, if it has this layout and `count`
// chapters, copies its page counts to `pages`. Returns false otherwise.
bool pagecache_find(const char *text, const char *path, uint32_t size, const PageLayout *layout, uint16_t *pages,
                    int count);

// Writes the file text for `old_text` with this book's section replaced by (or added as) a
// new one at the end. The oldest sections are dropped to fit `cap`. Returns the length,
// or -1 if the book cannot be stored (too many chapters, a long path, `cap` too small).
int pagecache_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size,
                     const PageLayout *layout, const uint16_t *pages, int count);

// The file versions of the two above. Load returns false if there is no entry for this layout.
bool pagecache_load(const char *path, uint32_t size, const PageLayout *layout, uint16_t *pages, int count);
bool pagecache_save(const char *path, uint32_t size, const PageLayout *layout, const uint16_t *pages, int count);

#endif
