#include "bookmarks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

static const char *const HEADER = "# DSLeaf bookmarks v1\n";
static const char *const SECTION = "[book]";

// ---- Editing ----

// Copies `src` to `dst`, replacing control characters with spaces, collapsing
// spaces and trimming. Cuts at a UTF-8 character boundary.
static void clean_text(char *dst, size_t cap, const char *src) {
	size_t n = 0;
	bool space = false;
	for (; *src; src++) {
		unsigned char c = (unsigned char)*src;
		if (c < 0x20 || c == 0x7F || c == ' ') {
			space = n > 0;
			continue;
		}
		size_t need = (space ? 1 : 0) + 1;
		if (n + need >= cap) break;
		if (space) dst[n++] = ' ';
		space = false;
		dst[n++] = (char)c;
	}
	// If the last character was cut short by the size limit, drop what is left of it.
	size_t lead = n;
	while (lead > 0 && n - lead < 3 && ((unsigned char)dst[lead - 1] & 0xC0) == 0x80) lead--;
	if (lead > 0) {
		unsigned char c = (unsigned char)dst[lead - 1];
		size_t want = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
		if (want > 1 && n - (lead - 1) < want) n = lead - 1; // an incomplete character
	}
	dst[n] = '\0';
}

static int mark_order(const Mark *a, int chapter, uint32_t offset) {
	if (a->chapter != chapter) return a->chapter < chapter ? -1 : 1;
	if (a->offset != offset) return a->offset < offset ? -1 : 1;
	return 0;
}

MarkResult marks_add(BookMarks *marks, int chapter, uint32_t offset, const char *text) {
	int at = 0;
	while (at < marks->count) {
		int order = mark_order(&marks->mark[at], chapter, offset);
		if (order == 0) return MARK_EXISTS;
		if (order > 0) break;
		at++;
	}
	if (marks->count >= BOOKMARKS_MAX) return MARK_FULL;

	for (int i = marks->count; i > at; i--) marks->mark[i] = marks->mark[i - 1];
	Mark *m = &marks->mark[at];
	m->chapter = (uint16_t)chapter;
	m->offset = offset;
	clean_text(m->text, sizeof m->text, text);
	marks->count++;
	return MARK_ADDED;
}

void marks_remove_at(BookMarks *marks, int index) {
	if (index < 0 || index >= marks->count) return;
	for (int i = index; i < marks->count - 1; i++) marks->mark[i] = marks->mark[i + 1];
	marks->count--;
}

int marks_remove_range(BookMarks *marks, int chapter, uint32_t from, uint32_t to) {
	int removed = 0;
	for (int i = marks->count - 1; i >= 0; i--) {
		const Mark *m = &marks->mark[i];
		if (m->chapter == chapter && m->offset >= from && m->offset < to) {
			marks_remove_at(marks, i);
			removed++;
		}
	}
	return removed;
}

int marks_count_range(const BookMarks *marks, int chapter, uint32_t from, uint32_t to) {
	int n = 0;
	for (int i = 0; i < marks->count; i++) {
		const Mark *m = &marks->mark[i];
		if (m->chapter == chapter && m->offset >= from && m->offset < to) n++;
	}
	return n;
}

// ---- Reading the text ----

// One line of `text`, without its line ending. Returns false at the end.
static bool next_line(const char **p, const char **start, size_t *len) {
	if (**p == '\0') return false;
	const char *s = *p;
	size_t n = strcspn(s, "\n");
	*start = s;
	*len = n;
	*p = s + n + (s[n] == '\n' ? 1 : 0);
	while (*len > 0 && ((*start)[*len - 1] == '\r' || (*start)[*len - 1] == ' ')) (*len)--;
	return true;
}

static bool starts_with(const char *line, size_t len, const char *prefix) {
	size_t n = strlen(prefix);
	return len >= n && strncmp(line, prefix, n) == 0;
}

// Parses a decimal number. Returns false if there is none.
static bool parse_u32(const char **p, const char *end, uint32_t *out) {
	if (*p >= end || **p < '0' || **p > '9') return false;
	uint64_t v = 0;
	while (*p < end && **p >= '0' && **p <= '9') {
		v = v * 10 + (uint64_t)(**p - '0');
		if (v > 0xFFFFFFFFu) return false;
		(*p)++;
	}
	*out = (uint32_t)v;
	return true;
}

