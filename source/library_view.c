// The Library on paper (decision 059): the same panel, title and ruled lines as the main menu.
// Each book has two text rows (one line of paper), so 9 fit.
#include "library.h"

#include <stdio.h>
#include <string.h>

#include "paper.h"

#define TEXT_END_COL 30 // the names stop before the panel edge
#define PANEL_H 153        // the panel is y 15 to 167
#define HINT_Y 169         // below the panel, ending at y 176
#define ERROR_TEXT " - error"

bool library_draw(Library *lib, Screen screen) {
	if (!lib->redraw) return false;
	lib->redraw = false;

	render_clear(screen);
	paper_begin(screen, PANEL_H);
	paper_title(screen, "Library");
	for (int k = 0; k < LIBRARY_ROWS; k++) paper_rule(screen, LIBRARY_FIRST_ROW + LIBRARY_ROW_STEP * k);

	if (lib->unreadable) {
		render_puts(screen, 3, LIBRARY_FIRST_ROW, "Cannot read the Books folder", STYLE_ERROR);
		render_puts(screen, 3, LIBRARY_FIRST_ROW + LIBRARY_ROW_STEP, "Is the SD card inserted?", STYLE_DIM);
	} else if (lib->count == 0) {
		render_puts(screen, 3, LIBRARY_FIRST_ROW, "No EPUB files found", STYLE_DIM);
		render_puts(screen, 3, LIBRARY_FIRST_ROW + LIBRARY_ROW_STEP, "Put .epub files in /Books", STYLE_DIM);
	}

	for (int k = 0; k < LIBRARY_ROWS; k++) {
		int i = lib->top + k;
		if (i >= lib->count) break;

		const LibraryEntry *e = &lib->entry[i];
		bool selected = i == lib->selected;
		int y = LIBRARY_FIRST_ROW + LIBRARY_ROW_STEP * k;

		// Show the name without .epub. Leave room for " - error" when it applies.
		char name[LIBRARY_NAME_MAX];
		size_t len = strlen(e->name) - 5;
		memcpy(name, e->name, len);
		name[len] = '\0';

		if (selected) paper_arrow(screen, y);
		int end = e->failed ? TEXT_END_COL - (int)strlen(ERROR_TEXT) : TEXT_END_COL;
		int col = render_puts_utf8_clip(screen, 3, y, name, selected ? STYLE_ACCENT : STYLE_NORMAL, end);
		if (e->failed) render_puts(screen, col, y, ERROR_TEXT, STYLE_ERROR);
	}

	// Below the panel: key hints, the position in the list and a note when files were left out.
	paper_hint(screen, 12, HINT_Y, "A open  B back");
	if (lib->incomplete) paper_hint(screen, 12 + 15 * 8, HINT_Y, "+more");
	if (lib->count > 0) {
		char pos[16];
		int n = snprintf(pos, sizeof pos, "%d/%d", lib->selected + 1, lib->count);
		paper_hint(screen, 244 - n * 8, HINT_Y, pos);
	}
	return true;
}
