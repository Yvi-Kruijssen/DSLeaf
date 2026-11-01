// Bookmarks and the last reading position (docs: storage.md, decisions 020, 045).
//
// One text file, /DSLeaf/bookmarks.txt, holds a section for each book. A book is
// identified by its path and its file size, so a renamed or replaced book starts
// afresh. A position is a chapter (an index into the book's chapter list) and an
// offset in that chapter's record stream (decision 041), so it stays valid when
// the page size, the font or the paragraph spacing change.
//
//   # DSLeaf bookmarks v1
//   [book]
//   path=/Books/Moby Dick.epub
//   size=481234
//   last=3,1520
//   b=3,1520,It was the best of
//   b=7,88,Call me Ishmael
//
// The text functions need no DS code, so they are tested on a PC.
#ifndef DSLEAF_BOOKMARKS_H
#define DSLEAF_BOOKMARKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOKMARKS_PATH "/DSLeaf/bookmarks.txt"
#define BOOKMARKS_MAX 20            // per book
#define BOOKMARKS_FILE_MAX (32 * 1024) // the file never grows past this; the oldest books are dropped first
#define MARK_TEXT_MAX 28            // bytes of text kept with a bookmark, with the NUL
#define BOOKMARKS_PATH_MAX 200      // longest book path that can be stored

typedef struct {
	uint16_t chapter;
	uint32_t offset;
	char text[MARK_TEXT_MAX]; // the first words of the page, to recognise it in the menu
} Mark;

typedef struct {
	bool has_last;
	uint16_t last_chapter;
	uint32_t last_offset;
	int count;
	Mark mark[BOOKMARKS_MAX]; // sorted by chapter, then offset
} BookMarks;

typedef enum { MARK_ADDED, MARK_EXISTS, MARK_FULL } MarkResult;

// Adds a bookmark in its place in the sorted list. `text` is cleaned up: control
// characters become spaces, runs of spaces become one, and it is cut to fit.
MarkResult marks_add(BookMarks *marks, int chapter, uint32_t offset, const char *text);

// Removes the bookmarks of `chapter` whose offset is in [from, to). Returns how many.
int marks_remove_range(BookMarks *marks, int chapter, uint32_t from, uint32_t to);
void marks_remove_at(BookMarks *marks, int index);

// How many bookmarks of `chapter` have an offset in [from, to).
int marks_count_range(const BookMarks *marks, int chapter, uint32_t from, uint32_t to);

// ---- The file ----

// Finds this book's section in the file text. Returns false if it has none. A
// damaged line is skipped and never stops the rest from loading.
bool bookmarks_parse(const char *text, const char *path, uint32_t size, BookMarks *out);

// Writes the file text with this book's section replaced by `marks` and moved to
// the end (the most recently read book). A book with no position and no
// bookmarks is left out. If the result would be longer than `cap`, the oldest
// other books are dropped. Returns the length, or -1 on error (a path that is
// too long or has a line break, or no room even for this book).
int bookmarks_merge(const char *text, const char *path, uint32_t size, const BookMarks *marks, char *out,
                    size_t cap);

// Reads and saves through the SD card. Loading gives an empty set and returns
// false if the book has no section (or there is no file).
bool bookmarks_load(const char *path, uint32_t size, BookMarks *marks);
bool bookmarks_save(const char *path, uint32_t size, const BookMarks *marks);

#endif
