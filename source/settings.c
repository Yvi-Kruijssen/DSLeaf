#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "font_builtin.h"
#include "input.h"

void settings_open(SettingsMenu *m) {
	memset(m, 0, sizeof *m);
	m->font_count = font_list(m->fonts, FONT_CHOICES_MAX);
	m->redraw = true;
}

const char *settings_label(Setting s) {
	switch (s) {
	case SETTING_SCREEN: return "Screen mode";
	case SETTING_THEME:  return "Theme";
	case SETTING_FONT:   return "Font";
	case SETTING_LINE:   return "Line spacing";
	case SETTING_ORIENTATION: return "Orientation";
	case SETTING_GAP:    return "Para spacing";
	case SETTING_HYPHEN: return "Hyphenation";
	case SETTING_INDENT: return "Indent";
	case SETTING_MARGIN: return "Hori. margin";
	case SETTING_VMARGIN: return "Vert. margin";
	case SETTING_FOOTER: return "Book progress";
	case SETTING_PREVIEW: return "Preview page";
	case SETTING_RESET:  return "Reset all";
	case SETTING_ABOUT:  return "About";
	default:             return "";
	}
}

// -1 if the font in the config is not in the list.
static int font_index(const SettingsMenu *m, const Config *c) {
	for (int i = 0; i < m->font_count; i++) {
		if (strcmp(m->fonts[i].name, c->font) == 0) return i;
	}
	return -1;
}

void settings_font_size(const SettingsMenu *m, const Config *c, int *width, int *height) {
	int i = font_index(m, c);
	*width = i >= 0 ? m->fonts[i].width : 8;
	*height = i >= 0 ? m->fonts[i].height : 8;
}

static int wrap(int value, int delta, int count) {
	return ((value + delta) % count + count) % count;
}

static void plural(char *out, size_t cap, int n, const char *one, const char *many) {
	if (n == 0) snprintf(out, cap, "None");
	else snprintf(out, cap, "%d %s", n, n == 1 ? one : many);
}

void settings_value_text(const SettingsMenu *m, const Config *c, Setting s, char *out, size_t cap) {
	switch (s) {
	case SETTING_SCREEN:
		snprintf(out, cap, "%s", c->display == DISPLAY_SINGLE ? "Single" : "Dual");
		break;
	case SETTING_THEME:
		snprintf(out, cap, "%s", c->theme == THEME_DARK ? "Dark" : c->theme == THEME_SEPIA ? "Sepia" : "Normal");
		break;
	case SETTING_FONT: {
		int i = font_index(m, c);
		if (i < 0) {
			snprintf(out, cap, "(missing)");
		} else if (i == 0) {
			snprintf(out, cap, "Built-in 8x8");
		} else if (strcmp(m->fonts[i].name, FONT_NAME_6X12) == 0) {
			snprintf(out, cap, "Built-in 6x12");
		} else {
			snprintf(out, cap, "%s", m->fonts[i].name);
			size_t n = strlen(out);
			if (n > 4 && strcasecmp(out + n - 4, ".fnt") == 0) out[n - 4] = '\0';
		}
		break;
	}
	case SETTING_ORIENTATION: snprintf(out, cap, "%s", c->rotate ? "Rotated" : "Normal"); break;
	case SETTING_GAP:    plural(out, cap, c->paragraph_gap, "line", "lines"); break;
	case SETTING_INDENT: plural(out, cap, c->paragraph_indent, "col", "cols"); break;
	case SETTING_MARGIN: plural(out, cap, c->margin, "col", "cols"); break;
	case SETTING_LINE:   plural(out, cap, c->line_spacing, "px", "px"); break;
	case SETTING_FOOTER:
		snprintf(out, cap, "%s", c->footer == FOOTER_PAGE ? "Pages" : c->footer == FOOTER_PERCENT ? "Percent" : c->footer == FOOTER_NONE ? "Nothing" : "Both");
		break;
	case SETTING_VMARGIN: snprintf(out, cap, "%d px", config_vmargin_px(c)); break;
	case SETTING_HYPHEN: snprintf(out, cap, "%s", c->hyphenation ? "On" : "Off"); break;
	case SETTING_PREVIEW: snprintf(out, cap, "Press A"); break;
	case SETTING_ABOUT:  snprintf(out, cap, "Press A"); break;
	case SETTING_RESET:  snprintf(out, cap, "%s", m->confirm_reset ? "Sure? Press A" : "Press A"); break;
	default:             if (cap > 0) out[0] = '\0'; break;
	}
}

