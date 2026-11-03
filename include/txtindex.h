// Saved chapter lists of big plain text books (docs: storage.md, decision 104).
//
// Opening a .txt book reads the whole file once to find its encoding's text, the blank lines and the chapter headings.
// That takes seconds for a big book on a DS, so what it finds is kept in /DSLeaf/txtindex.txt, one section per book:
//
//   # DSLeaf text index v1
//   [book]
//   path=/Books/Moby Dick.txt
//   size=1276315
//   v=1
//   i=0,0,1276315,0,1
//   s=0,1200,5400,1276315
//
// `i` is the encoding, where the text starts, where it ends, 1 if every line is a paragraph, and 1 if it looks English.
// `s` has the offset where each chapter starts and, last, the end of the text. A book is known by its path and size.
// The functions need no DS code, so they are tested on a PC.
#ifndef DSLEAF_TXTINDEX_H
#define DSLEAF_TXTINDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TXTINDEX_PATH "/DSLeaf/txtindex.txt"
#define TXTINDEX_FILE_MAX (24 * 1024)   // the file never grows past this; the oldest books are dropped first
#define TXTINDEX_MIN_BOOK (96 * 1024)   // a smaller book is scanned so fast that it is not stored
#define TXTINDEX_CHAPTERS_MAX 256
#define TXTINDEX_PATH_MAX 200

typedef struct {
	int encoding;
	uint32_t data_start, data_end;
	bool line_mode, english;
	int count;                                      // chapters
	uint32_t start[TXTINDEX_CHAPTERS_MAX + 1];      // the start of each chapter, then the end of the text
} TxtIndex;

// Reads the section of this book from `text`. False if there is none, or it is damaged (the numbers must make sense:
// the chapters in order, inside the file of `size` bytes).
bool txtindex_find(const char *text, const char *path, uint32_t size, TxtIndex *ix);

// The text of the file with this book's section replaced by (or added as) a new one at the end, the oldest sections
// dropped to fit `cap`. Returns the length, or -1 if the book cannot be stored.
int txtindex_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size, const TxtIndex *ix);

// The file versions. Load false if the book is not there. Save false if it could not be written.
bool txtindex_load(const char *path, uint32_t size, TxtIndex *ix);
bool txtindex_save(const char *path, uint32_t size, const TxtIndex *ix);

#endif
