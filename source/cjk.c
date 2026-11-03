#include "cjk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "storage.h"

#define RANGES_MAX 256
#define CACHE_SIZE 512 // glyphs kept: 512 x 36 bytes

typedef struct {
	uint16_t first, count;
	uint32_t before; // glyphs in the ranges before this one
} Range;

typedef struct {
	uint16_t cp;     // 0 for an empty slot
	bool ok;
	uint16_t rows[CJK_SIZE_MAX];
} Slot;

static FILE *file;
static int width, height, row_bytes, glyph_bytes;
static uint32_t data_start;
static Range range[RANGES_MAX];
static int range_count;
static Slot cache[CACHE_SIZE];

bool cjk_is_wide(uint32_t cp) {
	return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
	       (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA960 && cp <= 0xA97F) ||
	       (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
	       (cp >= 0xFFE0 && cp <= 0xFFE6);
}

void cjk_unload(void) {
	if (file) fclose(file);
	file = NULL;
	width = height = range_count = 0;
	memset(cache, 0, sizeof cache);
}

bool cjk_loaded(void) { return file != NULL; }
int cjk_width(void) { return width; }
int cjk_height(void) { return height; }

typedef struct {
	char name[64];
	bool found;
} Pick;

// The first .dslc in name order (case ignored).
static bool visit(const char *name, bool is_dir, void *user) {
	Pick *p = user;
	size_t n = strlen(name);
	if (is_dir || name[0] == '.' || n < 6 || n >= sizeof p->name || strcasecmp(name + n - 5, ".dslc") != 0) return true;
	if (!p->found || strcasecmp(name, p->name) < 0) {
		snprintf(p->name, sizeof p->name, "%s", name);
		p->found = true;
	}
	return true;
}

static bool read_file(void) {
	uint8_t head[16];
	if (fread(head, 1, sizeof head, file) != sizeof head || memcmp(head, "DSLC", 4) != 0 || head[4] != 1) return false;
	width = head[5];
	height = head[6];
	range_count = head[8] | (head[9] << 8);
	if (width < 4 || width > CJK_SIZE_MAX || height < 4 || height > CJK_SIZE_MAX || range_count < 1 || range_count > RANGES_MAX) return false;
	row_bytes = (width + 7) / 8;
	glyph_bytes = row_bytes * height;
	uint32_t total = 0;
	for (int i = 0; i < range_count; i++) {
		uint8_t r[4];
		if (fread(r, 1, 4, file) != 4) return false;
		range[i].first = (uint16_t)(r[0] | (r[1] << 8));
		range[i].count = (uint16_t)(r[2] | (r[3] << 8));
		if (range[i].count == 0 || (i > 0 && range[i].first < range[i - 1].first + range[i - 1].count)) return false; // sorted, apart
		range[i].before = total;
		total += range[i].count;
	}
	data_start = 16 + 4 * (uint32_t)range_count;
	// The file must be as long as the glyphs need.
	if (fseek(file, 0, SEEK_END) != 0) return false;
	long size = ftell(file);
	return size >= 0 && (uint64_t)size >= (uint64_t)data_start + (uint64_t)total * (uint64_t)glyph_bytes;
}

bool cjk_load(void) {
	cjk_unload();
	Pick pick = { "", false };
	if (!storage_list_dir("/DSLeaf/Fonts", visit, &pick) || !pick.found) return false;
	char path[128];
	snprintf(path, sizeof path, "/DSLeaf/Fonts/%s", pick.name);
	file = storage_fopen(path, "rb");
	if (!file) return false;
	if (!read_file()) {
		cjk_unload();
		return false;
	}
	return true;
}

// The range that holds the code point, or -1.
static int find_range(uint32_t cp) {
	int lo = 0, hi = range_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (cp < range[mid].first) hi = mid - 1;
		else if (cp >= (uint32_t)range[mid].first + range[mid].count) lo = mid + 1;
		else return mid;
	}
	return -1;
}

bool cjk_has(uint32_t cp) {
	return file && cp <= 0xFFFF && find_range(cp) >= 0;
}

bool cjk_rows(uint32_t cp, uint16_t rows[CJK_SIZE_MAX]) {
	if (!file || cp > 0xFFFF) return false;
	Slot *slot = &cache[(cp * 2654435761u >> 8) % CACHE_SIZE];
	if (slot->cp == cp && slot->cp != 0) {
		if (slot->ok) memcpy(rows, slot->rows, sizeof slot->rows);
		return slot->ok;
	}
	int r = find_range(cp);
	uint8_t raw[CJK_SIZE_MAX * 2];
	bool ok = false;
	if (r >= 0) {
		long at = (long)data_start + ((long)range[r].before + (long)(cp - range[r].first)) * glyph_bytes;
		ok = fseek(file, at, SEEK_SET) == 0 && fread(raw, 1, (size_t)glyph_bytes, file) == (size_t)glyph_bytes;
	}
	slot->cp = (uint16_t)cp;
	slot->ok = ok;
	memset(slot->rows, 0, sizeof slot->rows);
	if (ok) {
		for (int y = 0; y < height; y++) {
			slot->rows[y] = raw[y * row_bytes];
			if (row_bytes == 2) slot->rows[y] |= (uint16_t)(raw[y * row_bytes + 1] << 8);
		}
		memcpy(rows, slot->rows, sizeof slot->rows);
	}
	return ok;
}
