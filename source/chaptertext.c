#include "chaptertext.h"

#include <stdlib.h>
#include <string.h>

#include "layout.h"

static bool open_stream(ChapterText *ct) {
	if (ct->stream_open) epub_chapter_close(&ct->stream);
	ct->stream_open = epub_chapter_open(ct->book, ct->chapter, &ct->stream);
	if (ct->stream_open && ct->watch_count > 0) ct->stream.text.want_anchors = true;
	ct->done = !ct->stream_open;
	ct->have_held = false;
	ct->split_inside = false;
	return ct->stream_open;
}

static void finish_stream(ChapterText *ct) {
	ct->done = true;
	if (ct->stream_open) {
		epub_chapter_close(&ct->stream);
		ct->stream_open = false;
	}
}

// A window ends before a block. An anchor with only pictures or line breaks after
// it in this window belongs to the text that starts the next window, so it is
// moved there. Otherwise a link to a chapter heading would land on the last page
// of the window before it.
static void settle_anchors(ChapterText *ct, size_t content_end) {
	for (int i = 0; i < ct->watch_count; i++) {
		uint32_t at = ct->watch_found[i];
		if (at != 0xFFFFFFFFu && at >= ct->base + content_end && at >= ct->base) {
			ct->watch_found[i] = ct->base + (uint32_t)ct->len;
		}
	}
}

// A link to a place that is a picture goes to the first record after the pictures, so a contents entry
// or a link lands on the text it names and not on the illustration in front of it (decision 075).
static void skip_images(ChapterText *ct) {
	for (int i = 0; i < ct->watch_count; i++) {
		uint32_t at = ct->watch_found[i];
		if (at == 0xFFFFFFFFu || at < ct->base || at >= ct->base + ct->len) continue;
		uint32_t rel = at - ct->base;
		while (rel < ct->len && ct->buf[rel] == REC_IMAGE) rel++;
		if (rel < ct->len) ct->watch_found[i] = ct->base + rel; // a chapter that is only pictures keeps its link
	}
}

// Starts a window: nothing in the buffer yet.
static void begin_window(ChapterText *ct) {
	ct->len = 0;
	ct->has_words = false;
	ct->image_count = 0;
	ct->content_end = 0; // where the last word or block record ends
	ct->window_open = true;

	if (ct->split_inside) {
		// The previous window stopped inside a paragraph. Start this one with a block.
		TextItem block = { .kind = TEXT_BLOCK, .block = BLOCK_PARAGRAPH };
		ct->len += record_append(ct->buf, ct->len, CT_SOFT_LIMIT + CT_SLACK, &block);
		ct->content_end = ct->len;
		ct->split_inside = false;
	}
}

// Reads records into the window until it holds `target` bytes or it is complete (then window_open is false).
// Can be called again to carry on: the state is in `ct`.
static void fill_to(ChapterText *ct, size_t target) {
	size_t cap = CT_SOFT_LIMIT + CT_SLACK;

	for (;;) {
		if (ct->len >= target) return; // enough for now; the window stays open
		TextItem item;
		if (ct->have_held) {
			item = ct->held;
			ct->have_held = false;
		} else if (ct->done || !epub_chapter_next(&ct->stream, &item) || item.kind == TEXT_END) {
			finish_stream(ct);
			break;
		}

		if (item.kind == TEXT_ANCHOR) {
			// Not a record. The next record starts at the current end of the window.
			for (int i = 0; i < ct->watch_count; i++) {
				if (ct->watch_found[i] == 0xFFFFFFFFu && strcmp(item.text, ct->watch[i]) == 0) {
					ct->watch_found[i] = ct->base + (uint32_t)ct->len;
				}
			}
			continue;
		}

		if (ct->len >= CT_SOFT_LIMIT && item.kind == TEXT_BLOCK) {
			ct->held = item;
			ct->have_held = true;
			settle_anchors(ct, ct->content_end);
			break;
		}
		if (item.kind == TEXT_IMAGE) {
			// Only pictures big enough to look at get a record (and so a page).
			if (ct->image_count >= CT_IMAGES_MAX) continue;
			CtImage *im = &ct->images[ct->image_count];
			if (!epub_image_info(ct->book, ct->chapter, item.text, im->path, sizeof im->path, &im->info) ||
			    !image_wanted(im->info.width, im->info.height)) {
				continue;
			}
			im->offset = (uint32_t)ct->len;
			ct->image_count++;
		}
		size_t n = record_append(ct->buf, ct->len, cap, &item);
		if (n == 0) {
			if (item.kind == TEXT_IMAGE) ct->image_count--;
			// Out of room in the middle of a paragraph: carry on in the next window.
			ct->held = item;
			ct->have_held = true;
			ct->split_inside = true;
			settle_anchors(ct, ct->content_end);
			break;
		}
		ct->len += n;
		if (item.kind == TEXT_WORD || item.kind == TEXT_BLOCK) ct->content_end = ct->len;
		if (item.kind == TEXT_WORD) ct->has_words = true;
	}
	ct->window_open = false;
}