void settings_help(Setting s, const char **line1, const char **line2) {
	switch (s) {
	case SETTING_SCREEN:
		*line1 = "Dual: both. Single: bottom.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_THEME:
		*line1 = "Colours of menus and book.";
		*line2 = "Changes at once.";
		break;
	case SETTING_FONT:
		*line1 = "Font for the book text.";
		*line2 = "Files go in DSLeaf/Fonts.";
		break;
	case SETTING_ORIENTATION:
		*line1 = "Read with the DS sideways.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_GAP:
		*line1 = "Blank lines between paras.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_INDENT:
		*line1 = "First-line indent, in cols.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_MARGIN:
		*line1 = "Empty columns at each side.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_VMARGIN:
		*line1 = "Space above and below text.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_LINE:
		*line1 = "Extra pixels between lines.";
		*line2 = "Pixel fonts only.";
		break;
	case SETTING_FOOTER:
		*line1 = "What the footer shows.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_PREVIEW:
		*line1 = "Sample page, your settings.";
		*line2 = "Any key goes back.";
		break;
	case SETTING_HYPHEN:
		*line1 = "Hyphenate English words.";
		*line2 = "Applies when a book opens.";
		break;
	case SETTING_ABOUT:
		*line1 = "What DSLeaf is, its licence";
		*line2 = "and the font notices.";
		break;
	case SETTING_RESET:
		*line1 = "Every setting back to its";
		*line2 = "default. Press A twice.";
		break;
	default:
		*line1 = *line2 = "";
		break;
	}
}

// Moves a setting forward (delta 1) or back (delta -1).
static void change(SettingsMenu *m, Config *c, Setting s, int delta) {
	switch (s) {
	case SETTING_SCREEN:
		c->display = c->display == DISPLAY_DUAL ? DISPLAY_SINGLE : DISPLAY_DUAL;
		break;
	case SETTING_THEME:
		c->theme = (Theme)wrap((int)c->theme, delta, THEME_COUNT);
		break;
	case SETTING_FONT: {
		int i = font_index(m, c);
		if (i < 0) i = delta > 0 ? -1 : 0; // a missing font: carry on from either end of the list
		snprintf(c->font, sizeof c->font, "%s", m->fonts[wrap(i, delta, m->font_count)].name);
		break;
	}
	case SETTING_ORIENTATION: c->rotate = !c->rotate; break;
	case SETTING_GAP:    c->paragraph_gap = (uint8_t)wrap(c->paragraph_gap, delta, CONFIG_GAP_MAX + 1); break;
	case SETTING_HYPHEN: c->hyphenation = !c->hyphenation; break;
	case SETTING_INDENT: c->paragraph_indent = (uint8_t)wrap(c->paragraph_indent, delta, CONFIG_INDENT_MAX + 1); break;
	case SETTING_MARGIN: c->margin = (uint8_t)wrap(c->margin, delta, CONFIG_MARGIN_MAX + 1); break;
	case SETTING_LINE:   c->line_spacing = (uint8_t)wrap(c->line_spacing, delta, CONFIG_LINE_MAX + 1); break;
	case SETTING_FOOTER: c->footer = (FooterInfo)wrap((int)c->footer, delta, FOOTER_COUNT); break;
	case SETTING_VMARGIN: c->vmargin = (uint8_t)wrap(c->vmargin, delta, CONFIG_VMARGIN_MAX + 1); break;
	default: break;
	}
}

