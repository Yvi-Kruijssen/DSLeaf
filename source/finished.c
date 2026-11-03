#include "finished.h"

#include <string.h>

#include "storage.h"

int finished_load(char *text, size_t cap) {
	int n = storage_read_text(FINISHED_PATH, text, cap);
	if (n < 0) {
		text[0] = '\0';
		return 0;
	}
	return n;
}

bool finished_save(const char *text) {
	return storage_write_text(FINISHED_PATH, text);
}

// The start of the line holding `path`, or NULL.
static char *find_line(const char *text, const char *path) {
	size_t len = strlen(path);
	if (len == 0) return NULL;
	for (const char *p = text; *p;) {
		const char *end = strchr(p, '\n');
		size_t line = end ? (size_t)(end - p) : strlen(p);
		if (line == len && memcmp(p, path, len) == 0) return (char *)p;
		if (!end) break;
		p = end + 1;
	}
	return NULL;
}

bool finished_has(const char *text, const char *path) {
	return find_line(text, path) != NULL;
}

bool finished_add(char *text, size_t cap, const char *path) {
	size_t len = strlen(path);
	if (len == 0 || strchr(path, '\n') || strchr(path, '\r') || len + 2 > cap) return false;
	if (find_line(text, path)) return true;

	size_t used = strlen(text);
	// Drop the oldest lines (at the start) until the new one fits.
	while (used + len + 1 >= cap) {
		char *nl = strchr(text, '\n');
		if (!nl) {
			text[0] = '\0';
			used = 0;
			break;
		}
		size_t cut = (size_t)(nl - text) + 1;
		memmove(text, text + cut, used - cut + 1);
		used -= cut;
	}
	if (used > 0 && text[used - 1] != '\n') text[used++] = '\n'; // a file that did not end in a line break
	memcpy(text + used, path, len);
	text[used + len] = '\n';
	text[used + len + 1] = '\0';
	return true;
}

bool finished_remove(char *text, const char *path) {
	char *line = find_line(text, path);
	if (!line) return false;
	size_t len = strlen(path);
	size_t rest = strlen(line + len);
	if (line[len] == '\n') {
		memmove(line, line + len + 1, rest); // the line and its break
	} else {
		line[0] = '\0';                      // the last line had no break
		if (line > text && line[-1] == '\n') line[-1] = '\n';
	}
	return true;
}
