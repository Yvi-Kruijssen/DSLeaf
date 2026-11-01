#include "preview.h"

#include <string.h>

// '#' marks a heading, every other line is a paragraph. Public domain.
static const char *const sample[] = {
	"#CHAPTER I",
	"It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in want of a wife.",
	"However little known the feelings or views of such a man may be on his first entering a neighbourhood, this truth is so well fixed in the minds of the surrounding families, that he is considered the rightful property of some one or other of their daughters.",
	"\"My dear Mr. Bennet,\" said his lady to him one day, \"have you heard that Netherfield Park is let at last?\"",
	"Mr. Bennet replied that he had not.",
	"\"But it is,\" returned she; \"for Mrs. Long has just been here, and she told me all about it.\"",
	"Mr. Bennet made no answer.",
	"\"Do you not want to know who has taken it?\" cried his wife impatiently.",
	"\"You want to tell me, and I have no objection to hearing it.\"",
	"This was invitation enough.",
	"\"Why, my dear, you must know, Mrs. Long says that Netherfield is taken by a young man of large fortune from the north of England; that he came down on Monday in a chaise and four to see the place, and was so much delighted with it, that he agreed with Mr. Morris immediately; that he is to take possession before Michaelmas, and some of his servants are to be in the house by the end of next week.\"",
	"\"What is his name?\"",
	"\"Bingley.\"",
	"\"Is he married or single?\"",
	"\"Oh! Single, my dear, to be sure! A single man of large fortune; four or five thousand a year. What a fine thing for our girls!\"",
};

static uint8_t records[4096];

static size_t build_records(void) {
	size_t len = 0;
	for (size_t i = 0; i < sizeof sample / sizeof sample[0]; i++) {
		const char *s = sample[i];
		TextItem item;
		memset(&item, 0, sizeof item);
		item.kind = TEXT_BLOCK;
		item.block = s[0] == '#' ? BLOCK_HEADING : BLOCK_PARAGRAPH;
		item.level = s[0] == '#' ? 1 : 0;
		if (s[0] == '#') s++;
		size_t n = record_append(records, len, sizeof records, &item);
		if (n == 0) break;
		len += n;

		bool first = true;
		while (*s) {
			while (*s == ' ') s++;
			size_t w = 0;
			while (s[w] && s[w] != ' ') w++;
			if (w == 0) break;
			memset(&item, 0, sizeof item);
			item.kind = TEXT_WORD;
			item.space_before = !first;
			if (w >= sizeof item.text) w = sizeof item.text - 1;
			memcpy(item.text, s, w);
			n = record_append(records, len, sizeof records, &item);
			if (n == 0) return len;
			len += n;
			first = false;
			s += w;
		}
	}
	return len;
}

void preview_build(Preview *p, int cols, int rows, int pages, const LayoutStyle *style) {
	memset(p, 0, sizeof *p);
	p->cols = cols < 1 ? 1 : cols > LAYOUT_COLS_MAX ? LAYOUT_COLS_MAX : cols;
	p->rows = rows < 1 ? 1 : rows > LAYOUT_ROWS_MAX ? LAYOUT_ROWS_MAX : rows;
	p->pages = pages < 2 ? 1 : 2;

	size_t len = build_records();
	PageStart start = layout_first_page();
	bool ended = false;
	for (int i = 0; i < p->pages; i++) {
		if (ended) {
			p->page[i].at_end = true; // nothing left: an empty page
			continue;
		}
		layout_page(records, len, start, p->cols, p->rows, style, &p->page[i]);
		if (p->page[i].at_end) ended = true;
		start = p->page[i].next;
	}
}
