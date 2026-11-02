#include "epub.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "storage.h"
#include "txtbook.h"
#include "xml.h"

#define ID_MAX 64   // longest id we can match, including the terminator
#define HREF_MAX 128

EpubResult epub_check(const char *path) {
	FILE *f = storage_fopen(path, "rb");
	if (!f) return EPUB_ERR_READ;
	if (txt_is_path(path)) { // a text file only has to be readable
		fclose(f);
		return EPUB_OK;
	}

	unsigned char head[4];
	size_t n = fread(head, 1, sizeof head, f);
	bool failed = ferror(f);
	fclose(f);

	if (failed) return EPUB_ERR_READ;
	// "PK\3\4" starts a zip file. An empty archive ("PK\5\6") cannot be a book.
	if (n < sizeof head || head[0] != 'P' || head[1] != 'K' || head[2] != 3 || head[3] != 4) {
		return EPUB_ERR_NOT_ZIP;
	}
	return EPUB_OK;
}

const char *epub_error_text(EpubResult result) {
	switch (result) {
	case EPUB_OK:              return "No error";
	case EPUB_ERR_READ:        return "Cannot read the file";
	case EPUB_ERR_NOT_ZIP:     return "Not a valid EPUB";
	case EPUB_ERR_NO_CONTAINER: return "Book info is missing";
	case EPUB_ERR_BAD_PACKAGE: return "Book info is damaged";
	case EPUB_ERR_NO_CHAPTERS: return "No readable text";
	case EPUB_ERR_DRM:         return "Protected by DRM";
	case EPUB_ERR_MEMORY:      return "Out of memory";
	}
	return "Unknown error";
}

static int hex_value(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool epub_resolve_path(const char *base_dir, const char *href, char *out, size_t cap) {
	// Decode the link into a clean path first.
	char link[ZIP_NAME_MAX];
	size_t n = 0;
	for (const char *p = href; *p && *p != '#' && *p != '?'; p++) {
		int c = (unsigned char)*p;
		if (c == '%' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0) {
			c = hex_value(p[1]) * 16 + hex_value(p[2]);
			p += 2;
		}
		if (n >= sizeof link - 1) return false;
		link[n++] = (char)c;
	}
	link[n] = '\0';
	if (n == 0) return false;

	// A link starting with "/" is relative to the root of the zip.
	char joined[ZIP_NAME_MAX * 2];
	if (link[0] == '/') snprintf(joined, sizeof joined, "%s", link + 1);
	else snprintf(joined, sizeof joined, "%s%s", base_dir, link);

	// Resolve "." and ".." one segment at a time.
	size_t len = 0;
	out[0] = '\0';
	const char *seg = joined;
	while (*seg) {
		size_t seg_len = strcspn(seg, "/");
		if (seg_len == 0 || (seg_len == 1 && seg[0] == '.')) {
			// empty or "."
		} else if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
			if (len == 0) return false;
			// Remove the last segment.
			size_t k = len - 1;
			while (k > 0 && out[k - 1] != '/') k--;
			len = k;
			out[len] = '\0';
		} else {
			if (len + seg_len + 2 > cap) return false;
			if (len > 0 && out[len - 1] != '/') out[len++] = '/';
			memcpy(out + len, seg, seg_len);
			len += seg_len;
			out[len] = '\0';
		}
		seg += seg_len;
		if (*seg == '/') seg++;
	}
	return len > 0;
}

// The folder part of a zip path, including the final "/" ("OEBPS/content.opf" gives "OEBPS/").
static void dir_of(const char *path, char *out, size_t cap) {
	const char *slash = strrchr(path, '/');
	size_t n = slash ? (size_t)(slash - path) + 1 : 0;
	if (n >= cap) n = 0;
	memcpy(out, path, n);
	out[n] = '\0';
}

// Reads a zip entry through the XML reader.
typedef struct {
	ZipStream stream;
} XmlSource;

static size_t xml_source_read(void *user, uint8_t *buf, size_t cap) {
	int n = zip_stream_read(&((XmlSource *)user)->stream, buf, cap);
	return n > 0 ? (size_t)n : 0;
}

static bool open_xml(Zip *zip, const char *name, XmlSource *src, XmlReader *reader) {
	ZipEntry entry;
	if (!zip_find(zip, name, &entry)) return false;
	if (!zip_stream_open(zip, &entry, &src->stream)) return false;
	xml_init(reader, xml_source_read, src);
	return true;
}

