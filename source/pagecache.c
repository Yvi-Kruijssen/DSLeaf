#include "pagecache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define HEADER "# DSLeaf page counts v4\n"
// Counts made by an older layout (before pictures were pages, decision 075, before words broke at hyphens, decision 089, or before scripts were part of the layout, decision 091) have another version or none and are not used.
#define SECTION_VERSION "4"

// A section runs from a line "[book]" to the next one (or the end of the text).
static const char *next_section(const char *text) {
	const char *p = text;
	while (*p) {
		if (strncmp(p, "[book]", 6) == 0 && (p == text || p[-1] == '\n')) return p;
		const char *nl = strchr(p, '\n');
		if (!nl) return NULL;
		p = nl + 1;
	}
	return NULL;
}

// The value of a line "key=value" inside [from, to) copied into `out`. False if there is none.
static bool section_value(const char *from, const char *to, const char *key, char *out, size_t cap) {
	size_t klen = strlen(key);
	const char *p = from;
	while (p < to) {
		const char *nl = memchr(p, '\n', (size_t)(to - p));
		const char *end = nl ? nl : to;
		if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
			size_t n = (size_t)(end - p) - klen - 1;
			if (n >= cap) return false;
			memcpy(out, p + klen + 1, n);
			out[n] = '\0';
			return true;
		}
		if (!nl) break;
		p = nl + 1;
	}
	return false;
}

static bool is_book(const char *from, const char *to, const char *path, uint32_t size) {
	char value[PAGECACHE_PATH_MAX + 8];
	if (!section_value(from, to, "path", value, sizeof value) || strcmp(value, path) != 0) return false;
	if (!section_value(from, to, "size", value, sizeof value)) return false;
	return strtoul(value, NULL, 10) == size;
}

bool pagecache_find(const char *text, const char *path, uint32_t size, const PageLayout *layout, uint16_t *pages,
                    int count) {
	if (!text || count <= 0 || count > PAGECACHE_CHAPTERS_MAX) return false;
	for (const char *s = next_section(text); s; ) {
		const char *to = next_section(s + 6);
		const char *end = to ? to : s + strlen(s);
		if (is_book(s, end, path, size)) {
			char value[PAGECACHE_PATH_MAX + 8];
			int c, r, g, i, sc;
			if (!section_value(s, end, "v", value, sizeof value) || strcmp(value, SECTION_VERSION) != 0) return false;
			if (!section_value(s, end, "layout", value, sizeof value) || sscanf(value, "%d,%d,%d,%d,%d", &c, &r, &g, &i, &sc) != 5) return false;
			if (c != layout->cols || r != layout->rows || g != layout->gap || i != layout->indent || sc != layout->scripts) return false;

			// The counts: exactly `count` numbers, each 0 to 65535.
			const char *p = strstr(s, "\np=");
			if (!p || p >= end) return false;
			p += 3;
			int n = 0;
			while (*p && *p != '\n' && n < count) {
				char *stop;
				unsigned long v = strtoul(p, &stop, 10);
				if (stop == p || v > 0xFFFF) return false;
				pages[n++] = (uint16_t)v;
				p = stop;
				if (*p == ',') p++;
				else break;
			}
			return n == count && (*p == '\0' || *p == '\n');
		}
		s = to;
	}
	return false;
}

int pagecache_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size,
                     const PageLayout *layout, const uint16_t *pages, int count) {
	if (count <= 0 || count > PAGECACHE_CHAPTERS_MAX || strlen(path) > PAGECACHE_PATH_MAX || strchr(path, '\n')) return -1;

	// The new section first, so its size is known.
	size_t need = 160 + strlen(path) + (size_t)count * 6;
	char *sec = malloc(need);
	if (!sec) return -1;
	size_t len = (size_t)snprintf(sec, need, "[book]\npath=%s\nsize=%lu\nlayout=%d,%d,%d,%d,%d\nv=" SECTION_VERSION "\np=", path, (unsigned long)size,
	                              layout->cols, layout->rows, layout->gap, layout->indent, layout->scripts);
	for (int i = 0; i < count; i++) len += (size_t)snprintf(sec + len, need - len, i ? ",%u" : "%u", pages[i]);
	len += (size_t)snprintf(sec + len, need - len, "\n");
	if (len + sizeof HEADER > cap) {
		free(sec);
		return -1;
	}

	// The old sections of other books, oldest first, as many as fit before the new one.
	const char *first = old_text ? next_section(old_text) : NULL;
	size_t total = 0;
	const char *s = first;
	// Count the bytes of the sections to keep.
	for (const char *q = first; q; ) {
		const char *to = next_section(q + 6);
		const char *end = to ? to : q + strlen(q);
		if (!is_book(q, end, path, size)) total += (size_t)(end - q);
		q = to;
	}
	// Drop the oldest ones until it all fits.
	size_t budget = cap - sizeof HEADER - len;
	while (s && total > budget) {
		const char *to = next_section(s + 6);
		const char *end = to ? to : s + strlen(s);
		if (!is_book(s, end, path, size)) total -= (size_t)(end - s);
		s = to;
	}

	size_t n = (size_t)snprintf(out, cap, "%s", HEADER);
	for (const char *q = s; q; ) {
		const char *to = next_section(q + 6);
		const char *end = to ? to : q + strlen(q);
		if (!is_book(q, end, path, size)) {
			memcpy(out + n, q, (size_t)(end - q));
			n += (size_t)(end - q);
			if (n > 0 && out[n - 1] != '\n') out[n++] = '\n';
		}
		q = to;
	}
	memcpy(out + n, sec, len);
	n += len;
	out[n] = '\0';
	free(sec);
	return (int)n;
}

bool pagecache_load(const char *path, uint32_t size, const PageLayout *layout, uint16_t *pages, int count) {
	char *text = malloc(PAGECACHE_FILE_MAX);
	if (!text) return false;
	bool ok = storage_read_text(PAGECACHE_PATH, text, PAGECACHE_FILE_MAX) >= 0 && pagecache_find(text, path, size, layout, pages, count);
	free(text);
	return ok;
}

bool pagecache_save(const char *path, uint32_t size, const PageLayout *layout, const uint16_t *pages, int count) {
	char *old_text = malloc(PAGECACHE_FILE_MAX), *out = malloc(PAGECACHE_FILE_MAX);
	bool ok = false;
	if (old_text && out) {
		if (storage_read_text(PAGECACHE_PATH, old_text, PAGECACHE_FILE_MAX) < 0) old_text[0] = '\0';
		int n = pagecache_update(old_text, out, PAGECACHE_FILE_MAX, path, size, layout, pages, count);
		ok = n > 0 && storage_write_text(PAGECACHE_PATH, out);
	}
	free(old_text);
	free(out);
	return ok;
}
