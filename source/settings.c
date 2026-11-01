#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

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
	case SETTING_ORIENTATION: return "Orientation";
	case SETTING_GAP:    return "Para spacing";
	case SETTING_INDENT: return "Indent";
	case SETTING_MARGIN: return "Margins";
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
	default:             if (cap > 0) out[0] = '\0'; break;
	}
}

void settings_help(Setting s, const char **line1, const char **line2) {
	switch (s) {
	case SETTING_SCREEN:
		*line1 = "Dual uses both screens, single";
		*line2 = "only the bottom. Next book.";
		break;
	case SETTING_THEME:
		*line1 = "Colours of the menus and book.";
		*line2 = "Changes at once.";
		break;
	case SETTING_FONT:
		*line1 = "Font for the book text.";
		*line2 = "Add fonts to /DSLeaf/Fonts.";
		break;
	case SETTING_ORIENTATION:
		*line1 = "Hold the device sideways to";
		*line2 = "read. Used when a book opens.";
		break;
	case SETTING_GAP:
		*line1 = "Blank lines between paragraphs";
		*line2 = "Used when a book is opened.";
		break;
	case SETTING_INDENT:
		*line1 = "Space starting a paragraph.";
		*line2 = "Used when a book is opened.";
		break;
	case SETTING_MARGIN:
		*line1 = "Empty columns left and right.";
		*line2 = "Used when a book is opened.";
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
	case SETTING_INDENT: c->paragraph_indent = (uint8_t)wrap(c->paragraph_indent, delta, CONFIG_INDENT_MAX + 1); break;
	case SETTING_MARGIN: c->margin = (uint8_t)wrap(c->margin, delta, CONFIG_MARGIN_MAX + 1); break;
	default: break;
	}
}

SettingsEvent settings_update(SettingsMenu *m, Config *c, uint32_t actions) {
	SettingsEvent e = { SETTINGS_EVENT_NONE, SETTING_SCREEN };

	if (actions & ACT_BACK) {
		e.kind = SETTINGS_EVENT_BACK;
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