// "chapter,offset" at the start of a value. Leaves `*p` after the offset.
static bool parse_position(const char **p, const char *end, uint16_t *chapter, uint32_t *offset) {
	uint32_t c;
	if (!parse_u32(p, end, &c) || c > 0xFFFF) return false;
	if (*p >= end || **p != ',') return false;
	(*p)++;
	if (!parse_u32(p, end, offset)) return false;
	*chapter = (uint16_t)c;
	return true;
}

// The parts of one [book] section.
typedef struct {
	bool path_ok, size_ok;
	BookMarks marks;
} Section;

static void section_line(Section *s, const char *line, size_t len, const char *path, uint32_t size) {
	const char *end = line + len;
	if (starts_with(line, len, "path=")) {
		size_t n = strlen(path);
		s->path_ok = len - 5 == n && strncmp(line + 5, path, n) == 0;
	} else if (starts_with(line, len, "size=")) {
		const char *p = line + 5;
		uint32_t v;
		s->size_ok = parse_u32(&p, end, &v) && p == end && v == size;
	} else if (starts_with(line, len, "last=")) {
		const char *p = line + 5;
		uint16_t c;
		uint32_t o;
		if (parse_position(&p, end, &c, &o)) {
			s->marks.has_last = true;
			s->marks.last_chapter = c;
			s->marks.last_offset = o;
		}
	} else if (starts_with(line, len, "b=")) {
		const char *p = line + 2;
		uint16_t c;
		uint32_t o;
		if (!parse_position(&p, end, &c, &o)) return;
		char text[MARK_TEXT_MAX * 4];
		size_t n = 0;
		if (p < end && *p == ',') {
			p++;
			n = (size_t)(end - p) < sizeof text - 1 ? (size_t)(end - p) : sizeof text - 1;
			memcpy(text, p, n);
		}
		text[n] = '\0';
		marks_add(&s->marks, c, o, text); // a repeat or an overflow is simply ignored
	}
}

bool bookmarks_parse(const char *text, const char *path, uint32_t size, BookMarks *out) {
	memset(out, 0, sizeof *out);
	bool found = false;
	static Section cur; // about 800 bytes, kept off the stack
	bool in_section = false;

	const char *p = text, *line;
	size_t len;
	for (;;) {
		bool more = next_line(&p, &line, &len);
		bool header = more && len == strlen(SECTION) && strncmp(line, SECTION, len) == 0;
		if (in_section && (!more || header) && cur.path_ok && cur.size_ok) {
			*out = cur.marks; // a later copy of the same book wins
			found = true;
		}
		if (!more) break;
		if (header) {
			memset(&cur, 0, sizeof cur);
			in_section = true;
		} else if (in_section) {
			section_line(&cur, line, len, path, size);
		}
	}
	return found;
}

// ---- Writing the text ----

static bool valid_path(const char *path) {
	size_t n = strlen(path);
	return n > 0 && n <= BOOKMARKS_PATH_MAX && strpbrk(path, "\r\n") == NULL;
}

static int format_section(const char *path, uint32_t size, const BookMarks *m, char *out, size_t cap) {
	size_t used = 0;
	int n = snprintf(out, cap, "%s\npath=%s\nsize=%u\n", SECTION, path, (unsigned)size);
	if (n < 0 || (size_t)n >= cap) return -1;
	used = (size_t)n;

	if (m->has_last) {
		n = snprintf(out + used, cap - used, "last=%u,%u\n", (unsigned)m->last_chapter, (unsigned)m->last_offset);
		if (n < 0 || (size_t)n >= cap - used) return -1;
		used += (size_t)n;
	}
	for (int i = 0; i < m->count; i++) {
		n = snprintf(out + used, cap - used, "b=%u,%u,%s\n", (unsigned)m->mark[i].chapter,
		             (unsigned)m->mark[i].offset, m->mark[i].text);
		if (n < 0 || (size_t)n >= cap - used) return -1;
		used += (size_t)n;
	}
	return (int)used;
}

