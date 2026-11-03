#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "storage.h"

#define FILE_CAP 1024

int config_vmargin_px(const Config *c) {
	return 8 * (1 + (c->vmargin > CONFIG_VMARGIN_MAX ? CONFIG_VMARGIN_DEFAULT : c->vmargin));
}

void config_set_defaults(Config *c) {
	c->display = DISPLAY_DUAL;
	c->theme = THEME_NORMAL;
	c->rotate = false;
	c->paragraph_gap = 1;    // decision 042
	c->paragraph_indent = 0;
	c->margin = 1;           // decision 026
	c->line_spacing = 1;     // decision 050
	c->footer = FOOTER_BOTH; // decision 056
	c->hyphenation = true;   // decision 093
	c->vmargin = CONFIG_VMARGIN_DEFAULT; // 16 pixels, decision 062
	snprintf(c->font, sizeof c->font, "%s", CONFIG_FONT_DEFAULT);
}

// A font name is a plain file name. Anything that could point outside the
// Fonts folder is refused.
static bool valid_font_name(const char *name) {
	if (name[0] == '\0' || strlen(name) >= CONFIG_FONT_MAX) return false;
	if (strstr(name, "..")) return false;
	for (const char *p = name; *p; p++) {
		if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20) return false;
	}
	return true;
}

static bool parse_bool(const char *value, bool *out) {
	if (strcmp(value, "0") == 0) { *out = false; return true; }
	if (strcmp(value, "1") == 0) { *out = true; return true; }
	return false;
}

// A whole number from 0 to `max`. Anything else is refused.
static bool parse_number(const char *value, int max, uint8_t *out) {
	if (value[0] < '0' || value[0] > '9' || value[1] != '\0') return false; // one digit is enough for these settings
	int n = value[0] - '0';
	if (n > max) return false;
	*out = (uint8_t)n;
	return true;
}

static char *trim(char *s) {
	while (isspace((unsigned char)*s)) s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
	return s;
}

static void apply(Config *c, const char *key, const char *value) {
	bool b;
	if (strcmp(key, "screen_mode") == 0) {
		if (strcmp(value, "single") == 0) c->display = DISPLAY_SINGLE;
		else if (strcmp(value, "dual") == 0) c->display = DISPLAY_DUAL;
	} else if (strcmp(key, "theme") == 0) {
		if (strcmp(value, "normal") == 0 || strcmp(value, "light") == 0) c->theme = THEME_NORMAL;
		else if (strcmp(value, "dark") == 0) c->theme = THEME_DARK;
		else if (strcmp(value, "sepia") == 0) c->theme = THEME_SEPIA;
	} else if (strcmp(key, "invert") == 0) { // older versions
		if (parse_bool(value, &b)) c->theme = b ? THEME_DARK : THEME_NORMAL;
	} else if (strcmp(key, "rotate") == 0) {
		if (parse_bool(value, &b)) c->rotate = b;
	} else if (strcmp(key, "paragraph_gap") == 0) {
		parse_number(value, CONFIG_GAP_MAX, &c->paragraph_gap);
	} else if (strcmp(key, "paragraph_indent") == 0) {
		parse_number(value, CONFIG_INDENT_MAX, &c->paragraph_indent);
	} else if (strcmp(key, "margin") == 0) {
		parse_number(value, CONFIG_MARGIN_MAX, &c->margin);
	} else if (strcmp(key, "hyphenation") == 0) {
		if (strcmp(value, "1") == 0) c->hyphenation = true;
		else if (strcmp(value, "0") == 0) c->hyphenation = false;
	} else if (strcmp(key, "footer") == 0) {
		if (strcmp(value, "both") == 0) c->footer = FOOTER_BOTH;
		else if (strcmp(value, "page") == 0) c->footer = FOOTER_PAGE;
		else if (strcmp(value, "percent") == 0) c->footer = FOOTER_PERCENT;
		else if (strcmp(value, "none") == 0) c->footer = FOOTER_NONE;
	} else if (strcmp(key, "vert_margin") == 0) { // in pixels
		if (strcmp(value, "8") == 0) c->vmargin = 0;
		else if (strcmp(value, "16") == 0) c->vmargin = 1;
		else if (strcmp(value, "24") == 0) c->vmargin = 2;
	} else if (strcmp(key, "line_spacing") == 0) {
		parse_number(value, CONFIG_LINE_MAX, &c->line_spacing);
	} else if (strcmp(key, "font") == 0) {
		if (valid_font_name(value)) snprintf(c->font, sizeof c->font, "%s", value);
	}
}

void config_parse(Config *c, const char *text) {
	char line[160];
	while (*text) {
		size_t n = strcspn(text, "\n");
		size_t take = n < sizeof line - 1 ? n : sizeof line - 1;
		memcpy(line, text, take);
		line[take] = '\0';
		text += n;
		if (*text == '\n') text++;

		char *s = trim(line);
		if (*s == '#' || *s == '\0') continue;
		char *eq = strchr(s, '=');
		if (!eq) continue;
		*eq = '\0';
		apply(c, trim(s), trim(eq + 1));
	}
}

int config_format(const Config *c, char *buf, size_t cap) {
	int n = snprintf(buf, cap,
	                 "screen_mode=%s\ntheme=%s\nfont=%s\nrotate=%d\nparagraph_gap=%d\nparagraph_indent=%d\nmargin=%d\nline_spacing=%d\nfooter=%s\nvert_margin=%d\nhyphenation=%d\n",
	                 c->display == DISPLAY_SINGLE ? "single" : "dual",
	                 c->theme == THEME_DARK ? "dark" : c->theme == THEME_SEPIA ? "sepia" : "normal", c->font,
	                 c->rotate ? 1 : 0, c->paragraph_gap, c->paragraph_indent, c->margin, c->line_spacing,
	                 c->footer == FOOTER_PAGE ? "page" : c->footer == FOOTER_PERCENT ? "percent" : c->footer == FOOTER_NONE ? "none" : "both",
	                 config_vmargin_px(c), c->hyphenation ? 1 : 0);
	return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

bool config_load(Config *c) {
	config_set_defaults(c);
	char text[FILE_CAP];
	if (storage_read_text(CONFIG_PATH, text, sizeof text) < 0) return false;
	config_parse(c, text);
	return true;
}

bool config_save(const Config *c) {
	char text[FILE_CAP];
	if (config_format(c, text, sizeof text) < 0) return false;
	return storage_write_text(CONFIG_PATH, text);
}
