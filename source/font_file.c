#include <stdio.h>
#include <string.h>

#include "font.h"
#include "storage.h"

#define FONT_FILE_MAX (8 + 256 * 16) // the largest a font file can be

bool font_load_named(Font *font, const char *name) {
	if (strcmp(name, "default") != 0) {
		char path[160];
		uint8_t data[FONT_FILE_MAX + 1]; // one byte more than a font can be, to notice a file that is too big
		int n = snprintf(path, sizeof path, "/DSLeaf/Fonts/%s", name);
		if (n > 0 && n < (int)sizeof path) {
			int size = storage_read_file(path, data, sizeof data);
			if (size > 0 && font_from_bytes(font, data, (size_t)size)) return true;
		}
	}
	font_load_default(font);
	return strcmp(name, "default") == 0;
}
