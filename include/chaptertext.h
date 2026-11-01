// A chapter's text held in memory in windows (docs: decision 041).
//
// The word stream of a chapter is turned into compact records (see layout.h)
// and kept in one buffer, so the layout can work on any page of it in any
// order. A normal chapter fits in one window. A very long one (a whole book in
// a single file) is cut into several windows at paragraph boundaries, so memory
// use stays bounded. Windows are read in order. Going back to an earlier window
// reads the chapter again from its start.
//
// Offsets are positions in the compact record stream, counted from the start
// of the chapter. They depend only on the book, so they are stable and can be
// saved in bookmarks.
#ifndef DSLEAF_CHAPTERTEXT_H
#define DSLEAF_CHAPTERTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epub.h"

#define CT_SOFT_LIMIT (128 * 1024) // a window ends at the first paragraph break after this
#define CT_SLACK (16 * 1024)       // room for a paragraph that goes on past the limit

typedef struct {
	Epub *book;
	int chapter;
	EpubChapter stream;
	bool stream_open;
	bool done;          // the whole chapter has been read (a held item may remain)
	TextItem held;      // the first item of the next window
	bool have_held;
	bool split_inside;  // the last window ended in the middle of a paragraph

	uint8_t *buf;       // the current window
	size_t len;
	uint32_t base;      // offset of buf[0] within the chapter
	int window;
	bool has_words;

	// Looking for anchors: while a window is read, the offset of each id in
	// `watch` is noted. See ct_find_anchors.
	const char *const *watch;
	int watch_count;
	uint32_t *watch_found;
} ChapterText;

// Reads the window that contains `offset` (a huge offset gives the last window).
// Returns false if the chapter cannot be read or memory is short.
bool ct_open(ChapterText *ct, Epub *book, int chapter, uint32_t offset);
void ct_close(ChapterText *ct);

bool ct_has_next(const ChapterText *ct);
bool ct_next_window(ChapterText *ct); // false if this is the last window
bool ct_prev_window(ChapterText *ct); // false if this is the first window

// Finds where the elements with the given ids are in a chapter, as offsets from
// the start of the chapter's text (the same offsets bookmarks use). `offsets`
// gets one entry per id, UINT32_MAX for an id that is not in the chapter.
// Reads the chapter once, window by window. Returns how many were found, or -1
// if the chapter could not be read.
int ct_find_anchors(Epub *book, int chapter, const char *const *ids, int count, uint32_t *offsets);

// How much of the chapter file has been read so far, 0 to 1000.
int ct_progress(const ChapterText *ct);

#endif
