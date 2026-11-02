// The Settings screen on paper (decisions 059 and 060): the panel and title of the main menu, a band
// and arrow for the selected setting, the two actions (sample page, reset) as small buttons side by side, and the
// help text under a line. One text row for each setting (no ruled lines: a text row is the
// height of the line itself).
#include <stdio.h>
#include <string.h>

#include "paper.h"
#include "reader.h"
#include "render.h"
#include "settings.h"

#define FIRST_ROW SETTINGS_FIRST_ROW
#define VALUE_END SETTINGS_VALUE_END // the value ends at this column (x 239, inside the panel)
#define PANEL_H 149      // the panel is y 15 to 163
#define BUTTON_W SETTINGS_BUTTON_W // three buttons side by side
#define BUTTON_H SETTINGS_BUTTON_H
#define BUTTON_Y SETTINGS_BUTTON_Y
#define PREVIEW_X SETTINGS_BUTTON_X0
#define RESET_X SETTINGS_BUTTON_X1
#define ABOUT_X SETTINGS_BUTTON_X2
#define HELP_RULE_Y 143
#define HELP_ROW 18      // two lines: the help, then the second help line (or the size or a note)
#define HINT_Y 167       // below the panel, ending at y 174

bool settings_draw(SettingsMenu *m, const Config *c, Screen screen) {
	if (!m->redraw) return false;
	m->redraw = false;

	render_clear(screen);
	paper_begin(screen, PANEL_H);
	paper_title(screen, "Settings");

	// The settings. Preview and Reset are buttons below the list.
	for (int i = 0; i < SETTING_COUNT; i++) {
		if (i >= SETTING_PREVIEW) continue;
		int row = FIRST_ROW + i;
		bool selected = i == m->selected;
		Style style = selected ? STYLE_ACCENT : STYLE_NORMAL;

		char value[48], shown[64];
		settings_value_text(m, c, (Setting)i, value, sizeof value);
		if (strlen(value) > 14) value[14] = '\0';
		snprintf(shown, sizeof shown, "< %s >", value);

		if (selected) {
			paper_band(screen, row);
			paper_arrow(screen, row);
		}
		render_puts(screen, 3, row, settings_label((Setting)i), style);
		render_puts(screen, VALUE_END - (int)strlen(shown) + 1, row, shown, style);
	}
	paper_button(screen, PREVIEW_X, BUTTON_Y, BUTTON_W, BUTTON_H, "Preview", m->selected == SETTING_PREVIEW, false, false);
	paper_button(screen, RESET_X, BUTTON_Y, BUTTON_W, BUTTON_H, m->confirm_reset ? "Sure? A" : "Reset",
	             m->selected == SETTING_RESET, false, false);
	paper_button(screen, ABOUT_X, BUTTON_Y, BUTTON_W, BUTTON_H, "About", m->selected == SETTING_ABOUT, false, false);

	// What the selected setting does, under a line.
	render_backdrop_rect(screen, PAPER_RULE_X, HELP_RULE_Y, PAPER_RULE_W, 1, BD_RULE);
	const char *line1, *line2;
	settings_help((Setting)m->selected, &line1, &line2);
	render_puts(screen, 2, HELP_ROW, line1, STYLE_DIM);

	if (m->selected == SETTING_FONT || m->selected == SETTING_LINE) {
		// The second line is how much text a page of this font holds, with the margins and spacing as set now.
		int w, h;
		settings_font_size(m, c, &w, &h);
		Font size = { .width = w, .height = h, .line_gap = c->line_spacing, .safe = config_vmargin_px(c) };
		if (m->selected == SETTING_LINE && font_is_tile(&size)) {
			render_puts(screen, 2, HELP_ROW + 1, "This font ignores spacing.", STYLE_NORMAL);
		} else {
			int cols, rows;
			reader_geometry(&size, c->margin, c->rotate, &cols, &rows);
			char info[40];
			snprintf(info, sizeof info, "Size %dx%d, page %dx%d", w, h, cols, rows);
			render_puts(screen, 2, HELP_ROW + 1, info, STYLE_NORMAL);
		}
	} else {
		render_puts(screen, 2, HELP_ROW + 1, line2, STYLE_DIM);
	}

	paper_hint(screen, 12, HINT_Y, "Left/Right change  B back");
	return true;
}
