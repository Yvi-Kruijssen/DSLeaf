#include "chaptertext.h"

#include <stdlib.h>
#include <string.h>

#include "layout.h"

static bool open_stream(ChapterText *ct) {
	if (ct->stream_open) epub_chapter_close(&ct->stream);
	ct->stream_open = epub_chapter_open(ct->book, ct->chapter, &ct->stream);
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

// Fills the buffer with the next window of records.
static void fill(ChapterText *ct) {
	size_t cap = CT_SOFT_LIMIT + CT_SLACK;
	ct->len = 0;
	ct->has_words = false;

	if (ct->split_inside) {
		// The previous window stopped inside a paragraph. Start this one with a block.
		TextItem block = { .kind = TEXT_BLOCK, .block = BLOCK_PARAGRAPH };
		ct->len += record_append(ct->buf, ct->len, cap, &block);
		ct->split_inside = false;
	}

	for (;;) {
		TextItem item;
		if (ct->have_held) {
			item = ct->held;
			ct->have_held = false;
		} else if (ct->done || !epub_chapter_next(&ct->stream, &item) || item.kind == TEXT_END) {
			finish_stream(ct);
			break;
		}

		if (ct->len >= CT_SOFT_LIMIT && item.kind == TEXT_BLOCK) {
			ct->held = item;
			ct->have_held = true;
			break;
		}
		size_t n = record_append(ct->buf, ct->len, cap, &item);
		if (n == 0) {
			// Out of room in the middle of a paragraph: carry on in the next window.
			ct->held = item;
			ct->have_held = true;
			ct->split_inside = true;
			break;
		}
		ct->len += n;
		if (item.kind == TEXT_WORD) ct->has_words = true;
	}
}

bool ct_has_next(const ChapterText *ct) {
	return !ct->done || ct->have_held;
}

bool ct_next_window(ChapterText *ct) {
	if (!ct_has_next(ct)) return false;
	ct->base += (uint32_t)ct->len;
	ct->window++;
	fill(ct);
	return true;
}

bool ct_open(ChapterText *ct, Epub *book, int chapter, uint32_t offset) {
	memset(ct, 0, sizeof *ct);
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
