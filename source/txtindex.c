#include "txtindex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define HEADER "# DSLeaf text index v1\n"
#define SECTION_VERSION "1"

// The next "[book]" line at or after p (p itself if it is one).
static const char *next_section(const char *p) {
	while (*p) {
		if (strncmp(p, "[book]\n", 7) == 0) return p;
		const char *nl = strchr(p, '\n');
		if (!nl) return NULL;
		p = nl + 1;
	}
	return NULL;
}

// The start of the line "key=" inside [from, to), or NULL.
static const char *section_line(const char *from, const char *to, const char *key) {
	size_t klen = strlen(key);
	for (const char *p = from; p < to; ) {
		if ((size_t)(to - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') return p + klen + 1;
		const char *nl = memchr(p, '\n', (size_t)(to - p));
		if (!nl) break;
		p = nl + 1;
	}
	return NULL;
}

static bool section_is(const char *from, const char *to, const char *path, uint32_t size) {
	const char *p = section_line(from, to, "path");
	size_t n = strlen(path);
	if (!p || (size_t)(to - p) <= n || strncmp(p, path, n) != 0 || p[n] != '\n') return false;
	p = section_line(from, to, "size");
	return p && strtoul(p, NULL, 10) == size;
}

bool txtindex_find(const char *text, const char *path, uint32_t size, TxtIndex *ix) {
	if (!text) return false;
	for (const char *s = next_section(text); s; ) {
		const char *to = next_section(s + 7);
		const char *end = to ? to : s + strlen(s);
		if (!section_is(s, end, path, size)) {
			s = to;
			continue;
		}
		const char *p = section_line(s, end, "v");
		if (!p || p[0] != SECTION_VERSION[0] || p[1] != '\n') return false;
		unsigned enc, ds, de, lm, en;
		p = section_line(s, end, "i");
		if (!p || sscanf(p, "%u,%u,%u,%u,%u", &enc, &ds, &de, &lm, &en) != 5 || enc > 99 || lm > 1 || en > 1) return false;
		if (de > size || ds >= de) return false;
		p = section_line(s, end, "s");
		if (!p) return false;
		int n = 0;
		uint32_t prev = 0;
		while (n <= TXTINDEX_CHAPTERS_MAX) {
			char *stop;
			unsigned long v = strtoul(p, &stop, 10);
			if (stop == p || v > de || (n > 0 && v <= prev)) return false;
			ix->start[n++] = (uint32_t)v;
			prev = (uint32_t)v;
			p = stop;
			if (*p == ',') p++;
			else break;
		}
		if (n < 2 || n > TXTINDEX_CHAPTERS_MAX + 1 || (*p != '\n' && *p != '\0')) return false;
		if (ix->start[0] != ds || ix->start[n - 1] != de) return false;
		ix->encoding = (int)enc;
		ix->data_start = ds;
		ix->data_end = de;
		ix->line_mode = lm;
		ix->english = en;
		ix->count = n - 1;
		return true;
	}
	return false;
}

int txtindex_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size, const TxtIndex *ix) {
	if (ix->count < 1 || ix->count > TXTINDEX_CHAPTERS_MAX || strlen(path) > TXTINDEX_PATH_MAX || strchr(path, '\n')) return -1;
	size_t need = 200 + strlen(path) + (size_t)(ix->count + 1) * 11;
	char *sec = malloc(need);
	if (!sec) return -1;
	size_t len = (size_t)snprintf(sec, need, "[book]\npath=%s\nsize=%lu\nv=" SECTION_VERSION "\ni=%d,%lu,%lu,%d,%d\ns=", path,
	                              (unsigned long)size, ix->encoding, (unsigned long)ix->data_start, (unsigned long)ix->data_end,
	                              ix->line_mode ? 1 : 0, ix->english ? 1 : 0);
	for (int i = 0; i <= ix->count; i++) len += (size_t)snprintf(sec + len, need - len, i ? ",%lu" : "%lu", (unsigned long)ix->start[i]);
	len += (size_t)snprintf(sec + len, need - len, "\n");
	if (len + sizeof HEADER > cap) {
		free(sec);
		return -1;
	}

	// The sections of other books, newest last. Drop the oldest until the rest and the new one fit.
	const char *first = old_text ? next_section(old_text) : NULL;
	size_t room = cap - sizeof HEADER - len;
	const char *keep = first;
	for (;;) {
		size_t total = 0;
		for (const char *q = keep; q; ) {
			const char *to = next_section(q + 7);
			const char *end = to ? to : q + strlen(q);
			if (!section_is(q, end, path, size)) total += (size_t)(end - q) + 1;
			q = to;
		}
		if (total <= room || !keep) break;
		keep = next_section(keep + 7);
	}
	size_t n = (size_t)snprintf(out, cap, HEADER);
	for (const char *q = keep; q; ) {
		const char *to = next_section(q + 7);
		const char *end = to ? to : q + strlen(q);
		if (!section_is(q, end, path, size)) {
			memcpy(out + n, q, (size_t)(end - q));
			n += (size_t)(end - q);
			if (out[n - 1] != '\n') out[n++] = '\n';
		}
		q = to;
	}
	memcpy(out + n, sec, len);
	n += len;
	out[n] = '\0';
	free(sec);
	return (int)n;
}

bool txtindex_load(const char *path, uint32_t size, TxtIndex *ix) {
	char *text = malloc(TXTINDEX_FILE_MAX);
	if (!text) return false;
	bool ok = storage_read_text(TXTINDEX_PATH, text, TXTINDEX_FILE_MAX) >= 0 && txtindex_find(text, path, size, ix);
	free(text);
	return ok;
}

bool txtindex_save(const char *path, uint32_t size, const TxtIndex *ix) {
	char *old_text = malloc(TXTINDEX_FILE_MAX), *out = malloc(TXTINDEX_FILE_MAX);
	bool ok = false;
	if (old_text && out) {
		if (storage_read_text(TXTINDEX_PATH, old_text, TXTINDEX_FILE_MAX) < 0) old_text[0] = '\0';
		int n = txtindex_update(old_text, out, TXTINDEX_FILE_MAX, path, size, ix);
		ok = n > 0 && storage_write_text(TXTINDEX_PATH, out);
	}
	free(old_text);
	free(out);
	return ok;
}