SettingsEvent settings_update(SettingsMenu *m, Config *c, uint32_t actions) {
	SettingsEvent e = { SETTINGS_EVENT_NONE, SETTING_SCREEN };

	if (actions == 0) return e; // an idle frame keeps the question open

	bool armed = m->confirm_reset;
	m->confirm_reset = false; // any key but a second A on Reset cancels it
	if (armed) m->redraw = true;

	if (actions & ACT_BACK) {
		e.kind = SETTINGS_EVENT_BACK;
		return e;
	}
	// The three buttons below the list are side by side (decision 060 and 077): Left and Right move between
	// them, Up goes back to the last setting, Down has nowhere to go, and A presses.
	if (m->selected >= SETTING_PREVIEW) {
		if (actions & ACT_UP) {
			m->selected = SETTING_PREVIEW - 1;
			m->redraw = true;
		} else if (actions & ACT_LEFT) {
			if (m->selected > SETTING_PREVIEW) {
				m->selected--;
				m->redraw = true;
			}
		} else if (actions & ACT_RIGHT) {
			if (m->selected < SETTING_ABOUT) {
				m->selected++;
				m->redraw = true;
			}
		} else if (actions & ACT_CONFIRM) {
			if (m->selected == SETTING_PREVIEW) {
				e.kind = SETTINGS_EVENT_PREVIEW;
				e.setting = SETTING_PREVIEW;
			} else if (m->selected == SETTING_ABOUT) {
				e.kind = SETTINGS_EVENT_ABOUT;
				e.setting = SETTING_ABOUT;
			} else if (armed) {
				config_set_defaults(c);
				e.kind = SETTINGS_EVENT_CHANGED;
				e.setting = SETTING_RESET;
				m->redraw = true;
			} else {
				m->confirm_reset = true;
				m->redraw = true;
			}
		}
		return e;
	}
	if (actions & ACT_UP) {
		if (m->selected > 0) {
			m->selected--;
			m->redraw = true;
		}
	} else if (actions & ACT_DOWN) {
		if (m->selected < SETTING_COUNT - 1) {
			m->selected++;
			m->redraw = true;
		}
	} else if (actions & (ACT_LEFT | ACT_RIGHT | ACT_CONFIRM)) {
		change(m, c, (Setting)m->selected, (actions & ACT_LEFT) ? -1 : 1);
		e.kind = SETTINGS_EVENT_CHANGED;
		e.setting = (Setting)m->selected;
		m->redraw = true;
	}
	return e;
}

uint32_t settings_touch(SettingsMenu *m, const Config *c, int x, int y) {
	if (y >= SETTINGS_BACK_Y) return ACT_BACK;

	if (y >= SETTINGS_BUTTON_Y && y < SETTINGS_BUTTON_Y + SETTINGS_BUTTON_H) {
		int button = -1;
		if (x >= SETTINGS_BUTTON_X0 && x < SETTINGS_BUTTON_X0 + SETTINGS_BUTTON_W) button = SETTING_PREVIEW;
		else if (x >= SETTINGS_BUTTON_X1 && x < SETTINGS_BUTTON_X1 + SETTINGS_BUTTON_W) button = SETTING_RESET;
		else if (x >= SETTINGS_BUTTON_X2 && x < SETTINGS_BUTTON_X2 + SETTINGS_BUTTON_W) button = SETTING_ABOUT;
		if (button < 0) return ACT_NONE;
		if (m->selected != button) {
			m->confirm_reset = false;
			m->selected = button;
			m->redraw = true;
		}
		return ACT_CONFIRM; // a button is pressed by touching it; Reset still asks twice
	}

	int row = y / 8 - SETTINGS_FIRST_ROW;
	if (row < 0 || row >= SETTING_PREVIEW) return ACT_NONE;
	if (m->selected != row) {
		m->confirm_reset = false;
		m->selected = row;
		m->redraw = true;
		return ACT_NONE;
	}
	// The selected row: the left half of the value goes back, the right half forward.
	char value[48];
	settings_value_text(m, c, (Setting)row, value, sizeof value);
	int chars = (int)strlen(value);
	if (chars > 14) chars = 14;
	int width = (chars + 4) * 8; // "< value >"
	int start = (SETTINGS_VALUE_END + 1) * 8 - width;
	return x < start + width / 2 ? ACT_LEFT : ACT_RIGHT;
}
