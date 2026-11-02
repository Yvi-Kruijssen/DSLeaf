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
	case SETTING_LINE:   return "Line spacing";
	case SETTING_ORIENTATION: return "Orientation";
	case SETTING_GAP:    return "Para spacing";
	case SETTING_INDENT: return "Indent";
	case SETTING_MARGIN: return "Hori. margin";
	case SETTING_VMARGIN: return "Vert. margin";
	case SETTING_FOOTER: return "Progress";
	case SETTING_PREVIEW: return "Preview page";
	case SETTING_RESET:  return "Reset all";
	default:             return "";
	}
}

// -1 if the font in the config is not in the list.
static int font_index(const SettingsMenu *m, const Config *c) {
	for (int i = 0; i < m->font_count; i++) {
		if (strcmp(m->fonts[i].name, c->font) == 0) return i;
	}
	// The default font is not on the card: the book uses the built-in font, so show that.
	if (strcmp(c->font, CONFIG_FONT_DEFAULT) == 0) return 0;
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
	case SETTING_LINE:   plural(out, cap, c->line_spacing, "px", "px"); break;
	case SETTING_FOOTER:
		snprintf(out, cap, "%s", c->footer == FOOTER_PAGE ? "Page count" : c->footer == FOOTER_PERCENT ? "Percentage" : c->footer == FOOTER_NONE ? "Nothing" : "Both");
		break;
	case SETTING_VMARGIN: snprintf(out, cap, "%d px", config_vmargin_px(c)); break;
	case SETTING_PREVIEW: snprintf(out, cap, "Press A"); break;
	case SETTING_RESET:  snprintf(out, cap, "%s", m->confirm_reset ? "Sure? Press A" : "Press A"); break;
	default:             if (cap > 0) out[0] = '\0'; break;
	}
}

void settings_help(Setting s, const char **line1, const char **line2) {
	switch (s) {
	case SETTING_SCREEN:
		*line1 = "Dual: both screens.";
		*line2 = "Single: bottom. Next book.";
		break;
	case SETTING_THEME:
		*line1 = "Colours of menus and book.";
		*line2 = "Changes at once.";
		break;
	case SETTING_FONT:
		*line1 = "Font for the book text.";
		*line2 = "Add fonts to /DSLeaf/Fonts.";
		break;
	case SETTING_ORIENTATION:
		*line1 = "Hold the device sideways to";
		*line2 = "Used when a book opens.";
		break;
	case SETTING_GAP:
		*line1 = "Blank lines between paras.";
		*line2 = "Used when a book is opened.";
		break;
	case SETTING_INDENT:
		*line1 = "Space starting a paragraph.";
		*line2 = "Used when a book is opened.";
		break;
	case SETTING_MARGIN:
		*line1 = "Empty columns at each side.";
		*line2 = "Used when a book is opened.";
		break;
	case SETTING_VMARGIN:
		*line1 = "Space above and below the";
		*line2 = "text. Used with a new book.";
		break;
	case SETTING_LINE:
		*line1 = "Extra pixels between lines.";
		*line2 = "Pixel fonts. Next book.";
		break;
	case SETTING_FOOTER:
		*line1 = "Page count and percentage";
		*line2 = "in the footer. Next book.";
		break;
	case SETTING_PREVIEW:
		*line1 = "Sample page, your settings.";
		*line2 = "Any key goes back.";
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
	// The two buttons below the list are side by side (decision 060): Left and Right move between them,
	// Up goes back to the last setting, Down has nowhere to go, and A presses.
	if (m->selected == SETTING_PREVIEW || m->selected == SETTING_RESET) {
		if (actions & ACT_UP) {
			m->selected = SETTING_PREVIEW - 1;
			m->redraw = true;
		} else if (actions & ACT_LEFT) {
			if (m->selected == SETTING_RESET) {
				m->selected = SETTING_PREVIEW;
				m->redraw = true;
			}
		} else if (actions & ACT_RIGHT) {
			if (m->selected == SETTING_PREVIEW) {
				m->selected = SETTING_RESET;
				m->redraw = true;
			}
		} else if (actions & ACT_CONFIRM) {
			if (m->selected == SETTING_PREVIEW) {
				e.kind = SETTINGS_EVENT_PREVIEW;
				e.setting = SETTING_PREVIEW;
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