// ---- META-INF/container.xml ----

static bool read_container(Zip *zip, char *opf_path, size_t cap) {
	XmlSource src;
	XmlReader r;
	if (!open_xml(zip, "META-INF/container.xml", &src, &r)) return false;

	bool found = false;
	XmlEvent e;
	while (!found && (e = xml_next(&r)) != XML_EOF) {
		if (e != XML_START || strcasecmp(r.name, "rootfile") != 0) continue;
		const char *full = xml_attr(&r, "full-path");
		if (full && full[0]) {
			found = epub_resolve_path("", full, opf_path, cap);
		}
	}
	zip_stream_close(&src.stream);
	return found;
}

// ---- DRM ----

// Fonts are often obfuscated, which is not DRM. These two methods are only that.
static bool is_font_obfuscation(const char *algorithm) {
	return strcmp(algorithm, "http://www.idpf.org/2008/embedding") == 0 ||
	       strcmp(algorithm, "http://ns.adobe.com/pdf/enc#RC") == 0;
}

static bool has_drm(Zip *zip) {
	XmlSource src;
	XmlReader r;
	if (!open_xml(zip, "META-INF/encryption.xml", &src, &r)) return false; // none

	bool drm = false;
	XmlEvent e;
	while (!drm && (e = xml_next(&r)) != XML_EOF) {
		if (e != XML_START || strcasecmp(r.name, "EncryptionMethod") != 0) continue;
		const char *alg = xml_attr(&r, "Algorithm");
		if (!alg || !is_font_obfuscation(alg)) drm = true;
	}
	zip_stream_close(&src.stream);
	return drm;
}

// ---- The OPF package file ----

typedef enum { ITEM_OTHER, ITEM_CONTENT, ITEM_NCX, ITEM_NAV } ItemKind;

typedef struct {
	char id[ID_MAX];
	char href[HREF_MAX];
	ItemKind kind;
} ManifestItem;

// The manifest and the spine grow as items are found, so a small book uses
// little memory. Both are freed once the chapter list has been built.
typedef struct {
	ManifestItem *manifest;
	int manifest_count, manifest_cap;
	char (*idref)[ID_MAX];
	int idref_count, idref_cap;
	char toc_id[ID_MAX];
	bool out_of_memory;
} Package;

#define GROW_START 32

// Makes room for one more element. Returns false if memory ran out.
static bool grow(void **array, int count, int *cap, size_t element) {
	if (count < *cap) return true;
	int new_cap = *cap ? *cap * 2 : GROW_START;
	void *bigger = realloc(*array, (size_t)new_cap * element);
	if (!bigger) return false;
	*array = bigger;
	*cap = new_cap;
	return true;
}

static bool has_word(const char *list, const char *word) {
	size_t n = strlen(word);
	for (const char *p = list; *p;) {
		while (*p == ' ') p++;
		if (strncmp(p, word, n) == 0 && (p[n] == ' ' || p[n] == '\0')) return true;
		while (*p && *p != ' ') p++;
	}
	return false;
}

static void copy_trimmed(char *dst, size_t cap, const char *src) {
	// Collapse runs of whitespace and trim the ends.
	size_t n = 0;
	bool space = false;
	for (; *src; src++) {
		if (isspace((unsigned char)*src)) {
			space = n > 0;
			continue;
		}
		if (space && n + 1 < cap) dst[n++] = ' ';
		space = false;
		if (n + 1 < cap) dst[n++] = *src;
	}
	dst[n] = '\0';
}

