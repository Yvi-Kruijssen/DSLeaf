#include "picture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Cache of shrunk pictures (decision 076) ----
#define CACHE_ENTRIES 4
#define CACHE_MAX_BYTES (256 * 192)

typedef struct {
	char path[EPUB_PATH_MAX];
	int area_w, area_h, w, h;
	uint8_t *pixels; // w * h palette entries, NULL if the entry is free
	uint32_t used;   // for choosing the oldest
} CacheEntry;

static CacheEntry cache[CACHE_ENTRIES];
static uint32_t cache_clock;

void picture_cache_clear(void) {
	for (int i = 0; i < CACHE_ENTRIES; i++) {
		free(cache[i].pixels);
		cache[i].pixels = NULL;
	}
}

static CacheEntry *cache_find(const char *path, int aw, int ah) {
	for (int i = 0; i < CACHE_ENTRIES; i++) {
		CacheEntry *e = &cache[i];
		if (e->pixels && e->area_w == aw && e->area_h == ah && strcmp(e->path, path) == 0) return e;
	}
	return NULL;
}

// Fills the entry for a picture being decoded, so the rows can be copied into it.
typedef struct {
	PicRowFn row;
	PicSizeFn size;
	void *user;
	CacheEntry *entry; // NULL when the picture could not be given room
	const CtImage *image;
	int aw, ah;
} Tee;

static void tee_size(void *u, int w, int h) {
	Tee *t = u;
	if (t->size) t->size(t->user, w, h);
	if ((size_t)w * (size_t)h > CACHE_MAX_BYTES) return;
	CacheEntry *e = NULL;
	for (int i = 0; i < CACHE_ENTRIES; i++) {
		if (!cache[i].pixels) { e = &cache[i]; break; }
		if (!e || cache[i].used < e->used) e = &cache[i];
	}
	free(e->pixels);
	e->pixels = malloc((size_t)w * (size_t)h);
	if (!e->pixels) return;
	snprintf(e->path, sizeof e->path, "%s", t->image->path);
	e->area_w = t->aw;
	e->area_h = t->ah;
	e->w = w;
	e->h = h;
	e->used = ++cache_clock;
	t->entry = e;
}

static bool tee_row(void *u, int y, int w, const uint8_t *idx) {
	Tee *t = u;
	if (t->entry && y < t->entry->h && w == t->entry->w) memcpy(t->entry->pixels + (size_t)y * (size_t)w, idx, (size_t)w);
	return t->row(t->user, y, w, idx);
}

#ifndef JPEG_OVERSAMPLE_PERCENT
#define JPEG_OVERSAMPLE_PERCENT 150
#endif

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

	CacheEntry *hit = cache_find(image->path, area_w, area_h);
	if (hit) {
		hit->used = ++cache_clock;
		if (put_size) put_size(user, hit->w, hit->h);
		for (int y = 0; y < hit->h; y++) {
			if (!put_row(user, y, hit->w, hit->pixels + (size_t)y * (size_t)hit->w)) return DEC_STOPPED;
		}
		return DEC_OK;
	}

	Tee tee = { put_row, put_size, user, NULL, image, area_w, area_h };
	put_row = tee_row;
	put_size = tee_size;
	user = &tee;

	static Reader rd; // holds the 34 KB inflater of the zip stream
	if (!epub_image_open(book, image->path, &rd.zs)) return DEC_BAD;

	PicSink ps;
	ImageSink sink;
	picsink_init(&ps, area_w, area_h, put_row, put_size, user, &sink);
	DecResult result;
	if (image->info.kind == IMAGE_PNG) result = png_decode(read_zip, &rd, &image->info, &sink);
	else {
		// A JPEG is decoded no smaller than the size it will be shown at, which is the picture shrunk to fit the area
		// (a tall picture is much narrower than the area): this was the whole area before, which made tall pictures
		// decode at four times the size they needed (decision 112).
		int fit_w, fit_h;
		image_fit(image->info.width, image->info.height, area_w, area_h, &fit_w, &fit_h);
		result = jpeg_decode(read_zip, &rd, &image->info, fit_w * JPEG_OVERSAMPLE_PERCENT / 100, fit_h * JPEG_OVERSAMPLE_PERCENT / 100, &sink);
	}
	if (result == DEC_OK && !picsink_finish(&ps)) result = DEC_STOPPED;
	picsink_close(&ps);
	zip_stream_close(&rd.zs);
	if (result != DEC_OK && tee.entry) { // a picture that did not finish is not kept
		free(tee.entry->pixels);
		tee.entry->pixels = NULL;
	}
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
