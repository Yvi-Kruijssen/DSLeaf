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

// Decodes a picture into the cache without showing it (decision 115), so that it is there when its page comes up. The
// reader does this for the next picture while nothing is pressed. `stop` is asked before every row of the picture and
// ends the work when it says true (a key was pressed): DEC_STOPPED, and nothing is kept. DEC_OK when the picture is in
// the cache now (or was already). A picture that cannot be decoded is remembered and not tried again.
DecResult picture_prefetch(Epub *book, const CtImage *image, int area_w, int area_h, bool (*stop)(void *user), void *user);

// True if the picture is in the cache for that area, so that showing it takes no time.
bool picture_cached(const CtImage *image, int area_w, int area_h);

// The last few shrunk pictures are kept (decision 076), so turning back to one does not decode it
// again. Call this when the book is closed: the cache is keyed by the path inside the zip.
void picture_cache_clear(void);

// A short message for the page of a picture that could not be shown (at most 24 characters).
const char *picture_error_text(DecResult result);

#endif
