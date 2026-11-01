#include "library.h"

#include <stdio.h>
#include <string.h>

#define TEXT_END_COL 31
#define ERROR_TEXT " - error"

bool library_draw(Library *lib, Screen screen) {
	if (!lib->redraw) return false;
	lib->redraw = false;

	render_clear(screen);

	const char *title = "Library";
	render_puts(screen, (32 - (int)strlen(title)) / 2, 1, title, STYLE_HIGHLIGHT);
	for (int c = 1; c < 31; c++) render_putc(screen, c, 2, '-', STYLE_HIGHLIGHT);

	if (lib->unreadable) {
		render_puts(screen, 2, LIBRARY_FIRST_ROW, "Cannot read the Books folder", STYLE_ERROR);
		render_puts(screen, 2, LIBRARY_FIRST_ROW + 1, "Is the SD card inserted?", STYLE_DIM);
	} else if (lib->count == 0) {
		render_puts(screen, 2, LIBRARY_FIRST_ROW, "No EPUB files found", STYLE_DIM);
		render_puts(screen, 2, LIBRARY_FIRST_ROW + 1, "Put .epub files in /Books", STYLE_DIM);
	}

	for (int row = 0; row < LIBRARY_ROWS; row++) {
		int i = lib->top + row;
		if (i >= lib->count) break;

		const LibraryEntry *e = &lib->entry[i];
		bool selected = i == lib->selected;
		Style style = selected ? STYLE_HIGHLIGHT : STYLE_NORMAL;
		int y = LIBRARY_FIRST_ROW + row;

		// Show the name without .epub. Leave room for " - error" when it applies.
		char name[LIBRARY_NAME_MAX];
		size_t len = strlen(e->name) - 5;
		memcpy(name, e->name, len);
		name[len] = '\0';

		if (selected) render_putc(screen, 1, y, '>', STYLE_HIGHLIGHT);
		int end = e->failed ? TEXT_END_COL - (int)strlen(ERROR_TEXT) : TEXT_END_COL;
		int col = render_puts_utf8_clip(screen, 3, y, name, style, end);
		if (e->failed) render_puts(screen, col, y, ERROR_TEXT, STYLE_ERROR);
	}

	// Footer: key hints and the position in the list.
	render_puts(screen, 1, LIBRARY_FOOTER_ROW, "A open  B back", STYLE_DIM);
	if (lib->count > 0) {
		char pos[16];
		int n = snprintf(pos, sizeof pos, "%d/%d", lib->selected + 1, lib->count);
		render_puts(screen, 31 - n, LIBRARY_FOOTER_ROW, pos, STYLE_DIM);
	}
	if (lib->incomplete) render_puts(screen, 17, LIBRARY_FOOTER_ROW, "+more", STYLE_ERROR);
	return true;
}
