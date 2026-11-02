// Plain text books (.txt) (docs: formats.md, decision 074).
//
// A text file is opened as a book with a few chapters, so the reader, the contents, the bookmarks and the
// page counts work as they do for an EPUB. The file is never held in memory: a chapter is a byte range
// that is read as a stream of words and breaks.
//
// - Encoding: UTF-8 (with or without a byte order mark), UTF-16 with a byte order mark, and otherwise
//   Windows-1252 (Latin-1 with curly quotes and dashes), also when a UTF-8 file has invalid bytes.
// - Line breaks: LF, CR LF or CR.
// - Paragraphs: a file with blank lines has paragraphs separated by blank lines, and the lines of a paragraph are
//   joined (a hard-wrapped book). A file without blank lines has a paragraph on every line. A line that is
//   indented more than the one before starts a paragraph, and so does a line after a heading.
// - Headings: a short line on its own between blank lines that starts with Chapter, Book, Part, Volume,
//   Prologue and so on, or is a Roman numeral, or is in capital letters. They are headings of the page.
// - Project Gutenberg files: the licence before the "*** START OF" line and after the "*** END OF" line is left out.
// - A chapter heading with its title on the next line ("CHAPTER I." then "Down the Rabbit-Hole") is one heading of two lines.
// - Chapters: the file is cut into chapters at the lines that start with Chapter, Book, Part and so on, or
//   are a Roman numeral, on their own between blank lines, when at least 1500 bytes follow. Headings that
//   stand close together are a run: three or more are a list of chapter names (a contents list) and are not
//   used, and of two (such as "BOOK ONE" and "I") the first counts. A heading in the first 400 bytes is the start of
//   the first chapter. A file with none is one chapter.
#ifndef DSLEAF_TXTBOOK_H
#define DSLEAF_TXTBOOK_H

#include <stdbool.h>

#include "epub.h"

#define TXT_CHAPTERS_MAX 256

// True if the path ends with .txt (any case).
bool txt_is_path(const char *path);

// Opens a text file as a book: finds the encoding and the chapters. Returns EPUB_OK or the reason it failed
// (EPUB_ERR_READ, EPUB_ERR_NO_CHAPTERS for an empty file or one without text, EPUB_ERR_MEMORY).
EpubResult txt_open(Epub *book, const char *path);
void txt_close(Epub *book);

// Chapter streams, used through epub_chapter_open, epub_chapter_next and epub_chapter_close.
bool txt_chapter_open(Epub *book, int index, EpubChapter *chapter);
bool txt_chapter_next(EpubChapter *chapter, TextItem *item);
void txt_chapter_close(EpubChapter *chapter);

#endif
