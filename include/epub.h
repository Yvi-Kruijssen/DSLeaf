// EPUB loading (docs: formats.md, decisions 023, 036). Only the first check
// exists so far. The real parser is added here later.
#ifndef DSLEAF_EPUB_H
#define DSLEAF_EPUB_H

typedef enum {
	EPUB_OK,
	EPUB_ERR_READ,    // the file cannot be opened or read
	EPUB_ERR_NOT_ZIP, // does not start like a zip archive, so it is not an EPUB
} EpubResult;

// Quick check that `path` can be read and looks like a zip archive. It does
// not look inside yet.
EpubResult epub_check(const char *path);

// Short reason for the error box, at most 24 characters.
const char *epub_error_text(EpubResult result);

#endif
