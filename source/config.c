#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "storage.h"

#define FILE_CAP 1024

void config_set_defaults(Config *c) {
	c->display = DISPLAY_DUAL;
	c->invert = false;
	c->rotate = false;
	snprintf(c->font, sizeof c->font, "default");
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
	} else if (strcmp(key, "invert") == 0) {
		if (parse_bool(value, &b)) c->invert = b;
	} else if (strcmp(key, "rotate") == 0) {
		if (parse_bool(value, &b)) c->rotate = b;
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
	                 "screen_mode=%s\ninvert=%d\nfont=%s\nrotate=%d\n",
	                 c->display == DISPLAY_SINGLE ? "single" : "dual", c->invert ? 1 : 0, c->font,
	                 c->rotate ? 1 : 0);
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
