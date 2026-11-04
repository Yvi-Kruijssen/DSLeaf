#include "imgcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define HEADER "# DSLeaf picture sizes v1\n"
#define SECTION_VERSION "1"

uint32_t imgcache_key(const char *zip_path) {
	uint32_t h = 2166136261u; // FNV-1a
	for (const unsigned char *p = (const unsigned char *)zip_path; *p; p++) h = (h ^ *p) * 16777619u;
	return h;
}

// The next "[book]" line at or after p.
static const char *next_section(const char *p) {
	while (*p) {
		if (strncmp(p, "[book]\n", 7) == 0) return p;
		const char *nl = strchr(p, '\n');
		if (!nl) return NULL;
		p = nl + 1;
	}
	return NULL;
}

static const char *line_value(const char *from, const char *to, const char *key) {
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
	const char *p = line_value(from, to, "path");
	size_t n = strlen(path);
	if (!p || (size_t)(to - p) <= n || strncmp(p, path, n) != 0 || p[n] != '\n') return false;
	p = line_value(from, to, "size");
	return p && strtoul(p, NULL, 10) == size;
}

int imgcache_find(const char *text, const char *path, uint32_t size, ImgEntry *out, int cap) {
	if (!text) return 0;
	for (const char *s = next_section(text); s; ) {
		const char *to = next_section(s + 7);
		const char *end = to ? to : s + strlen(s);
		if (!section_is(s, end, path, size)) {
			s = to;
			continue;
		}
		const char *v = line_value(s, end, "v");
		if (!v || v[0] != SECTION_VERSION[0] || v[1] != '\n') return 0;
		int n = 0;
		for (const char *p = s; p < end && n < cap; ) {
			const char *nl = memchr(p, '\n', (size_t)(end - p));
			size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
			if (len > 2 && p[0] == 'i' && p[1] == '=') {
				char line[96];
				if (len - 2 < sizeof line) {
					memcpy(line, p + 2, len - 2);
					line[len - 2] = '\0';
					unsigned key, kind, prog, comp, depth, ctype;
					unsigned long w, h;
					if (sscanf(line, "%x,%u,%lu,%lu,%u,%u,%u,%u", &key, &kind, &w, &h, &prog, &comp, &depth, &ctype) == 8 &&
					    kind > IMAGE_NONE && kind <= IMAGE_JPEG && w > 0 && h > 0 && w <= 65535 && h <= 65535 && prog <= 1 &&
					    comp <= 4 && depth <= 16 && ctype <= 6) {
						ImgEntry *e = &out[n++];
						e->key = key;
						e->info.kind = (ImageKind)kind;
						e->info.width = (uint32_t)w;
						e->info.height = (uint32_t)h;
						e->info.progressive = prog != 0;
						e->info.components = (uint8_t)comp;
						e->info.bit_depth = (uint8_t)depth;
						e->info.color_type = (uint8_t)ctype;
					}
				}
			}
			if (!nl) break;
			p = nl + 1;
		}
		return n;
	}
	return 0;
}

int imgcache_update(const char *old_text, char *out, size_t cap, const char *path, uint32_t size, const ImgEntry *entry, int count) {
	if (count < 1 || count > IMGCACHE_MAX || strlen(path) > IMGCACHE_PATH_MAX || strchr(path, '\n')) return -1;
	size_t need = 160 + strlen(path) + (size_t)count * 48;
	char *sec = malloc(need);
	if (!sec) return -1;
	size_t len = (size_t)snprintf(sec, need, "[book]\npath=%s\nsize=%lu\nv=" SECTION_VERSION "\n", path, (unsigned long)size);
	for (int i = 0; i < count; i++) {
		const ImageInfo *f = &entry[i].info;
		len += (size_t)snprintf(sec + len, need - len, "i=%08x,%d,%lu,%lu,%d,%d,%d,%d\n", (unsigned)entry[i].key, (int)f->kind,
		                        (unsigned long)f->width, (unsigned long)f->height, f->progressive ? 1 : 0, f->components,
		                        f->bit_depth, f->color_type);
	}
	if (len + sizeof HEADER > cap) {
		free(sec);
		return -1;
	}
	size_t room = cap - sizeof HEADER - len;
	const char *keep = old_text ? next_section(old_text) : NULL;
	for (;;) { // drop the oldest sections of other books until the rest fits
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

ImgCache *imgcache_open(const char *path, uint32_t size) {
	ImgCache *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	if (strlen(path) > IMGCACHE_PATH_MAX) return c; // works, but is not stored
	snprintf(c->path, sizeof c->path, "%s", path);
	c->size = size;
	char *text = malloc(IMGCACHE_FILE_MAX);
	ImgEntry *e = malloc(sizeof *e * IMGCACHE_MAX);
	if (text && e && storage_read_text(IMGCACHE_PATH, text, IMGCACHE_FILE_MAX) >= 0) {
		int n = imgcache_find(text, c->path, size, e, IMGCACHE_MAX);
		if (n > 0) {
			c->entry = e;
			e = NULL;
			c->count = n;
			c->cap = IMGCACHE_MAX;
		}
	}
	free(text);
	free(e);
	return c;
}

const ImageInfo *imgcache_get(const ImgCache *c, uint32_t key) {
	if (!c) return NULL;
	for (int i = 0; i < c->count; i++) {
		if (c->entry[i].key == key) return &c->entry[i].info;
	}
	return NULL;
}

void imgcache_put(ImgCache *c, uint32_t key, const ImageInfo *info) {
	if (!c || c->path[0] == '\0' || imgcache_get(c, key)) return;
	if (!c->entry) {
		c->entry = malloc(sizeof *c->entry * IMGCACHE_MAX);
		if (!c->entry) return;
		c->cap = IMGCACHE_MAX;
	}
	if (c->count >= c->cap) return;
	c->entry[c->count].key = key;
	c->entry[c->count].info = *info;
	c->count++;
	c->dirty = true;
}

bool imgcache_save(ImgCache *c) {
	if (!c || !c->dirty) return true;
	char *old_text = malloc(IMGCACHE_FILE_MAX), *out = malloc(IMGCACHE_FILE_MAX);
	bool ok = false;
	if (old_text && out) {
		if (storage_read_text(IMGCACHE_PATH, old_text, IMGCACHE_FILE_MAX) < 0) old_text[0] = '\0';
		int n = imgcache_update(old_text, out, IMGCACHE_FILE_MAX, c->path, c->size, c->entry, c->count);
		ok = n > 0 && storage_write_text(IMGCACHE_PATH, out);
	}
	free(old_text);
	free(out);
	c->dirty = false; // a failed write is not tried again at every frame; the sizes are found again next time
	return ok;
}

void imgcache_close(ImgCache *c) {
	if (!c) return;
	imgcache_save(c);
	free(c->entry);
	free(c);
}
