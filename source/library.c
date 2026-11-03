#include "library.h"

#include "bookname.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "input.h"
#include "storage.h"

#define FAILED_MAX 32

// Names of books that failed, kept across scans until the app closes.
static char failed_name[FAILED_MAX][LIBRARY_DIR_MAX + LIBRARY_NAME_MAX];
static int failed_count;
static int failed_next; // where the next name goes once the list is full

// The key of a book is its path below /Books, so two books of the same name in different folders are told apart.
static void failed_key(const Library *lib, const char *name, char *out, size_t cap) {
	if (lib->dir[0]) snprintf(out, cap, "%s/%s", lib->dir, name);
	else snprintf(out, cap, "%s", name);
}

static int failed_find(const char *key) {
	for (int i = 0; i < failed_count; i++) {
		if (strcmp(failed_name[i], key) == 0) return i;
	}
	return -1;
}

static bool has_book_ending(const char *name) {
	return book_ending_length(name) > 0;
}

static int title_len(const char *name) {
	return (int)strlen(name) - book_ending_length(name); // only called for names that end like a book
}

// Alphabetical by title, ignoring case. The ending (.epub or .txt) is not compared,
// so "A B.epub" sorts before "A.epub" and not after it.
static int compare_entries(const void *pa, const void *pb) {
	const LibraryEntry *ea = pa, *eb = pb;
	if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1; // folders first
	const char *a = ea->name;
	const char *b = eb->name;
	int la = ea->is_dir ? (int)strlen(a) : title_len(a), lb = eb->is_dir ? (int)strlen(b) : title_len(b);
	int n = la < lb ? la : lb;
	for (int i = 0; i < n; i++) {
		int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
		if (ca != cb) return ca - cb;
	}
	if (la != lb) return la - lb;
	return strcmp(a, b); // same title in different case: keep the order stable
}

typedef struct {
	Library *lib;
} ScanState;

static int depth_of(const char *dir) {
	if (!dir[0]) return 0;
	int n = 1;
	for (const char *p = dir; *p; p++) n += *p == '/';
	return n;
}

static bool visit(const char *name, bool is_dir, void *user) {
	Library *lib = ((ScanState *)user)->lib;

	if (name[0] == '.') return true;
	if (!is_dir && !has_book_ending(name)) return true;
	size_t dir_len = strlen(lib->dir), name_len = strlen(name);
	// Left out: too many, a name that is too long, a folder too deep, or a book whose path could not be remembered.
	bool too_long = name_len >= LIBRARY_NAME_MAX ||
	                (is_dir ? dir_len + 1 + name_len >= LIBRARY_DIR_MAX || depth_of(lib->dir) >= LIBRARY_DEPTH_MAX
	                        : strlen("/Books/") + dir_len + 1 + name_len > LIBRARY_BOOK_PATH_MAX);
	if (too_long || lib->count >= LIBRARY_MAX) {
		lib->incomplete = true;
		return true;
	}

	LibraryEntry *e = &lib->entry[lib->count++];
	memcpy(e->name, name, name_len + 1);
	e->is_dir = is_dir;
	char key[LIBRARY_DIR_MAX + LIBRARY_NAME_MAX];
	failed_key(lib, name, key, sizeof key);
	e->failed = !is_dir && failed_find(key) >= 0;
	return true;
}

// Reads the folder `lib->dir`.
static bool scan_dir(Library *lib) {
	lib->count = 0;
	lib->selected = 0;
	lib->top = 0;
	lib->incomplete = false;
	lib->redraw = true;

	char path[LIBRARY_DIR_MAX + 16];
	if (lib->dir[0]) snprintf(path, sizeof path, "/Books/%s", lib->dir);
	else snprintf(path, sizeof path, "/Books");

	ScanState state = { lib };
	lib->unreadable = !storage_list_dir(path, visit, &state);
	if (lib->count > 1) qsort(lib->entry, (size_t)lib->count, sizeof lib->entry[0], compare_entries);
	return !lib->unreadable;
}

bool library_scan(Library *lib) {
	lib->dir[0] = '\0';
	return scan_dir(lib);
}

static void select_entry(Library *lib, int index);

bool library_enter(Library *lib, int index) {
	if (index < 0 || index >= lib->count || !lib->entry[index].is_dir) return false;
	size_t len = strlen(lib->dir);
	if (len + 1 + strlen(lib->entry[index].name) >= LIBRARY_DIR_MAX) return false;
	if (len) lib->dir[len++] = '/';
	strcpy(lib->dir + len, lib->entry[index].name);
	scan_dir(lib);
	return true;
}

