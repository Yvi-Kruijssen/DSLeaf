// Library screen (docs: ui.md, decisions 016, 024, 036, 085): the folders and the books
// (EPUB and text files) of one folder under /Books, folders first, each sorted alphabetically.
// A on a folder goes in, B goes up (and back to the main menu from /Books itself). The logic
// has no DS code. Drawing is separate, in library_view.c.
#ifndef DSLEAF_LIBRARY_H
#define DSLEAF_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "render.h"

#define LIBRARY_MAX 512      // more entries than this in one folder are left out
#define LIBRARY_DIR_MAX 128  // longest folder path below /Books
#define LIBRARY_DEPTH_MAX 6  // folders inside folders, at most this deep
#define LIBRARY_BOOK_PATH_MAX 199 // a book with a longer path cannot be remembered (bookmarks), so it is left out
#define LIBRARY_NAME_MAX 160 // longer file names are left out
#define LIBRARY_FIRST_ROW 5  // text row of the first book
#define LIBRARY_ROW_STEP 2   // text rows for each book: a line of paper (decision 059)
#define LIBRARY_ROWS 8       // books on screen, at rows 5, 7, ... 19

typedef struct {
	char name[LIBRARY_NAME_MAX]; // file name with its ending, or the name of a folder
	bool is_dir;
	bool failed;
	bool done;                   // the book has been read to the end (finished.h, decision 086)                 // could not be opened earlier in this session
} LibraryEntry;

typedef struct {
	LibraryEntry entry[LIBRARY_MAX];
	char dir[LIBRARY_DIR_MAX]; // the folder shown, below /Books: "" or "Fiction/Old" (no slash at either end)
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
	LIBRARY_EVENT_TOGGLE_DONE, // Select on a book: `index` is the book to mark as read, or to take the mark off
} LibraryEventKind;

typedef struct {
	LibraryEventKind kind;
	int index;
} LibraryEvent;

// Reads /Books (the top folder). Books ending in .epub or .txt (any case) and folders are
// listed; hidden entries and macOS "._" files are skipped. Folders come first, then the books,
// each sorted by title, ignoring case. Books that failed earlier keep their error mark.
// Returns false if the folder could not be read (the list is then empty).
bool library_scan(Library *lib);

// Reads the folder shown again (after a book was read, for example), keeping the folder and the selected
// entry. Falls back to /Books if the folder is gone. Returns false if nothing could be read.
bool library_rescan(Library *lib);

// The folder and the selected entry kept between sessions in LIBRARY_STATE_PATH (decision 110): two lines,
// `dir=Fiction/Old` and `select=Name.epub` (a folder has a `/` after its name). library_state_text writes them;
// library_state_apply reads them (a bad or missing line is ignored, so a damaged file gives the top folder). The
// folder is read when the Library is next shown (library_rescan), which falls back to /Books if it is gone.
#define LIBRARY_STATE_PATH "/DSLeaf/library.txt"
void library_state_text(const Library *lib, char *buf, size_t cap);
void library_state_apply(Library *lib, const char *text);

// Sets the read marks from the text of the list of finished books (finished.h).
void library_apply_finished(Library *lib, const char *text);

// Goes into the folder at `index` (does nothing for a book), or up to the parent folder, which
// is selected in the list. Both read the folder again. library_up returns false at /Books.
bool library_enter(Library *lib, int index);
bool library_up(Library *lib);

// Full path of an entry, such as "/Books/Fiction/Dune.epub" (or of a folder).
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
