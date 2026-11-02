#include "toc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "chaptertext.h"
#include "storage.h"

#define NCX_DEPTH 8

// ---- Entries ----

void toc_free(Toc *toc) {
	free(toc->entry);
	toc->entry = NULL;
	toc->count = toc->cap = 0;
}

// Copies `src` into `dst`, collapsing white space, and cuts at a character boundary.
static void clean_title(char *dst, size_t cap, const char *src) {
	size_t n = 0;
	bool space = false;
	for (; *src; src++) {
		unsigned char c = (unsigned char)*src;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			space = n > 0;
			continue;
		}
		if (n + (space ? 1 : 0) + 1 >= cap) break;
		if (space) dst[n++] = ' ';
		space = false;
		dst[n++] = (char)c;
	}
	// If the last character was cut short, drop what is left of it.
	size_t lead = n;
	while (lead > 0 && n - lead < 3 && ((unsigned char)dst[lead - 1] & 0xC0) == 0x80) lead--;
	if (lead > 0) {
		unsigned char c = (unsigned char)dst[lead - 1];
		size_t want = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
		if (want > 1 && n - (lead - 1) < want) n = lead - 1;
	}
	dst[n] = '\0';
}

static int hex_value(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Some books (illustrated Project Gutenberg editions, for example) have a picture
// and a caption before each chapter heading, and their contents list the whole
// block: "I hope Mr. Bingley will like it. CHAPTER II.". That is hard to scan, so
// the chapter label is moved to the front: "CHAPTER II. I hope Mr. Bingley will
// like it.". It only happens when a sentence-like caption (one that ends with a
// full stop, !, ?, a quote or a bracket) comes before a trailing "CHAPTER" and a
// number, so ordinary titles are left alone.
static bool is_number_word(const char *s, size_t n) {
	if (n == 0 || n > 8) return false;
	size_t digits = 0;
	for (size_t i = 0; i < n; i++) {
		char c = (char)toupper((unsigned char)s[i]);
		if (c == '.' && i == n - 1 && i > 0) continue;       // a full stop after the number
		if (!(c >= '0' && c <= '9') && !strchr("IVXLCDM", c)) return false;
		digits++;
	}
	return digits > 0;
}

static bool ends_a_sentence(const char *s, size_t n) {
	if (n == 0) return false;
	unsigned char last = (unsigned char)s[n - 1];
	if (last == '.' || last == '!' || last == '?' || last == '"' || last == ')' || last == '\'') return true;
	// A curly closing quote: U+201D or U+2019 in UTF-8.
	return n >= 3 && (unsigned char)s[n - 3] == 0xE2 && (unsigned char)s[n - 2] == 0x80 &&
	       ((unsigned char)s[n - 1] == 0x9D || (unsigned char)s[n - 1] == 0x99);
}

static void tidy_title(char *title, size_t cap) {
	size_t len = strlen(title);

	// The last word is the number, the word before it must be "chapter".
	size_t num_start = len;
	while (num_start > 0 && title[num_start - 1] != ' ') num_start--;
	if (num_start == 0 || !is_number_word(title + num_start, len - num_start)) return;

	size_t word_end = num_start - 1; // the space before the number
	size_t word_start = word_end;
	while (word_start > 0 && title[word_start - 1] != ' ') word_start--;
	if (word_end - word_start != 7 || strncasecmp(title + word_start, "chapter", 7) != 0) return;
	if (word_start == 0) return; // just "CHAPTER II.", nothing to move

	size_t caption_len = word_start - 1; // without the space before "chapter"
	if (!ends_a_sentence(title, caption_len)) return;

	char moved[TOC_TITLE_MAX * 4];
	int n = snprintf(moved, sizeof moved, "%.*s %.*s", (int)(len - word_start), title + word_start, (int)caption_len, title);
	if (n > 0 && (size_t)n < sizeof moved && (size_t)n < cap) memcpy(title, moved, (size_t)n + 1);
}

typedef struct {
	Toc *toc;
	const char *base_dir;
	TocLookup lookup;
	void *lookup_user;
	int added;
} Builder;

// Appends an entry to the list, growing it. Returns false if memory is short.
static bool push_entry(Toc *toc, const char *title, const char *anchor, int chapter, int level) {
	if (toc->count >= toc->cap) {
		int cap = toc->cap ? toc->cap * 2 : 64;
		TocEntry *bigger = realloc(toc->entry, (size_t)cap * sizeof *bigger);
		if (!bigger) return false;
		toc->entry = bigger;
		toc->cap = cap;
	}
	TocEntry *e = &toc->entry[toc->count++];
	snprintf(e->title, sizeof e->title, "%s", title);
	snprintf(e->anchor, sizeof e->anchor, "%s", anchor);
	e->chapter = (uint16_t)chapter;
	e->level = (uint8_t)level;
	e->offset = TOC_OFFSET_UNKNOWN;
	return true;
}

// Adds one entry from a title and a link.
static void add_entry(Builder *b, const char *raw_title, const char *href, int level) {
	Toc *toc = b->toc;
	if (toc->count >= TOC_ENTRIES_MAX) return;

	char full[TOC_TITLE_MAX * 3];
	clean_title(full, sizeof full, raw_title);
	tidy_title(full, sizeof full);
	char title[TOC_TITLE_MAX];
	clean_title(title, sizeof title, full);   // cut to size after the label has moved, so the label always survives
	if (title[0] == '\0' || href[0] == '\0') return;

	char path[EPUB_PATH_MAX];
	if (!epub_resolve_path(b->base_dir, href, path, sizeof path)) return; // for example a link to "#here" in the same file
	int chapter = b->lookup(b->lookup_user, path);
	if (chapter < 0 || chapter > 0xFFFF) return;

	// The anchor: the part after '#', with %20 style escapes decoded.
	char anchor[TOC_ANCHOR_MAX] = "";
	const char *hash = strchr(href, '#');
	if (hash) {
		size_t n = 0;
		bool too_long = false;
		for (const char *p = hash + 1; *p; p++) {
			int c = (unsigned char)*p;
			if (c == '%' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0) {
				c = hex_value(p[1]) * 16 + hex_value(p[2]);
				p += 2;
			}
			if (n + 1 >= sizeof anchor) {
				too_long = true;
				break;
			}
			anchor[n++] = (char)c;
		}
		anchor[n] = '\0';
		if (too_long) anchor[0] = '\0'; // it could not be matched anyway, so go to the start of the chapter
	}

	if (level < 0) level = 0;
	if (level > TOC_LEVEL_MAX) level = TOC_LEVEL_MAX;

	// The same entry twice in a row is left out.
	if (toc->count > 0) {
		const TocEntry *last = &toc->entry[toc->count - 1];
		if (last->chapter == chapter && strcmp(last->anchor, anchor) == 0 && strcmp(last->title, title) == 0) return;
	}

	if (push_entry(toc, title, anchor, chapter, level)) b->added++;
}

// Text collected while reading an entry's title, with white space collapsed.
typedef struct {
	char text[TOC_TITLE_MAX * 4];
	size_t len;
} TitleBuf;

static void title_clear(TitleBuf *t) {
	t->len = 0;
	t->text[0] = '\0';
}

static void title_add(TitleBuf *t, const char *piece, size_t n) {
	if (t->len + n >= sizeof t->text) n = sizeof t->text - 1 - t->len;
	memcpy(t->text + t->len, piece, n);
	t->len += n;
	t->text[t->len] = '\0';
}

static bool contains_word(const char *list, const char *word) {
	size_t n = strlen(word);
	for (const char *p = list; *p;) {
		while (*p == ' ') p++;
		if (strncasecmp(p, word, n) == 0 && (p[n] == ' ' || p[n] == '\0')) return true;
		while (*p && *p != ' ') p++;
	}
	return false;
}

// ---- NCX: <navMap><navPoint><navLabel><text>Title</text></navLabel><content src="..."/> ... ----

typedef struct {
	TitleBuf title;
	char src[192];
	bool emitted;
} NcxFrame;

static int parse_ncx(Builder *b, XmlReader *x) {
	NcxFrame frames[NCX_DEPTH];
	int depth = 0;           // navPoints open
	bool in_navmap = false, in_label = false, in_text = false;

	XmlEvent e;
	while ((e = xml_next(x)) != XML_EOF) {
		if (e == XML_TEXT) {
			if (in_text && depth >= 1 && depth <= NCX_DEPTH) title_add(&frames[depth - 1].title, x->text, x->text_len);
			continue;
		}
		bool start = e == XML_START;

		if (strcasecmp(x->name, "navMap") == 0) {
			in_navmap = start;
		} else if (strcasecmp(x->name, "navPoint") == 0 && in_navmap) {
			if (start) {
				depth++;
				if (depth <= NCX_DEPTH) {
					title_clear(&frames[depth - 1].title);
					frames[depth - 1].src[0] = '\0';
					frames[depth - 1].emitted = false;
				}
			} else if (depth > 0) {
				depth--;
			}
		} else if (strcasecmp(x->name, "navLabel") == 0) {
			in_label = start;
		} else if (strcasecmp(x->name, "text") == 0 && in_label) {
			in_text = start;
		} else if (strcasecmp(x->name, "content") == 0 && start && depth >= 1 && depth <= NCX_DEPTH) {
			const char *src = xml_attr(x, "src");
			if (src) snprintf(frames[depth - 1].src, sizeof frames[depth - 1].src, "%s", src);
		}

		// An entry is complete once it has a title and a link.
		if (depth >= 1 && depth <= NCX_DEPTH) {
			NcxFrame *f = &frames[depth - 1];
			if (!f->emitted && f->src[0] && f->title.len > 0 && !in_text) {
				add_entry(b, f->title.text, f->src, depth - 1);
				f->emitted = true;
			}
		}
	}
	return b->added;
}

// ---- Navigation document: <nav epub:type="toc"><ol><li><a href="...">Title</a><ol> ... ----

static int parse_nav(Builder *b, XmlReader *x) {
	bool in_nav = false, nav_done = false, in_a = false;
	int ol_depth = 0;
	TitleBuf title;
	char href[192] = "";
	title_clear(&title);

	XmlEvent e;
	while ((e = xml_next(x)) != XML_EOF) {
		if (e == XML_TEXT) {
			if (in_a) title_add(&title, x->text, x->text_len);
			continue;
		}
		bool start = e == XML_START;

		if (strcasecmp(x->name, "nav") == 0) {
			if (start && !nav_done) {
				const char *type = xml_attr(x, "type");
				in_nav = type ? contains_word(type, "toc") : true; // a nav with no type is taken as the contents
				ol_depth = 0;
			} else if (!start && in_nav) {
				in_nav = false;
				nav_done = true;
			}
		} else if (!in_nav) {
			continue;
		} else if (strcasecmp(x->name, "ol") == 0) {
			ol_depth += start ? 1 : (ol_depth > 0 ? -1 : 0);
		} else if (strcasecmp(x->name, "a") == 0) {
			if (start) {
				const char *h = xml_attr(x, "href");
				snprintf(href, sizeof href, "%s", h ? h : "");
				title_clear(&title);
				in_a = true;
			} else if (in_a) {
				in_a = false;
				add_entry(b, title.text, href, ol_depth - 1);
			}
		}
	}
	return b->added;
}

int toc_parse(Toc *toc, XmlReadFn read, void *source, bool is_nav, const char *base_dir, TocLookup lookup,
              void *lookup_user) {
	static XmlReader reader; // about 2.5 KB, kept off the stack
	xml_init(&reader, read, source);
	Builder b = { toc, base_dir, lookup, lookup_user, 0 };
	return is_nav ? parse_nav(&b, &reader) : parse_ncx(&b, &reader);
}

// ---- A book's table of contents ----

typedef struct {
	ZipStream stream;
} Source;

static size_t source_read(void *user, uint8_t *buf, size_t cap) {
	int n = zip_stream_read(&((Source *)user)->stream, buf, cap);
	return n > 0 ? (size_t)n : 0;
}

static int chapter_of(void *user, const char *path) {
	const Epub *book = user;
	for (int i = 0; i < book->chapter_count; i++) {
		if (strcmp(book->chapter[i], path) == 0) return i;
	}
	return -1;
}

// ---- A table of contents made from the chapters (decision 052) ----
//
// For a book with no contents of its own: one entry for every chapter file that
// has text, titled with the chapter's first heading, or "Section N" when it has
// none. Only the start of each chapter is read.
#define SCAN_ITEMS 400 // how far into a chapter a heading is looked for

// Finds the title of a chapter. Returns false if the chapter has no text. `title`
// is empty when it has text but no heading.
static bool chapter_title(Epub *book, int chapter, char *title, size_t cap, bool *heading_first) {
	static EpubChapter stream; // too big for the stack
	title[0] = '\0';
	if (!epub_chapter_open(book, chapter, &stream)) return false;

	char raw[TOC_TITLE_MAX * 4];
	size_t len = 0;
	bool has_text = false, in_heading = false, seen_block = false;
	*heading_first = false;
	TextItem item;
	for (int n = 0; n < SCAN_ITEMS && epub_chapter_next(&stream, &item); n++) {
		if (item.kind == TEXT_BLOCK) {
			if (in_heading) break; // the heading is over
			if (!seen_block) *heading_first = item.block == BLOCK_HEADING;
			seen_block = true;
			if (item.block == BLOCK_HEADING) in_heading = true;
		} else if (item.kind == TEXT_WORD) {
			has_text = true;
			if (in_heading && len + strlen(item.text) + 2 < sizeof raw) len += (size_t)snprintf(raw + len, sizeof raw - len, " %s", item.text);
		} else if (item.kind == TEXT_LINEBREAK && in_heading && len + 2 < sizeof raw) {
			raw[len++] = ' '; // a heading over two lines: "CHAPTER I" <br> "The beginning"
		}
	}
	epub_chapter_close(&stream);
	if (!has_text) return false;

	raw[len] = '\0';
	char full[TOC_TITLE_MAX * 3];
	clean_title(full, sizeof full, raw);
	tidy_title(full, sizeof full);
	clean_title(title, cap, full);
	return true;
}

static bool toc_from_chapters(Toc *toc, Epub *book) {
	int sections = 0;
	for (int i = 0; i < book->chapter_count && toc->count < TOC_ENTRIES_MAX; i++) {
		char title[TOC_TITLE_MAX];
		bool heading_first;
		if (!chapter_title(book, i, title, sizeof title, &heading_first)) continue; // a cover, a picture page
		if (book->txt && i == 0 && !heading_first && book->title[0]) { // the start of a text file (a title page, a contents list): the name of the book
			clean_title(title, sizeof title, book->title);
		} else if (title[0] == '\0') {
			snprintf(title, sizeof title, "Section %d", sections + 1);
		}
		sections++;
		if (!push_entry(toc, title, "", i, 0)) break;
	}
	if (toc->count == 0) {
		toc_free(toc);
		return false;
	}
	return true;
}

static bool toc_from_file(Toc *toc, Epub *book) {
	if (book->toc_path[0] == '\0') return false;

	ZipEntry entry;
	static Source src;
	if (!zip_find(&book->zip, book->toc_path, &entry) || !zip_stream_open(&book->zip, &entry, &src.stream)) {
		return false;
	}

	char dir[EPUB_PATH_MAX];
	snprintf(dir, sizeof dir, "%s", book->toc_path);
	char *slash = strrchr(dir, '/');
	if (slash) slash[1] = '\0';
	else dir[0] = '\0';

	toc_parse(toc, source_read, &src, book->toc_is_nav, dir, chapter_of, book);
	zip_stream_close(&src.stream);

	if (toc->count == 0) {
		toc_free(toc);
		return false;
	}
	return true;
}

bool toc_load(Toc *toc, Epub *book) {
	memset(toc, 0, sizeof *toc);
	if (toc_from_file(toc, book)) return true;
	toc_free(toc);
	return toc_from_chapters(toc, book); // the book has no usable contents of its own
}

bool toc_resolve(Toc *toc, Epub *book, int chapter) {
	// Static, because three arrays of this size are too much for the DS stack.
	static const char *ids[TOC_ENTRIES_MAX];
	static int which[TOC_ENTRIES_MAX];
	static uint32_t offsets[TOC_ENTRIES_MAX];
	int n = 0;

	for (int i = 0; i < toc->count; i++) {
		TocEntry *e = &toc->entry[i];
		if (e->chapter != chapter || e->offset != TOC_OFFSET_UNKNOWN) continue;
		if (e->anchor[0] == '\0') {
			e->offset = 0;
		} else {
			ids[n] = e->anchor;
			which[n++] = i;
		}
	}
	if (n == 0) return true;

	if (ct_find_anchors(book, chapter, ids, n, offsets) < 0) return false;
	for (int k = 0; k < n; k++) {
		toc->entry[which[k]].offset = offsets[k] == 0xFFFFFFFFu ? 0 : offsets[k]; // not found: the start of the chapter
	}
	return true;
}

int toc_current(const Toc *toc, int chapter, uint32_t offset) {
	int best = -1;
	for (int i = 0; i < toc->count; i++) {
		const TocEntry *e = &toc->entry[i];
		uint32_t at = e->offset == TOC_OFFSET_UNKNOWN ? 0 : e->offset;
		bool before = e->chapter < chapter || (e->chapter == chapter && at <= offset);
		if (!before) continue;

		if (best < 0) {
			best = i;
			continue;
		}
		const TocEntry *b = &toc->entry[best];
		uint32_t bat = b->offset == TOC_OFFSET_UNKNOWN ? 0 : b->offset;
		if (e->chapter > b->chapter || (e->chapter == b->chapter && at >= bat)) best = i;
	}
	return best;
}
