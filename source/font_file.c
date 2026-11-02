#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "font.h"
#include "font_builtin.h"
#include "storage.h"

#define FONT_FILE_MAX (8 + 256 * 16) // the largest a font file can be

bool font_load_named(Font *font, const char *name) {
	if (strcmp(name, FONT_NAME_6X12) == 0) {
		if (font_from_bytes(font, font_6x12_data, (size_t)font_6x12_size)) return true; // built into the program
	} else if (strcmp(name, FONT_NAME_8X8) != 0) {
		char path[160];
		uint8_t data[FONT_FILE_MAX + 1]; // one byte more than a font can be, to notice a file that is too big
		int n = snprintf(path, sizeof path, "/DSLeaf/Fonts/%s", name);
		if (n > 0 && n < (int)sizeof path) {
			int size = storage_read_file(path, data, sizeof data);
			if (size > 0 && font_from_bytes(font, data, (size_t)size)) return true;
		}
	}
	font_load_default(font);
	return strcmp(name, FONT_NAME_8X8) == 0;
}

static bool has_fnt_ending(const char *name) {
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".fnt") == 0;
}

// A name config.txt can hold: it is trimmed when read, so no spaces at the ends.
static bool storable_name(const char *name) {
	size_t n = strlen(name);
	if (n == 0 || n >= sizeof ((FontChoice *)0)->name) return false;
	if (name[0] == ' ' || name[n - 1] == ' ') return false;
	for (const char *p = name; *p; p++) {
		if ((unsigned char)*p < 0x20 || *p == '\\') return false;
	}
	return strstr(name, "..") == NULL;
}

// Fonts are collected first, then sorted, so which ones fit in a short list
// depends on their names and not on the order the card returns them in.
#define LIST_COLLECT_MAX 64

typedef struct {
	FontChoice *found;
	int count;
} ListState;

static bool list_visit(const char *name, bool is_dir, void *user) {
	ListState *s = user;
	if (is_dir || s->count >= LIST_COLLECT_MAX || !has_fnt_ending(name) || !storable_name(name)) return true;

	static Font font; // 4 KB, kept off the stack
	if (!font_load_named(&font, name)) return true; // not a usable font

	FontChoice *c = &s->found[s->count++];
	snprintf(c->name, sizeof c->name, "%s", name);
	c->width = font.width;
	c->height = font.height;
	return true;
}

static int compare_choices(const void *a, const void *b) {
	return strcasecmp(((const FontChoice *)a)->name, ((const FontChoice *)b)->name);
}

int font_list(FontChoice *out, int max) {
	if (max < 1) return 0;
	snprintf(out[0].name, sizeof out[0].name, FONT_NAME_8X8);
	out[0].width = out[0].height = 8;
	int built_in = 1;
	if (max >= 2) { // the two fonts built into the program come first
		snprintf(out[1].name, sizeof out[1].name, FONT_NAME_6X12);
		out[1].width = 6;
		out[1].height = 12;
		built_in = 2;
	}

	static FontChoice found[LIST_COLLECT_MAX];
	ListState s = { found, 0 };
	storage_list_dir("/DSLeaf/Fonts", list_visit, &s);
	if (s.count > 1) qsort(found, (size_t)s.count, sizeof found[0], compare_choices);

	int n = s.count < max - built_in ? s.count : max - built_in;
	memcpy(out + built_in, found, (size_t)n * sizeof found[0]);
	return built_in + n;
}
