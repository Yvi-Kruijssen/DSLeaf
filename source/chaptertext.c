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

// Fills the buffer with the next window of records.
static void fill(ChapterText *ct) {
	size_t cap = CT_SOFT_LIMIT + CT_SLACK;
	ct->len = 0;
	ct->has_words = false;
	size_t content_end = 0; // where the last word or block record ends

	if (ct->split_inside) {
		// The previous window stopped inside a paragraph. Start this one with a block.
		TextItem block = { .kind = TEXT_BLOCK, .block = BLOCK_PARAGRAPH };
		ct->len += record_append(ct->buf, ct->len, cap, &block);
		content_end = ct->len;
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
			settle_anchors(ct, content_end);
			break;
		}
		size_t n = record_append(ct->buf, ct->len, cap, &item);
		if (n == 0) {
			// Out of room in the middle of a paragraph: carry on in the next window.
			ct->held = item;
			ct->have_held = true;
			ct->split_inside = true;
			settle_anchors(ct, content_end);
			break;
		}
		ct->len += n;
		if (item.kind == TEXT_WORD || item.kind == TEXT_BLOCK) content_end = ct->len;
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
