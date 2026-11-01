#include "library.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "input.h"
#include "storage.h"

#define FAILED_MAX 32

// Names of books that failed, kept across scans until the app closes.
static char failed_name[FAILED_MAX][LIBRARY_NAME_MAX];
static int failed_count;
static int failed_next; // where the next name goes once the list is full

static int failed_find(const char *name) {
	for (int i = 0; i < failed_count; i++) {
		if (strcmp(failed_name[i], name) == 0) return i;
	}
	return -1;
}

static bool has_epub_ending(const char *name) {
	size_t len = strlen(name);
	return len > 5 && strcasecmp(name + len - 5, ".epub") == 0;
}

static int title_len(const char *name) {
	return (int)strlen(name) - 5; // only called for names with .epub
}

// Alphabetical by title, ignoring case. The ".epub" ending is not compared,
// so "A B.epub" sorts before "A.epub" and not after it.
static int compare_entries(const void *pa, const void *pb) {
	const char *a = ((const LibraryEntry *)pa)->name;
	const char *b = ((const LibraryEntry *)pb)->name;
	int la = title_len(a), lb = title_len(b);
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

static bool visit(const char *name, bool is_dir, void *user) {
	Library *lib = ((ScanState *)user)->lib;

	if (is_dir || name[0] == '.' || !has_epub_ending(name)) return true;
	if (strlen(name) >= LIBRARY_NAME_MAX || lib->count >= LIBRARY_MAX) {
		lib->incomplete = true;
		return true;
	}

	LibraryEntry *e = &lib->entry[lib->count++];
	memcpy(e->name, name, strlen(name) + 1);
	e->failed = failed_find(name) >= 0;
	return true;
}

bool library_scan(Library *lib) {
	lib->count = 0;
	lib->selected = 0;
	lib->top = 0;
	lib->incomplete = false;
	lib->redraw = true;

	ScanState state = { lib };
	lib->unreadable = !storage_list_dir("/Books", visit, &state);
	if (lib->count > 1) qsort(lib->entry, (size_t)lib->count, sizeof lib->entry[0], compare_entries);
	return !lib->unreadable;
}

void library_path(const Library *lib, int index, char *buf, size_t cap) {
	if (index < 0 || index >= lib->count) {
		if (cap > 0) buf[0] = '\0';
		return;
	}
	snprintf(buf, cap, "/Books/%s", lib->entry[index].name);
}

void library_set_failed(Library *lib, int index, bool failed) {
	if (index < 0 || index >= lib->count) return;
	LibraryEntry *e = &lib->entry[index];
	int known = failed_find(e->name);

	if (failed && known < 0) {
		int slot = failed_count < FAILED_MAX ? failed_count++ : failed_next++ % FAILED_MAX;
		memcpy(failed_name[slot], e->name, strlen(e->name) + 1);
	} else if (!failed && known >= 0) {
		memcpy(failed_name[known], failed_name[failed_count - 1], LIBRARY_NAME_MAX);
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

LibraryEvent library_update(Library *lib, uint32_t actions, bool tapped, int tap_x, int tap_y) {
	(void)tap_x; // rows are all that matters, the list spans the full width
	if (actions & ACT_BACK) return event(LIBRARY_EVENT_BACK, 0);

	if (actions & ACT_UP) select_entry(lib, lib->selected - 1);
	else if (actions & ACT_DOWN) select_entry(lib, lib->selected + 1);
	else if (actions & ACT_LEFT) select_entry(lib, lib->selected - LIBRARY_ROWS);   // page up
	else if (actions & ACT_RIGHT) select_entry(lib, lib->selected + LIBRARY_ROWS);  // page down
	else if ((actions & ACT_CONFIRM) && lib->count > 0) return event(LIBRARY_EVENT_OPEN, lib->selected);
	else if (tapped) {
		int row = tap_y / 8;
		int index = lib->top + (row - LIBRARY_FIRST_ROW);
		if (row >= LIBRARY_FIRST_ROW && row < LIBRARY_FIRST_ROW + LIBRARY_ROWS && index < lib->count) {
			if (index == lib->selected) return event(LIBRARY_EVENT_OPEN, index);
			select_entry(lib, index);
		}
	}
	return event(LIBRARY_EVENT_NONE, 0);
}