static EpubResult read_package(Zip *zip, const char *opf_path, Epub *book, Package *pkg) {
	XmlSource src;
	XmlReader r;
	if (!open_xml(zip, opf_path, &src, &r)) return EPUB_ERR_BAD_PACKAGE;

	bool in_title = false, have_title = false;
	char title_raw[EPUB_TITLE_MAX * 2];
	size_t title_len = 0;
	bool saw_package = false;

	XmlEvent e;
	while ((e = xml_next(&r)) != XML_EOF) {
		if (e == XML_TEXT) {
			if (in_title && title_len + r.text_len < sizeof title_raw) {
				memcpy(title_raw + title_len, r.text, r.text_len);
				title_len += r.text_len;
			}
			continue;
		}
		if (e == XML_END) {
			if (in_title && strcasecmp(r.name, "title") == 0) {
				in_title = false;
				have_title = true;
				title_raw[title_len] = '\0';
				copy_trimmed(book->title, sizeof book->title, title_raw);
			}
			continue;
		}

		// A start tag.
		if (strcasecmp(r.name, "package") == 0) {
			saw_package = true;
		} else if (strcasecmp(r.name, "title") == 0 && !have_title && !in_title) {
			in_title = true;
			title_len = 0;
		} else if (strcasecmp(r.name, "item") == 0) {
			const char *id = xml_attr(&r, "id"), *href = xml_attr(&r, "href");
			const char *type = xml_attr(&r, "media-type"), *props = xml_attr(&r, "properties");
			if (!id || !href || !type || strlen(id) >= ID_MAX || strlen(href) >= HREF_MAX) continue;

			ItemKind kind = ITEM_OTHER;
			if (strcasecmp(type, "application/xhtml+xml") == 0 || strcasecmp(type, "text/html") == 0) {
				kind = (props && has_word(props, "nav")) ? ITEM_NAV : ITEM_CONTENT;
			} else if (strcasecmp(type, "application/x-dtbncx+xml") == 0) {
				kind = ITEM_NCX;
			}
			// Images, styles and fonts are not needed, so they are not kept.
			if (kind == ITEM_OTHER || pkg->manifest_count >= EPUB_CHAPTER_MAX * 2) continue;
			if (!grow((void **)&pkg->manifest, pkg->manifest_count, &pkg->manifest_cap, sizeof *pkg->manifest)) {
				pkg->out_of_memory = true;
				break;
			}

			ManifestItem *m = &pkg->manifest[pkg->manifest_count++];
			snprintf(m->id, sizeof m->id, "%s", id);
			snprintf(m->href, sizeof m->href, "%s", href);
			m->kind = kind;
		} else if (strcasecmp(r.name, "spine") == 0) {
			const char *toc = xml_attr(&r, "toc");
			if (toc && strlen(toc) < ID_MAX) snprintf(pkg->toc_id, sizeof pkg->toc_id, "%s", toc);
		} else if (strcasecmp(r.name, "itemref") == 0) {
			const char *idref = xml_attr(&r, "idref");
			if (!idref || strlen(idref) >= ID_MAX || pkg->idref_count >= EPUB_CHAPTER_MAX) {
				book->skipped++;
			} else if (!grow((void **)&pkg->idref, pkg->idref_count, &pkg->idref_cap, ID_MAX)) {
				pkg->out_of_memory = true;
				break;
			} else {
				snprintf(pkg->idref[pkg->idref_count++], ID_MAX, "%s", idref);
			}
		}
	}
	zip_stream_close(&src.stream);
	if (pkg->out_of_memory) return EPUB_ERR_MEMORY;
	return saw_package ? EPUB_OK : EPUB_ERR_BAD_PACKAGE;
}

static const ManifestItem *find_item(const Package *pkg, const char *id) {
	for (int i = 0; i < pkg->manifest_count; i++) {
		if (strcmp(pkg->manifest[i].id, id) == 0) return &pkg->manifest[i];
	}
	return NULL;
}