typedef struct {
	const char *start;
	size_t len;
} Span;

int bookmarks_merge(const char *text, const char *path, uint32_t size, const BookMarks *marks, char *out,
                    size_t cap) {
	if (!valid_path(path)) return -1;
	size_t header_len = strlen(HEADER);
	if (cap <= header_len) return -1;

	// This book's new section, which goes last.
	static char mine[BOOKMARKS_FILE_MAX / 4];
	int mine_len = 0;
	if (marks->has_last || marks->count > 0) {
		mine_len = format_section(path, size, marks, mine, sizeof mine);
		if (mine_len < 0) return -1;
	}
	if (header_len + (size_t)mine_len + 1 > cap) return -1;

	// The other books' sections, in order, as spans of the old text.
	static Span spans[256];
	int span_count = 0;
	const char *section_start = NULL;
	const char *p = text, *line;
	size_t len;
	static Section probe;
	for (;;) {
		const char *line_start = p;
		bool more = next_line(&p, &line, &len);
		bool header = more && len == strlen(SECTION) && strncmp(line, SECTION, len) == 0;
		if (section_start && (!more || header)) {
			const char *section_end = line_start;
			// Is it this book's own old section? Then it is replaced, not kept.
			char *copy = malloc((size_t)(section_end - section_start) + 1);
			bool mine_old = false;
			if (copy) {
				memcpy(copy, section_start, (size_t)(section_end - section_start));
				copy[section_end - section_start] = '\0';
				memset(&probe, 0, sizeof probe);
				const char *q = copy, *l2;
				size_t n2;
				while (next_line(&q, &l2, &n2)) section_line(&probe, l2, n2, path, size);
				mine_old = probe.path_ok && probe.size_ok;
				free(copy);
			}
			if (!mine_old && span_count < (int)(sizeof spans / sizeof spans[0])) {
				spans[span_count].start = section_start;
				spans[span_count].len = (size_t)(section_end - section_start);
				span_count++;
			}
		}
		if (!more) break;
		if (header) section_start = line_start;
	}

	// Keep as many of the newest other sections as fit.
	size_t budget = cap - 1 - header_len - (size_t)mine_len;
	int first = span_count;
	size_t used = 0;
	while (first > 0 && used + spans[first - 1].len <= budget) {
		used += spans[first - 1].len;
		first--;
	}

	size_t pos = 0;
	memcpy(out, HEADER, header_len);
	pos = header_len;
	for (int i = first; i < span_count; i++) {
		memcpy(out + pos, spans[i].start, spans[i].len);
		pos += spans[i].len;
		if (pos > 0 && out[pos - 1] != '\n') out[pos++] = '\n';
	}
	memcpy(out + pos, mine, (size_t)mine_len);
	pos += (size_t)mine_len;
	out[pos] = '\0';
	return (int)pos;
}

// ---- The card ----

bool bookmarks_load(const char *path, uint32_t size, BookMarks *marks) {
	memset(marks, 0, sizeof *marks);
	char *text = malloc(BOOKMARKS_FILE_MAX + 1);
	if (!text) return false;

	bool found = false;
	if (storage_read_text(BOOKMARKS_PATH, text, BOOKMARKS_FILE_MAX + 1) >= 0) {
		found = bookmarks_parse(text, path, size, marks);
	}
	free(text);
	return found;
}

bool bookmarks_save(const char *path, uint32_t size, const BookMarks *marks) {
	char *old = malloc(BOOKMARKS_FILE_MAX + 1);
	char *out = malloc(BOOKMARKS_FILE_MAX + 1);
	bool ok = false;
	if (old && out) {
		if (storage_read_text(BOOKMARKS_PATH, old, BOOKMARKS_FILE_MAX + 1) < 0) old[0] = '\0';
		int n = bookmarks_merge(old, path, size, marks, out, BOOKMARKS_FILE_MAX + 1);
		ok = n >= 0 && storage_write_text(BOOKMARKS_PATH, out);
	}
	free(old);
	free(out);
	return ok;
}
