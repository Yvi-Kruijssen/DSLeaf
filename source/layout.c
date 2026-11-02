#include "layout.h"

#include <string.h>

#include "text.h"

#define GLYPHS_MAX (TEXTX_WORD_MAX * 3)

size_t record_size(const uint8_t *p) {
	switch (p[0]) {
	case REC_WORD:  return 3u + p[2];
	case REC_BLOCK: return 3;
	case REC_BREAK: return 1;
	case REC_IMAGE: return 1;
	}
	return 0;
}

size_t record_append(uint8_t *buf, size_t len, size_t cap, const TextItem *item) {
	size_t need;
	switch (item->kind) {
	case TEXT_WORD: {
		size_t n = strlen(item->text);
		if (n == 0 || n > TEXTX_WORD_MAX) return 0;
		need = 3 + n;
		if (cap - len < need) return 0;
		buf[len] = REC_WORD;
		buf[len + 1] = item->space_before ? REC_FLAG_SPACE : 0;
		buf[len + 2] = (uint8_t)n;
		memcpy(buf + len + 3, item->text, n);
		return need;
	}
	case TEXT_BLOCK:
		if (cap - len < 3) return 0;
		buf[len] = REC_BLOCK;
		buf[len + 1] = (uint8_t)item->block;
		buf[len + 2] = (uint8_t)item->level;
		return 3;
	case TEXT_LINEBREAK:
		if (cap - len < 1) return 0;
		buf[len] = REC_BREAK;
		return 1;
	case TEXT_IMAGE:
		if (cap - len < 1) return 0;
		buf[len] = REC_IMAGE;
		return 1;
	case TEXT_ANCHOR:
	case TEXT_END:
		break;
	}
	return 0;
}

PageStart layout_first_page(void) {
	PageStart s = { 0, 0, BLOCK_PARAGRAPH, 0, false };
	return s;
}

// Everything the layout needs while it works on one page.
typedef struct {
	const uint8_t *buf;
	size_t len;
	int cols, rows;
	LayoutStyle style;
	Page *page;

	BlockKind block;
	int level;
	bool first_line;   // the next line is the first line of its block
	BlockKind prev_block;
	int pending_blank; // blank lines wanted before the next line of text

	uint8_t cur[LAYOUT_COLS_MAX];
	int cur_len;
	int cur_text;      // glyphs of real text on the current line (not indent)
	// Where the current line began, in case the page turns out to be full.
	PageStart line_start;
} State;

static void clear_page(Page *page) {
	page->line_count = 0;
	page->at_end = false;
	page->picture = false;
	page->picture_offset = 0;
}

static void push_line(State *s, const uint8_t *glyphs, int len) {
	LayoutLine *l = &s->page->line[s->page->line_count++];
	memcpy(l->glyph, glyphs, (size_t)len);
	l->len = (uint8_t)len;
}

static void push_blank(State *s) {
	LayoutLine *l = &s->page->line[s->page->line_count++];
	l->len = 0;
}

// Ends the line being built. Headings are centred.
static void finish_line(State *s) {
	if (s->cur_text == 0) {
		s->cur_len = 0;
		return;
	}
	if (s->block == BLOCK_HEADING && s->cur_len < s->cols) {
		int pad = (s->cols - s->cur_len) / 2;
		uint8_t tmp[LAYOUT_COLS_MAX];
		memset(tmp, ' ', (size_t)pad);
		memcpy(tmp + pad, s->cur, (size_t)s->cur_len);
		push_line(s, tmp, pad + s->cur_len);
	} else {
		push_line(s, s->cur, s->cur_len);
	}
	s->cur_len = 0;
	s->cur_text = 0;
	s->first_line = false;
}

static int left_indent(const State *s) {
	switch (s->block) {
	case BLOCK_PARAGRAPH: return s->first_line ? s->style.paragraph_indent : 0;
	case BLOCK_LIST_ITEM: return LAYOUT_LIST_INDENT;
	case BLOCK_QUOTE:     return LAYOUT_QUOTE_INDENT;
	default:              return 0;
	}
}

// Starts a new line of text at the indent. Returns false if the page has no
// room, in which case the page is complete and `next` is set from `where`.
static bool start_line(State *s, uint32_t offset, int skip) {
	int blanks = s->page->line_count > 0 ? s->pending_blank : 0; // none at the top of a page
	if (s->page->line_count + blanks + 1 > s->rows) {
		// No room for the blank lines and one line of text: the page ends here.
		s->page->next.offset = offset;
		s->page->next.skip = (uint16_t)skip;
		s->page->next.block = (uint8_t)s->block;
		s->page->next.level = (uint8_t)s->level;
		s->page->next.continuation = !s->first_line;
		return false;
	}
	for (int i = 0; i < blanks; i++) push_blank(s);
	s->pending_blank = 0;

	s->line_start.offset = offset;
	s->line_start.skip = (uint16_t)skip;
	s->line_start.block = (uint8_t)s->block;
	s->line_start.level = (uint8_t)s->level;
	s->line_start.continuation = !s->first_line;

	int indent = left_indent(s);
	memset(s->cur, ' ', (size_t)indent);
	if (s->block == BLOCK_LIST_ITEM && s->first_line) {
		s->cur[0] = '-';
	}
	s->cur_len = indent;
	s->cur_text = 0;
	return true;
}

// Turns a UTF-8 word into glyphs. Returns how many.
static int to_glyphs(const uint8_t *text, int bytes, uint8_t *out) {
	const char *p = (const char *)text;
	const char *end = p + bytes;
	int n = 0;
	while (p < end && n + 3 <= GLYPHS_MAX) {
		uint8_t g[3];
		int count = text_utf8_next(&p, g);
		for (int i = 0; i < count; i++) out[n++] = g[i];
	}
	return n;
}

