// Decoding a picture of a book for a page (docs: decision 075). No DS code, so it is tested on a PC.
#ifndef DSLEAF_PICTURE_H
#define DSLEAF_PICTURE_H

#include "chaptertext.h"
#include "imagedec.h"
#include "picsink.h"

// Decodes `image` of `book`, shrunk to fit `area_w` x `area_h`, and hands the rows to `put_row`
// (and the size to `put_size` first). Returns DEC_OK or why it failed. Progressive JPEG files give
// DEC_UNSUPPORTED.
DecResult picture_decode(Epub *book, const CtImage *image, int area_w, int area_h, PicRowFn put_row,
                         PicSizeFn put_size, void *user);

// The last few shrunk pictures are kept (decision 076), so turning back to one does not decode it
// again. Call this when the book is closed: the cache is keyed by the path inside the zip.
void picture_cache_clear(void);

// A short message for the page of a picture that could not be shown (at most 24 characters).
const char *picture_error_text(DecResult result);

#endif
