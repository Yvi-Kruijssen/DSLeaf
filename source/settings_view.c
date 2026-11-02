#include <stdio.h>
#include <string.h>

#include "reader.h"
#include "render.h"
#include "settings.h"

#define FIRST_ROW 4
#define ROW_STEP 1
#define VALUE_END 30     // the value ends at this column
#define HELP_ROW 17
#define FOOTER_ROW 23

bool settings_draw(SettingsMenu *m, const Config *c, Screen screen) {
	if (!m->redraw) return false;
	m->redraw = false;

	render_clear(screen);
	const char *title = "Settings";
	render_puts(screen, (32 - (int)strlen(title)) / 2, 1, title, STYLE_HIGHLIGHT);
	for (int x = 1; x < 31; x++) render_putc(screen, x, 2, '-', STYLE_HIGHLIGHT);

	for (int i = 0; i < SETTING_COUNT; i++) {
		int row = FIRST_ROW + i * ROW_STEP + (i == SETTING_RESET ? 1 : 0); // a blank row before Reset
		bool selected = i == m->selected;
		Style style = selected ? STYLE_HIGHLIGHT : STYLE_NORMAL;

		char value[48], shown[64];
		settings_value_text(m, c, (Setting)i, value, sizeof value);
		if (strlen(value) > 14) value[14] = '\0';
		if (i == SETTING_RESET || i == SETTING_PREVIEW) snprintf(shown, sizeof shown, "%s", value);
		else snprintf(shown, sizeof shown, "< %s >", value);

		if (selected) render_putc(screen, 1, row, '>', STYLE_HIGHLIGHT);
		render_puts(screen, 3, row, settings_label((Setting)i), style);
		render_puts(screen, VALUE_END - (int)strlen(shown) + 1, row, shown, style);
	}

	// What the selected setting does.
	const char *line1, *line2;
	settings_help((Setting)m->selected, &line1, &line2);
	render_puts(screen, 1, HELP_ROW, line1, STYLE_DIM);
	render_puts(screen, 1, HELP_ROW + 1, line2, STYLE_DIM);

	if (m->selected == SETTING_FONT || m->selected == SETTING_LINE) {
		// How much text a page of this font holds, with the margins and spacing as set now.
		int w, h;
		settings_font_size(m, c, &w, &h);
		Font size = { .width = w, .height = h, .line_gap = c->line_spacing };
		int cols, rows;
		reader_geometry(&size, c->margin, c->rotate, &cols, &rows);
		char info[40];
		snprintf(info, sizeof info, "Size %dx%d, page %dx%d", w, h, cols, rows);
		render_puts(screen, 1, HELP_ROW + 2, info, STYLE_NORMAL);
		if (m->selected == SETTING_LINE && font_is_tile(&size)) render_puts(screen, 1, HELP_ROW + 3, "This font ignores spacing.", STYLE_DIM);
	}

	render_puts(screen, 1, FOOTER_ROW, "Left/Right change  B back", STYLE_DIM);
	return true;
}