// Places a word. Returns false if the page filled up first (then `next` says
// where to carry on, which is inside this word if some of it was placed).
static bool place_word(State *s, uint32_t offset, const uint8_t *glyphs, int total, int skip,
                       bool space_before) {
	int pos = skip; // glyphs of the word already placed
	bool first_piece = true;

	while (pos < total) {
		int remaining = total - pos;

		if (s->cur_text == 0) {
			if (s->cur_len == 0 && !start_line(s, offset, pos)) return false;
			int avail = s->cols - s->cur_len;
			int take = remaining < avail ? remaining : avail; // a word wider than the line is cut
			memcpy(s->cur + s->cur_len, glyphs + pos, (size_t)take);
			s->cur_len += take;
			s->cur_text += take;
			pos += take;
			if (pos < total) finish_line(s);
			first_piece = false;
			continue;
		}

		int gap = (space_before && first_piece) ? 1 : 0;
		if (s->cur_len + gap + remaining <= s->cols) {
			if (gap) s->cur[s->cur_len++] = ' ';
			memcpy(s->cur + s->cur_len, glyphs + pos, (size_t)remaining);
			s->cur_len += remaining;
			s->cur_text += remaining;
			return true;
		}
		finish_line(s); // does not fit: the word starts the next line
		first_piece = false;
	}
	return true;
}

void layout_page(const uint8_t *buf, size_t len, PageStart start, int cols, int rows,
                 const LayoutStyle *style, Page *page) {
	if (cols > LAYOUT_COLS_MAX) cols = LAYOUT_COLS_MAX;
	if (rows > LAYOUT_ROWS_MAX) rows = LAYOUT_ROWS_MAX;
	if (cols < 4) cols = 4;
	if (rows < 1) rows = 1;

	clear_page(page);
	page->start = start;

	State s;
	memset(&s, 0, sizeof s);
	s.buf = buf;
	s.len = len;
	s.cols = cols;
	s.rows = rows;
	s.style = *style;
	s.page = page;
	s.block = (BlockKind)start.block;
	s.level = start.level;
	s.first_line = !start.continuation;
	s.prev_block = s.block;

	size_t pos = start.offset;
	int skip = start.skip;
	bool full = false;

	while (pos < len && !full) {
		const uint8_t *r = buf + pos;
		size_t size = record_size(r);
		if (size == 0 || pos + size > len) break; // damaged buffer: stop

		switch (r[0]) {
		case REC_BLOCK: {
			finish_line(&s);
			BlockKind kind = (BlockKind)r[1];
			// Blank lines before this block: around headings, and between paragraphs.
			int gap = 0;
			if (s.prev_block == BLOCK_HEADING || kind == BLOCK_HEADING) gap = 1;
			else if (kind == BLOCK_PARAGRAPH || kind == BLOCK_QUOTE || kind == BLOCK_RULE) gap = s.style.paragraph_gap;
			else if (kind == BLOCK_LIST_ITEM && s.prev_block != BLOCK_LIST_ITEM) gap = s.style.paragraph_gap;
			if (s.pending_blank < gap) s.pending_blank = gap;
			s.block = kind;
			s.level = r[2];
			s.first_line = true;
			s.prev_block = kind;

			if (kind == BLOCK_RULE) {
				if (!start_line(&s, (uint32_t)pos, 0)) {
					full = true;
					break;
				}
				memset(s.cur, '-', (size_t)cols);
				s.cur_len = cols;
				s.cur_text = cols;
				s.block = BLOCK_PARAGRAPH; // do not centre it
				finish_line(&s);
				s.first_line = true;
			}
			break;
		}
		case REC_BREAK:
			if (s.cur_text == 0) {
				if (s.pending_blank < 2) s.pending_blank++; // <br><br> is a blank line
			} else {
				finish_line(&s);
			}
			break;
		case REC_IMAGE:
			finish_line(&s);
			if (page->line_count == 0) {
				// The picture fills the page. The page after it starts with the next record.
				page->picture = true;
				page->picture_offset = (uint32_t)pos;
				page->next.offset = (uint32_t)(pos + size);
				page->next.skip = 0;
				page->next.block = BLOCK_PARAGRAPH;
				page->next.level = 0;
				page->next.continuation = false;
				page->at_end = pos + size >= len;
				return;
			}
			// Text came first: that page ends here and the picture starts the next one.
			page->next.offset = (uint32_t)pos;
			page->next.skip = 0;
			page->next.block = (uint8_t)s.block;
			page->next.level = (uint8_t)s.level;
			page->next.continuation = !s.first_line;
			return;
		case REC_WORD: {
			uint8_t glyphs[GLYPHS_MAX];
			int total = to_glyphs(r + 3, r[2], glyphs);
			if (skip > total) skip = total;
			if (!place_word(&s, (uint32_t)pos, glyphs, total, skip, (r[1] & REC_FLAG_SPACE) != 0)) {
				full = true;
			}
			skip = 0;
			break;
		}
		}
		if (full) break;
		pos += size;
	}

	if (full) {
		// The page filled up while starting a line: `next` was set there. The
		// line being built (if any) is complete, so push it first.
		finish_line(&s);
		return;
	}

	finish_line(&s);
	page->next.offset = (uint32_t)len;
	page->next.skip = 0;
	page->next.block = (uint8_t)s.block;
	page->next.level = (uint8_t)s.level;
	page->next.continuation = !s.first_line;
	page->at_end = true;
}
