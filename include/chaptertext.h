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
#include "imageinfo.h"

#define CT_SOFT_LIMIT (128 * 1024) // a window ends at the first paragraph break after this
#define CT_SLACK (16 * 1024)       // room for a paragraph that goes on past the limit
#define CT_FIRST_SLACK (8 * 1024)  // a window opened lazily is read this far past the place that is wanted (decision 111)

// A picture of the current window that gets a page (decision 075).
#define CT_IMAGES_MAX 32
typedef struct {
	uint32_t offset;           // of its REC_IMAGE record, from the start of the window
	ImageInfo info;
	char path[EPUB_PATH_MAX];  // inside the zip
} CtImage;

typedef struct {
	Epub *book;
	int chapter;
	EpubChapter stream;
	bool stream_open;
	bool done;          // the whole chapter has been read (a held item may remain)
	TextItem held;      // the first item of the next window
	bool have_held;
	bool split_inside;  // the last window ended in the middle of a paragraph
	bool window_open;   // the window is only filled part of the way and can take more (decision 111); `len` grows
	size_t content_end; // where the last word or block record of the window ends

	uint8_t *buf;       // the current window
	size_t len;
	uint32_t base;      // offset of buf[0] within the chapter
	int window;
	bool has_words;
	CtImage images[CT_IMAGES_MAX]; // the pictures of the current window
	int image_count;

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

// A window opened lazily (ct_open_lazy, ct_next_window_lazy) holds only the start of its text, up to `window_open`
// being false. ct_fill_more reads more of it, ct_complete the rest. ct_has_next and ct_next_window only mean a
// following window once the window is complete (ct_next_window completes it first).
// `skipped` (may be NULL) is called for each whole window that is passed on the way to the one holding `offset`,
// before the next one is started (the reader counts its pages with it, decision 111).
typedef void (*CtSkipFn)(const ChapterText *ct, void *user);
bool ct_open_lazy(ChapterText *ct, Epub *book, int chapter, uint32_t offset, CtSkipFn skipped, void *user);
// `first` is how many bytes of the new window are read at once (0: none, the caller reads on with ct_fill_more).
bool ct_next_window_lazy(ChapterText *ct, size_t first);
// Opens a chapter with an empty first window (the same, without reading anything: for the background counting).
bool ct_open_start(ChapterText *ct, Epub *book, int chapter);
void ct_fill_more(ChapterText *ct, size_t more);
void ct_complete(ChapterText *ct);

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
// The picture whose record is at `offset` (counted from the start of the chapter), or NULL.
const CtImage *ct_image_at(const ChapterText *ct, uint32_t offset);

int ct_progress(const ChapterText *ct);

#endif
