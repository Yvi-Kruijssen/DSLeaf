#include "epub.h"

#include <stdio.h>

#include "storage.h"

EpubResult epub_check(const char *path) {
	FILE *f = storage_fopen(path, "rb");
	if (!f) return EPUB_ERR_READ;

	unsigned char head[4];
	size_t n = fread(head, 1, sizeof head, f);
	bool failed = ferror(f);
	fclose(f);

	if (failed) return EPUB_ERR_READ;
	// "PK\3\4" starts a zip file. An empty archive ("PK\5\6") cannot be a book.
	if (n < sizeof head || head[0] != 'P' || head[1] != 'K' || head[2] != 3 || head[3] != 4) {
		return EPUB_ERR_NOT_ZIP;
	}
	return EPUB_OK;
}

const char *epub_error_text(EpubResult result) {
	switch (result) {
	case EPUB_OK:           return "No error";
	case EPUB_ERR_READ:     return "Cannot read the file";
	case EPUB_ERR_NOT_ZIP:  return "Not a valid EPUB";
	}
	return "Unknown error";
}
