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

void bookmark_menu_draw(const BookmarkMenu *m, const BookMarks *marks, int first_chapter, Screen screen) {
	int visible = bookmark_menu_visible();
	int shown = marks->count == 0 ? 2 : (marks->count < visible ? marks->count : visible);

	int width = render_cols() - 2;
	if (width > BOX_MAX_COLS) width = BOX_MAX_COLS;
	int height = shown + BOX_FRAME_ROWS;
	int col = (render_cols() - width) / 2;
	int row = (render_rows() - height) / 2;
	int text_col = col + 2, text_end = col + width - 1;

	put_edge(screen, col, row, width, '+', '-');
	for (int y = 1; y < height - 1; y++) put_blank_row(screen, col, row + y, width);
	put_edge(screen, col, row + height - 1, width, '+', '-');

	char title[32];
	snprintf(title, sizeof title, "Bookmarks %d/%d", marks->count, BOOKMARKS_MAX);
	render_puts(screen, text_col, row + 1, title, STYLE_HIGHLIGHT);

	if (marks->count == 0) {
		render_puts_utf8_clip(screen, text_col, row + 2, "No bookmarks yet.", STYLE_NORMAL, text_end);
		render_puts_utf8_clip(screen, text_col, row + 3, "A in a book adds one.", STYLE_DIM, text_end);
	}
	for (int i = 0; i < shown && marks->count > 0; i++) {
		int index = m->top + i;
		if (index >= marks->count) break;

		const Mark *k = &marks->mark[index];
		bool selected = index == m->selected;
		Style style = selected ? STYLE_HIGHLIGHT : STYLE_NORMAL;

		char line[BOOKMARKS_PATH_MAX];
		int chapter = (int)k->chapter - first_chapter + 1;
		snprintf(line, sizeof line, "Ch %d %s", chapter < 1 ? 1 : chapter, k->text[0] ? k->text : "(no text)");
		if (selected) render_putc(screen, col + 1, row + 2 + i, '>', STYLE_HIGHLIGHT);
		render_puts_utf8_clip(screen, text_col, row + 2 + i, line, style, text_end);
	}

	render_puts_utf8_clip(screen, text_col, row + height - 2, "A go  Sel del  B back", STYLE_DIM, text_end);
}
