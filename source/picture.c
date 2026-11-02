#include "picture.h"

#include <stdio.h>

typedef struct {
	ZipStream zs;
} Reader;

static size_t read_zip(void *user, uint8_t *buf, size_t cap) {
	int n = zip_stream_read(&((Reader *)user)->zs, buf, cap);
	return n < 0 ? 0 : (size_t)n;
}

DecResult picture_decode(Epub *book, const CtImage *image, int area_w, int area_h, PicRowFn put_row,
                         PicSizeFn put_size, void *user) {
	if (image->info.kind == IMAGE_JPEG && image->info.progressive) return DEC_UNSUPPORTED;

	static Reader rd; // holds the 34 KB inflater of the zip stream
	if (!epub_image_open(book, image->path, &rd.zs)) return DEC_BAD;

	PicSink ps;
	ImageSink sink;
	picsink_init(&ps, area_w, area_h, put_row, put_size, user, &sink);
	DecResult result;
	if (image->info.kind == IMAGE_PNG) result = png_decode(read_zip, &rd, &image->info, &sink);
	else result = jpeg_decode(read_zip, &rd, &image->info, area_w, area_h, &sink);
	if (result == DEC_OK && !picsink_finish(&ps)) result = DEC_STOPPED;
	picsink_close(&ps);
	zip_stream_close(&rd.zs);
	return result;
}

const char *picture_error_text(DecResult result) {
	switch (result) {
	case DEC_UNSUPPORTED: return "Picture not supported";
	case DEC_MEMORY: return "Picture too large";
	case DEC_OK: return "";
	default: return "Picture damaged";
	}
}