bool library_up(Library *lib) {
	if (!lib->dir[0]) return false;
	char left[LIBRARY_NAME_MAX];
	char *slash = strrchr(lib->dir, '/');
	snprintf(left, sizeof left, "%s", slash ? slash + 1 : lib->dir);
	if (slash) *slash = '\0';
	else lib->dir[0] = '\0';
	scan_dir(lib);
	for (int i = 0; i < lib->count; i++) { // the folder we came out of is selected
		if (lib->entry[i].is_dir && strcmp(lib->entry[i].name, left) == 0) {
			select_entry(lib, i);
			break;
		}
	}
	return true;
}

void library_path(const Library *lib, int index, char *buf, size_t cap) {
	if (index < 0 || index >= lib->count) {
		if (cap > 0) buf[0] = '\0';
		return;
	}
	if (lib->dir[0]) snprintf(buf, cap, "/Books/%s/%s", lib->dir, lib->entry[index].name);
	else snprintf(buf, cap, "/Books/%s", lib->entry[index].name);
}

void library_set_failed(Library *lib, int index, bool failed) {
	if (index < 0 || index >= lib->count) return;
	LibraryEntry *e = &lib->entry[index];
	if (e->is_dir) return;
	char key[LIBRARY_DIR_MAX + LIBRARY_NAME_MAX];
	failed_key(lib, e->name, key, sizeof key);
	int known = failed_find(key);

	if (failed && known < 0) {
		int slot = failed_count < FAILED_MAX ? failed_count++ : failed_next++ % FAILED_MAX;
		memcpy(failed_name[slot], key, strlen(key) + 1);
	} else if (!failed && known >= 0) {
		memcpy(failed_name[known], failed_name[failed_count - 1], sizeof failed_name[0]);
		failed_count--;
	}
	e->failed = failed;
	lib->redraw = true;
}

static void select_entry(Library *lib, int index) {
	if (index < 0) index = 0;
	if (index > lib->count - 1) index = lib->count - 1;
	if (index < 0) return;

	if (index != lib->selected) lib->redraw = true;
	lib->selected = index;

	// Keep the selection on screen.
	int top = lib->top;
	if (index < top) top = index;
	if (index >= top + LIBRARY_ROWS) top = index - LIBRARY_ROWS + 1;
	if (top != lib->top) lib->redraw = true;
	lib->top = top;
}

static LibraryEvent event(LibraryEventKind kind, int index) {
	LibraryEvent e = { kind, index };
	return e;
}

// A on a folder goes into it; on a book it opens it.
static LibraryEvent open_entry(Library *lib, int index) {
	if (lib->entry[index].is_dir) {
		library_enter(lib, index);
		return event(LIBRARY_EVENT_NONE, 0);
	}
	return event(LIBRARY_EVENT_OPEN, index);
}

LibraryEvent library_update(Library *lib, uint32_t actions, bool tapped, int tap_x, int tap_y) {
	(void)tap_x; // rows are all that matters, the list spans the full width
	if (actions & ACT_BACK) {
		if (library_up(lib)) return event(LIBRARY_EVENT_NONE, 0);
		return event(LIBRARY_EVENT_BACK, 0);
	}

	if (actions & ACT_UP) select_entry(lib, lib->selected - 1);
	else if (actions & ACT_DOWN) select_entry(lib, lib->selected + 1);
	else if (actions & ACT_LEFT) select_entry(lib, lib->selected - LIBRARY_ROWS);   // page up
	else if (actions & ACT_RIGHT) select_entry(lib, lib->selected + LIBRARY_ROWS);  // page down
	else if ((actions & ACT_CONFIRM) && lib->count > 0) return open_entry(lib, lib->selected);
	else if (tapped) {
		// A book takes the pixel rows of its text and the line of paper under it (and a little above).
		int rel = tap_y - (LIBRARY_FIRST_ROW * 8 - 4);
		int k = rel >= 0 ? rel / (LIBRARY_ROW_STEP * 8) : -1;
		int index = lib->top + k;
		if (k >= 0 && k < LIBRARY_ROWS && index < lib->count) {
			if (index == lib->selected) return open_entry(lib, index);
			select_entry(lib, index);
		}
	}
	return event(LIBRARY_EVENT_NONE, 0);
}
