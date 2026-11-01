#include <stdio.h>
#include <string.h>

#include "bookmark_menu.h"

#define BOX_MAX_COLS 36
#define BOX_MAX_ITEMS 12
#define BOX_FRAME_ROWS 4 // top edge, title, hint, bottom edge

int bookmark_menu_visible(void) {
	int rows = render_rows() - 2 - BOX_FRAME_ROWS; // leave a row free above and below
	if (rows > BOX_MAX_ITEMS) rows = BOX_MAX_ITEMS;
	return rows < 1 ? 1 : rows;
}

static void put_edge(Screen screen, int col, int row, int width, char left, char fill) {
	render_putc(screen, col, row, (uint8_t)left, STYLE_NORMAL);
	for (int x = 1; x < width - 1; x++) render_putc(screen, col + x, row, (uint8_t)fill, STYLE_NORMAL);
	render_putc(screen, col + width - 1, row, (uint8_t)left, STYLE_NORMAL);
}

static void put_blank_row(Screen screen, int col, int row, int width) {
	render_putc(screen, col, row, '|', STYLE_NORMAL);
	for (int x = 1; x < width - 1; x++) render_putc(screen, col + x, row, ' ', STYLE_NORMAL);
	render_putc(screen, col + width - 1, row, '|', STYLE_NORMAL);
}

// Draws the frame, the tab names and the footer, and returns where the list goes.
typedef struct {
	int col, row, width, height, text_col, text_end;
} Box;

static Box draw_box(Screen screen, ReadingTab tab, int bookmark_count, int shown) {
	Box b;
	b.width = render_cols() - 2;
	if (b.width > BOX_MAX_COLS) b.width = BOX_MAX_COLS;
	b.height = shown + BOX_FRAME_ROWS;
	b.col = (render_cols() - b.width) / 2;
	b.row = (render_rows() - b.height) / 2;
	b.text_col = b.col + 2;
	b.text_end = b.col + b.width - 1;

	put_edge(screen, b.col, b.row, b.width, '+', '-');
	for (int y = 1; y < b.height - 1; y++) put_blank_row(screen, b.col, b.row + y, b.width);
	put_edge(screen, b.col, b.row + b.height - 1, b.width, '+', '-');

	// The tab names: the one in use is highlighted. A narrow box (a rotated page)
	// shows only that one, with arrows to say there is another.
	int inner = b.text_end - b.text_col;
	char marks[24];
	snprintf(marks, sizeof marks, "Bookmarks %d/%d", bookmark_count, BOOKMARKS_MAX);
	if (reading_menu_full_header(inner)) {
		int next = render_puts(screen, b.text_col, b.row + 1, "Contents", tab == TAB_CONTENTS ? STYLE_HIGHLIGHT : STYLE_DIM);
		render_puts_utf8_clip(screen, next + 1, b.row + 1, marks, tab == TAB_BOOKMARKS ? STYLE_HIGHLIGHT : STYLE_DIM, b.text_end);
	} else {
		char one[32];
		if (tab == TAB_CONTENTS) snprintf(one, sizeof one, "<Contents>");
		else if ((int)strlen(marks) + 2 <= inner) snprintf(one, sizeof one, "<%s>", marks);
		else snprintf(one, sizeof one, "<Bookmarks>");
		render_puts_utf8_clip(screen, b.text_col, b.row + 1, one, STYLE_HIGHLIGHT, b.text_end);
	}

	render_puts_utf8_clip(screen, b.text_col, b.row + b.height - 2, reading_menu_hint(tab, inner), STYLE_DIM, b.text_end);
	return b;
}

void bookmark_menu_draw(const BookmarkMenu *m, const BookMarks *marks, int first_chapter, Screen screen) {
	int visible = bookmark_menu_visible();
	int shown = marks->count == 0 ? 2 : (marks->count < visible ? marks->count : visible);
	Box b = draw_box(screen, TAB_BOOKMARKS, marks->count, shown);

	if (marks->count == 0) {
		render_puts_utf8_clip(screen, b.text_col, b.row + 2, "No bookmarks yet.", STYLE_NORMAL, b.text_end);
		render_puts_utf8_clip(screen, b.text_col, b.row + 3, "A in a book adds one.", STYLE_DIM, b.text_end);
		return;
	}
	for (int i = 0; i < shown; i++) {
		int index = m->top + i;
		if (index >= marks->count) break;

		const Mark *k = &marks->mark[index];
		bool selected = index == m->selected;
		Style style = selected ? STYLE_HIGHLIGHT : STYLE_NORMAL;

		char line[BOOKMARKS_PATH_MAX];
		int chapter = (int)k->chapter - first_chapter + 1;
		snprintf(line, sizeof line, "Ch %d %s", chapter < 1 ? 1 : chapter, k->text[0] ? k->text : "(no text)");
		if (selected) render_putc(screen, b.col + 1, b.row + 2 + i, '>', STYLE_HIGHLIGHT);
		render_puts_utf8_clip(screen, b.text_col, b.row + 2 + i, line, style, b.text_end);
	}
}

void toc_menu_draw(const ListMenu *m, const Toc *toc, int here, int bookmark_count, Screen screen) {
	int visible = bookmark_menu_visible();
	int count = toc ? toc->count : 0;
	int shown = count == 0 ? 2 : (count < visible ? count : visible);
	Box b = draw_box(screen, TAB_CONTENTS, bookmark_count, shown);

	if (count == 0) {
		render_puts_utf8_clip(screen, b.text_col, b.row + 2, "No contents found", STYLE_NORMAL, b.text_end);
		render_puts_utf8_clip(screen, b.text_col, b.row + 3, "in this book", STYLE_DIM, b.text_end);
		return;
	}
	for (int i = 0; i < shown; i++) {
		int index = m->top + i;
		if (index >= count) break;

		const TocEntry *e = &toc->entry[index];
		bool selected = index == m->selected;
		Style style = selected ? STYLE_HIGHLIGHT : STYLE_NORMAL;
		int row = b.row + 2 + i;

		if (selected) render_putc(screen, b.col + 1, row, '>', STYLE_HIGHLIGHT);
		else if (index == here) render_putc(screen, b.col + 1, row, '*', STYLE_DIM); // where you are
		int indent = e->level > 2 ? 2 : e->level; // two columns for each level, at most two levels deep
		render_puts_utf8_clip(screen, b.text_col + indent * 2, row, e->title, style, b.text_end);
	}
}