static void fill(ChapterText *ct) {
	begin_window(ct);
	fill_to(ct, (size_t)-1);
	if (ct->watch_count > 0) skip_images(ct);
}

void ct_fill_more(ChapterText *ct, size_t more) {
	if (ct->window_open) fill_to(ct, ct->len + more);
}

void ct_complete(ChapterText *ct) {
	if (ct->window_open) fill_to(ct, (size_t)-1);
}

bool ct_has_next(const ChapterText *ct) {
	return !ct->done || ct->have_held;
}

bool ct_next_window(ChapterText *ct) {
	ct_complete(ct);
	if (!ct_has_next(ct)) return false;
	ct->base += (uint32_t)ct->len;
	ct->window++;
	fill(ct);
	return true;
}

// The window that is started by a next window call, filled only as far as its first bytes.
bool ct_next_window_lazy(ChapterText *ct) {
	ct_complete(ct);
	if (!ct_has_next(ct)) return false;
	ct->base += (uint32_t)ct->len;
	ct->window++;
	begin_window(ct);
	fill_to(ct, CT_FIRST_SLACK);
	return true;
}

static bool open_with(ChapterText *ct, Epub *book, int chapter, uint32_t offset) {
	ct->book = book;
	ct->chapter = chapter;
	ct->buf = malloc(CT_SOFT_LIMIT + CT_SLACK);
	if (!ct->buf) return false;

	if (!open_stream(ct)) {
		ct_close(ct);
		return false;
	}
	fill(ct);
	while (offset >= ct->base + ct->len && ct_has_next(ct)) ct_next_window(ct);
	return true;
}

bool ct_open(ChapterText *ct, Epub *book, int chapter, uint32_t offset) {
	memset(ct, 0, sizeof *ct);
	return open_with(ct, book, chapter, offset);
}

// Like ct_open, but the window holding `offset` is read only a little past it (decision 111).
bool ct_open_lazy(ChapterText *ct, Epub *book, int chapter, uint32_t offset, CtSkipFn skipped, void *user) {
	memset(ct, 0, sizeof *ct);
	ct->book = book;
	ct->chapter = chapter;
	ct->buf = malloc(CT_SOFT_LIMIT + CT_SLACK);
	if (!ct->buf) return false;
	if (!open_stream(ct)) {
		ct_close(ct);
		return false;
	}
	begin_window(ct);
	for (;;) {
		uint64_t want = (offset > ct->base ? offset - ct->base : 0) + (uint64_t)CT_FIRST_SLACK;
		fill_to(ct, want > (uint64_t)(CT_SOFT_LIMIT + CT_SLACK) ? (size_t)(CT_SOFT_LIMIT + CT_SLACK) : (size_t)want);
		if (offset < (uint64_t)ct->base + ct->len || ct->window_open) break; // the offset is in this window
		if (!ct_has_next(ct)) break;                                           // past the end: the last window
		if (skipped) skipped(ct, user);
		ct->base += (uint32_t)ct->len;
		ct->window++;
		begin_window(ct);
	}
	return true;
}

int ct_find_anchors(Epub *book, int chapter, const char *const *ids, int count, uint32_t *offsets) {
	for (int i = 0; i < count; i++) offsets[i] = 0xFFFFFFFFu;
	if (count <= 0) return 0;

	static ChapterText ct; // 144 KB of buffer, so keep the struct itself off the stack
	memset(&ct, 0, sizeof ct);
	ct.watch = ids;
	ct.watch_count = count;
	ct.watch_found = offsets;
	if (!open_with(&ct, book, chapter, 0)) return -1;

	int found = 0;
	for (;;) {
		found = 0;
		for (int i = 0; i < count; i++) found += offsets[i] != 0xFFFFFFFFu;
		if (found == count || !ct_has_next(&ct)) break;
		ct_next_window(&ct);
	}
	ct_close(&ct);
	return found;
}

bool ct_prev_window(ChapterText *ct) {
	if (ct->window == 0) return false;
	int target = ct->window - 1;

	if (!open_stream(ct)) return false;
	ct->base = 0;
	ct->window = 0;
	fill(ct);
	while (ct->window < target) ct_next_window(ct);
	return true;
}

void ct_close(ChapterText *ct) {
	if (ct->stream_open) epub_chapter_close(&ct->stream);
	ct->stream_open = false;
	free(ct->buf);
	ct->buf = NULL;
}

int ct_progress(const ChapterText *ct) {
	if (ct->done && !ct->have_held) return 1000;
	uint32_t size = ct->book->chapter_size[ct->chapter];
	if (size == 0 || !ct->stream_open) return 1000;
	uint32_t left = ct->stream.stream.out_left;
	if (left > size) left = size;
	return (int)(((uint64_t)(size - left) * 1000) / size);
}

const CtImage *ct_image_at(const ChapterText *ct, uint32_t offset) {
	if (offset < ct->base) return NULL;
	uint32_t rel = offset - ct->base;
	for (int i = 0; i < ct->image_count; i++) {
		if (ct->images[i].offset == rel) return &ct->images[i];
	}
	return NULL;
}
