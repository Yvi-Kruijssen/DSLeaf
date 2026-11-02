// Library screen (docs: ui.md, decisions 016, 024, 036): a flat list of the
// EPUB files in /Books, sorted alphabetically. The logic has no DS code.
// Drawing is separate, in library_view.c.
#ifndef DSLEAF_LIBRARY_H
#define DSLEAF_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render.h"

#define LIBRARY_MAX 512      // more files than this are left out
#define LIBRARY_NAME_MAX 160 // longer file names are left out
#define LIBRARY_FIRST_ROW 4  // text row of the first book
#define LIBRARY_ROW_STEP 2   // text rows for each book: a line of paper (decision 059)
#define LIBRARY_ROWS 9       // books on screen, at rows 4, 6, ... 20

typedef struct {
	char name[LIBRARY_NAME_MAX]; // file name in /Books, with the .epub ending
	bool failed;                 // could not be opened earlier in this session
} LibraryEntry;

typedef struct {
	LibraryEntry entry[LIBRARY_MAX];
	int count;
	int selected;
	int top;          // first entry shown
	bool incomplete;  // some files were left out (too many, or a name too long)
	bool unreadable;  // /Books could not be read, for example no SD card
	bool redraw;
} Library;

typedef enum {
	LIBRARY_EVENT_NONE,
	LIBRARY_EVENT_BACK,
	LIBRARY_EVENT_OPEN, // `index` is the entry to open
} LibraryEventKind;

typedef struct {
	LibraryEventKind kind;
	int index;
} LibraryEvent;

// Reads /Books. Only files ending in .epub (any case) are listed. Folders,
// hidden files and macOS "._" files are skipped. The list is sorted by title,
// ignoring case. Books that failed earlier keep their error mark.
// Returns false if the folder could not be read (the list is then empty).
bool library_scan(Library *lib);

// Full path of an entry, such as "/Books/Dune.epub".
void library_path(const Library *lib, int index, char *buf, size_t cap);

// Marks or clears the red error mark. The mark is kept until the app is
// closed, and is not saved (decision 024).
void library_set_failed(Library *lib, int index, bool failed);

// Call once per frame with the actions from the input layer. A tap position is
// optional (touch is only an extra way to do the same things).
LibraryEvent library_update(Library *lib, uint32_t actions, bool tapped, int tap_x, int tap_y);

// Draws if anything changed. Returns true when it drew.
bool library_draw(Library *lib, Screen screen);

#endif
