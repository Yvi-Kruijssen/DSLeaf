// The table of contents of a book (docs: formats.md, decision 048).
//
// Books store it in one of two ways: an NCX file (EPUB 2) or a navigation
// document (EPUB 3). Both are read into the same list of entries. An entry has a
// title, the chapter it points into and, often, an anchor (the part after "#")
// that points into the middle of that chapter. A chapter file can hold many
// entries, so the position of an anchor is looked up when it is needed.
#ifndef DSLEAF_TOC_H
#define DSLEAF_TOC_H

#include <stdbool.h>
#include <stdint.h>

#include "epub.h"
#include "xml.h"

#define TOC_TITLE_MAX 56
#define TOC_ANCHOR_MAX 96
#define TOC_ENTRIES_MAX 512 // more entries than this are left out
#define TOC_LEVEL_MAX 3     // deeper levels are shown as this one
#define TOC_OFFSET_UNKNOWN 0xFFFFFFFFu

typedef struct {
	char title[TOC_TITLE_MAX];
	char anchor[TOC_ANCHOR_MAX]; // after the "#", or empty for the start of the chapter
	uint16_t chapter;            // an index into the book's chapter list
	uint8_t level;               // 0 for the top level
	uint32_t offset;             // in the chapter's text, TOC_OFFSET_UNKNOWN until looked up
} TocEntry;

typedef struct {
	TocEntry *entry;
	int count;
	int cap;
} Toc;

// Turns a path inside the zip into a chapter index, or -1 if it is not a chapter.
typedef int (*TocLookup)(void *user, const char *zip_path);

// Reads a table of contents document and adds its entries. `is_nav` is true for
// an EPUB 3 navigation document and false for an NCX. Links are relative to
// `base_dir` (the folder of the document, such as "OEBPS/" or ""). Entries that
// point to something that is not a chapter, or have no title, are skipped.
// Returns the number of entries added.
int toc_parse(Toc *toc, XmlReadFn read, void *source, bool is_nav, const char *base_dir, TocLookup lookup,
              void *lookup_user);

// Reads the table of contents of an open book. A book without a usable one gets
// a list made from its chapters: the first heading of each chapter that has
// text, or "Section N" (decision 052). Returns false only if there is nothing
// at all to list (`toc` is then empty). Free it with toc_free.
bool toc_load(Toc *toc, Epub *book);
void toc_free(Toc *toc);

// Looks up where the entries that point into `chapter` are in its text. An
// entry without an anchor, or whose anchor is not found, points to the start.
// Does nothing for entries that are already known. Returns false if the chapter
// could not be read.
bool toc_resolve(Toc *toc, Epub *book, int chapter);

// The entry a place in the book belongs to: the last entry at or before it, or
// -1 if the place is before the first entry. The offsets of the entries in
// `chapter` should have been looked up with toc_resolve.
int toc_current(const Toc *toc, int chapter, uint32_t offset);

#endif
