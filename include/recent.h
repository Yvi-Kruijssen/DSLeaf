// Recent Reads list saved in /DSLeaf/recent.txt (docs: storage.md, decisions 016, 022).
#ifndef DSLEAF_RECENT_H
#define DSLEAF_RECENT_H

#include <stdbool.h>
#include <stddef.h>

#define RECENT_MAX 3
#define RECENT_PATH_MAX 256
#define RECENT_PATH "/DSLeaf/recent.txt"

typedef struct {
	char path[RECENT_MAX][RECENT_PATH_MAX]; // "/Books/Name.epub", newest first
	int count;
} RecentList;

// Reads paths, one per line. Blank lines, comments, paths that are too long or
// do not start with "/", and repeats are skipped. Stops at RECENT_MAX.
void recent_parse(RecentList *list, const char *text);

// Returns the length, or -1 if `cap` is too small.
int recent_format(const RecentList *list, char *buf, size_t cap);

// Moves `path` to the front, adding it if needed and dropping the oldest entry
// when the list is full. Returns false if the path is not valid.
bool recent_touch(RecentList *list, const char *path);

// Removes the entry at `index` (the position it had). Does nothing if out of range.
void recent_remove(RecentList *list, int index);

// A readable name from a path: the file name without folders and without a
// ".epub" ending. Real book titles from the EPUB come later.
void recent_title_from_path(const char *path, char *buf, size_t cap);

// Loading never fails: a missing file gives an empty list. Returns false if
// there was no readable file.
bool recent_load(RecentList *list);
bool recent_save(const RecentList *list);

#endif