EpubResult epub_open(Epub *book, const char *path) {
	if (txt_is_path(path)) return txt_open(book, path); // a plain text book (decision 074)
	memset(book, 0, sizeof *book);

	EpubResult check = epub_check(path);
	if (check != EPUB_OK) return check;
	if (!zip_open(&book->zip, path)) return EPUB_ERR_NOT_ZIP;

	EpubResult result;
	Package pkg;
	memset(&pkg, 0, sizeof pkg);
	char opf_path[ZIP_NAME_MAX], opf_dir[ZIP_NAME_MAX];

	if (has_drm(&book->zip)) {
		result = EPUB_ERR_DRM;
		goto fail;
	}
	if (!read_container(&book->zip, opf_path, sizeof opf_path)) {
		result = EPUB_ERR_NO_CONTAINER;
		goto fail;
	}
	dir_of(opf_path, opf_dir, sizeof opf_dir);

	result = read_package(&book->zip, opf_path, book, &pkg);
	if (result != EPUB_OK) goto fail;

	// At most one chapter per spine entry, so that is all the room needed.
	if (pkg.idref_count == 0) {
		result = EPUB_ERR_NO_CHAPTERS;
		goto fail;
	}
	book->chapter = malloc(sizeof *book->chapter * (size_t)pkg.idref_count);
	book->chapter_size = malloc(sizeof *book->chapter_size * (size_t)pkg.idref_count);
	if (!book->chapter || !book->chapter_size) {
		result = EPUB_ERR_MEMORY;
		goto fail;
	}

	// Chapters: the spine in order, as paths inside the zip.
	for (int i = 0; i < pkg.idref_count; i++) {
		const ManifestItem *m = find_item(&pkg, pkg.idref[i]);
		char resolved[EPUB_PATH_MAX];
		ZipEntry entry;
		if (!m || m->kind != ITEM_CONTENT || !epub_resolve_path(opf_dir, m->href, resolved, sizeof resolved) ||
		    !zip_find(&book->zip, resolved, &entry)) {
			book->skipped++;
			continue;
		}
		book->chapter_size[book->chapter_count] = entry.size;
		memcpy(book->chapter[book->chapter_count++], resolved, sizeof resolved);
	}
	if (book->chapter_count == 0) {
		result = EPUB_ERR_NO_CHAPTERS;
		goto fail;
	}

	// Table of contents: an EPUB 3 nav document if there is one, otherwise the NCX.
	const ManifestItem *toc = NULL;
	bool is_nav = false;
	for (int i = 0; i < pkg.manifest_count && !toc; i++) {
		if (pkg.manifest[i].kind == ITEM_NAV) {
			toc = &pkg.manifest[i];
			is_nav = true;
		}
	}
	if (!toc && pkg.toc_id[0]) toc = find_item(&pkg, pkg.toc_id);
	if (!toc) {
		for (int i = 0; i < pkg.manifest_count && !toc; i++) {
			if (pkg.manifest[i].kind == ITEM_NCX) toc = &pkg.manifest[i];
		}
	}
	if (toc && epub_resolve_path(opf_dir, toc->href, book->toc_path, sizeof book->toc_path)) {
		book->toc_is_nav = is_nav || toc->kind == ITEM_NAV;
	} else {
		book->toc_path[0] = '\0';
	}

	free(pkg.manifest);
	free(pkg.idref);
	return EPUB_OK;

fail:
	free(pkg.manifest);
	free(pkg.idref);
	free(book->chapter);
	free(book->chapter_size);
	book->chapter = NULL;
	book->chapter_size = NULL;
	zip_close(&book->zip);
	return result;
}

void epub_close(Epub *book) {
	if (book->txt) {
		txt_close(book);
		return;
	}
	free(book->chapter);
	free(book->chapter_size);
	book->chapter = NULL;
	book->chapter_size = NULL;
	zip_close(&book->zip);
}

bool epub_chapter_open(Epub *book, int index, EpubChapter *chapter) {
	if (book->txt) return txt_chapter_open(book, index, chapter);
	memset(chapter, 0, sizeof *chapter);
	if (index < 0 || index >= book->chapter_count) return false;

	ZipEntry entry;
	if (!zip_find(&book->zip, book->chapter[index], &entry)) return false;
	if (!zip_stream_open(&book->zip, &entry, &chapter->stream)) return false;

	// xml_source_read only needs the stream, which is the first member.
	textx_init(&chapter->text, xml_source_read, chapter);
	return true;
}

bool epub_chapter_next(EpubChapter *chapter, TextItem *item) {
	if (chapter->txt.dec.file) return txt_chapter_next(chapter, item);
	return textx_next(&chapter->text, item);
}

void epub_chapter_close(EpubChapter *chapter) {
	if (chapter->txt.dec.file) {
		txt_chapter_close(chapter);
		return;
	}
	zip_stream_close(&chapter->stream);
}

bool epub_chapter_has_text(Epub *book, int index) {
	static EpubChapter chapter; // too big for the stack
	if (!epub_chapter_open(book, index, &chapter)) return false;

	bool found = false;
	TextItem item;
	while (!found && epub_chapter_next(&chapter, &item)) {
		if (item.kind == TEXT_WORD) found = true;
	}
	epub_chapter_close(&chapter);
	return found;
}
