// EPUB loading (docs: formats.md, decisions 023, 036, 037, 039). Plain text books (txtbook.h, decision 074) use
// the same structure and functions.
//
// epub_check is a quick test used before anything else. epub_open reads the
// book's structure: its title and the chapters in reading order.
#ifndef DSLEAF_EPUB_H
#define DSLEAF_EPUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imageinfo.h"
#include "textx.h"
#include "txtstream.h"
#include "zip.h"

#define EPUB_TITLE_MAX 128
#define EPUB_PATH_MAX 192    // longest chapter path inside the zip
#define EPUB_CHAPTER_MAX 1024

typedef enum {
	EPUB_OK,
	EPUB_ERR_READ,         // the file cannot be opened or read
	EPUB_ERR_NOT_ZIP,      // not a usable zip archive, so not an EPUB
	EPUB_ERR_NO_CONTAINER, // META-INF/container.xml is missing or names no book file
	EPUB_ERR_BAD_PACKAGE,  // the book file (OPF) is missing or unreadable
	EPUB_ERR_NO_CHAPTERS,  // the book has no readable chapters
	EPUB_ERR_DRM,          // protected by DRM
	EPUB_ERR_MEMORY,
} EpubResult;

typedef struct {
	Zip zip;
	char title[EPUB_TITLE_MAX];          // empty if the book has none
	char lang[8];                        // the language of the book in lower case, such as "en" or "en-us", empty if unknown (decision 093)
	char toc_path[EPUB_PATH_MAX];        // table of contents file in the zip, or empty
	bool toc_is_nav;                     // true for an EPUB 3 nav document, false for NCX
	int chapter_count;
	char (*chapter)[EPUB_PATH_MAX];      // zip paths in reading order, allocated by epub_open
	uint32_t *chapter_size;              // uncompressed size of each chapter file, in bytes
	int skipped;                         // spine entries that could not be used
	struct TxtBook *txt;                 // set for a plain text book (txtbook.h), NULL for an EPUB
} Epub;

// Quick check that `path` can be read and looks like a zip archive.
EpubResult epub_check(const char *path);

// Opens the book and reads its structure. On success the caller must call
// epub_close. On failure nothing needs closing.
EpubResult epub_open(Epub *book, const char *path);
void epub_close(Epub *book);

// Reads one chapter as a stream of words and breaks (see textx.h). The stream
// holds about 37 KB while open. Only one chapter needs to be open at a time.
typedef struct {
	ZipStream stream; // must stay the first member
	TextExtractor text;
	TxtStream txt;    // used instead of the two above for a plain text book
} EpubChapter;

// Opens chapter `index` (0 to chapter_count - 1). Returns false if it cannot be read.
bool epub_chapter_open(Epub *book, int index, EpubChapter *chapter);

// Next item. Returns false after the END item.
bool epub_chapter_next(EpubChapter *chapter, TextItem *item);
void epub_chapter_close(EpubChapter *chapter);

// True if chapter `index` has at least one word of text or one picture that gets a page
// (imageinfo.h). Chapters with nothing else give false. Cheap: stops at the
// first word.
bool epub_chapter_has_text(Epub *book, int index);

// Pictures (decision 075). `src` is the link as written in chapter `chapter`. Finds the
// picture in the zip, gives its path inside the zip in `path` and its header in `info`.
// False for a text book, a missing file or a format that is not a picture.
bool epub_image_info(Epub *book, int chapter, const char *src, char *path, size_t cap, ImageInfo *info);

// Opens a picture found by epub_image_info for reading.
bool epub_image_open(Epub *book, const char *path, ZipStream *stream);

// Short reason for the error box, at most 24 characters.
const char *epub_error_text(EpubResult result);

// Turns a link from inside the book into a zip path. `base_dir` is the folder
// of the file the link is in, such as "OEBPS/" or "". Drops "#fragment" and
// "?query", decodes %20 style escapes and resolves "." and "..". Returns false
// if the result does not fit or leaves the zip (too many "..").
bool epub_resolve_path(const char *base_dir, const char *href, char *out, size_t cap);

#endif
