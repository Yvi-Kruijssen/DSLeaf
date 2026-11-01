// Main menu and Library backed by the SD card. Settings and the reader are not
// built yet, so opening a book only reports what would happen. Replaced by the
// real app as it is built.
#include <nds.h>
#include <stdio.h>

#include "config.h"
#include "epub.h"
#include "input.h"
#include "library.h"
#include "main_menu.h"
#include "message_box.h"
#include "recent.h"
#include "render.h"
#include "storage.h"

typedef enum { SCREEN_MAIN_MENU, SCREEN_LIBRARY } AppScreen;

static Font font;
static Config config;
static RecentList recent;
static MainMenu menu;
static Library library; // large, so not on the stack
static AppScreen current = SCREEN_MAIN_MENU;
static bool box_visible;
static bool save_failed;

// Placeholder top screen. The real one shows the logo.
static void report(const char *line1, const char *line2) {
	render_clear(SCREEN_TOP);
	render_puts(SCREEN_TOP, 1, 1, "DSLeaf", STYLE_HIGHLIGHT);
	render_puts_utf8_clip(SCREEN_TOP, 1, 4, line1, STYLE_NORMAL, 31);
	render_puts_utf8_clip(SCREEN_TOP, 1, 5, line2, STYLE_NORMAL, 31);

	char buf[96];
	render_puts(SCREEN_TOP, 1, 8, storage_available() ? "SD card: found" : "SD card: NOT FOUND",
	            storage_available() ? STYLE_DIM : STYLE_ERROR);
	if (!storage_available()) {
		render_puts(SCREEN_TOP, 1, 9, "Settings will not be saved", STYLE_ERROR);
	} else if (save_failed) {
		render_puts(SCREEN_TOP, 1, 9, "Could not save changes", STYLE_ERROR);
	}
	snprintf(buf, sizeof buf, "Mode: %s", config.display == DISPLAY_SINGLE ? "single" : "dual");
	render_puts(SCREEN_TOP, 1, 11, buf, STYLE_DIM);
	snprintf(buf, sizeof buf, "Invert: %s  Font: %s", config.invert ? "yes" : "no", config.font);
	render_puts(SCREEN_TOP, 1, 12, buf, STYLE_DIM);
	snprintf(buf, sizeof buf, "Recent books: %d", recent.count);
	render_puts(SCREEN_TOP, 1, 13, buf, STYLE_DIM);
}

static void rebuild_menu(void) {
	static char titles[RECENT_MAX][TITLE_MAX];
	const char *list[RECENT_MAX];
	for (int i = 0; i < recent.count; i++) {
		recent_title_from_path(recent.path[i], titles[i], TITLE_MAX);
		list[i] = titles[i];
	}
	main_menu_init(&menu, list, recent.count);
}

static void save_recent(void) {
	save_failed = !recent_save(&recent);
}

static void show_error(const char *path, EpubResult result) {
	char name[TITLE_MAX];
	recent_title_from_path(path, name, sizeof name);
	char reason[48];
	snprintf(reason, sizeof reason, "Reason: %s", epub_error_text(result));
	message_box_draw(SCREEN_BOTTOM, "Couldn't open book", name, reason);
	box_visible = true;
}

// Tries to open a book. Returns true if it worked. A real reader will take over here.
static bool open_book(const char *path) {
	static Epub book; // holds the chapter list, so keep it off the stack
	EpubResult result = epub_open(&book, path);
	if (result != EPUB_OK) {
		show_error(path, result);
		return false;
	}

	char title[EPUB_TITLE_MAX + 8], chapters[32];
	snprintf(title, sizeof title, "Title: %s", book.title[0] ? book.title : "(none)");
	snprintf(chapters, sizeof chapters, "Chapters: %d", book.chapter_count);
	epub_close(&book);

	recent_touch(&recent, path);
	save_recent();
	rebuild_menu();
	report(title, chapters);
	return true;
}

static void show_main_menu(void) {
	current = SCREEN_MAIN_MENU;
	main_menu_invalidate(&menu);
}

static void show_library(void) {
	library_scan(&library);
	current = SCREEN_LIBRARY;
}

static void update_main_menu(uint32_t actions, bool tapped, int x, int y) {
	MainMenuEvent e = main_menu_update(&menu, actions, tapped, x, y);
	char buf[RECENT_PATH_MAX + 8];

	switch (e.kind) {
	case MENU_EVENT_OPEN_BOOK:
		// open_book changes the list, so copy the path first.
		snprintf(buf, sizeof buf, "%s", recent.path[e.index]);
		open_book(buf);
		break;
	case MENU_EVENT_OPEN_LIBRARY:
		show_library();
		break;
	case MENU_EVENT_OPEN_SETTINGS:
		report("Would open Settings", "");
		break;
	case MENU_EVENT_REMOVE_BOOK:
		recent_remove(&recent, e.index);
		save_recent();
		snprintf(buf, sizeof buf, "Removed entry %d from the list", e.index + 1);
		report(buf, "");
		break;
	case MENU_EVENT_NONE:
		break;
	}
}

static void update_library(uint32_t actions, bool tapped, int x, int y) {
	LibraryEvent e = library_update(&library, actions, tapped, x, y);

	if (e.kind == LIBRARY_EVENT_BACK) {
		show_main_menu();
	} else if (e.kind == LIBRARY_EVENT_OPEN) {
		char path[RECENT_PATH_MAX];
		library_path(&library, e.index, path, sizeof path);

		bool ok = open_book(path);
		library_set_failed(&library, e.index, !ok);
		if (ok) show_main_menu(); // the book is now first in Recent Reads
	}
}

int main(void) {
	render_init();
	font_load_default(&font);
	render_set_font(&font);
	render_set_rotated(false); // menus are never rotated (decision 027)
	input_init();
	input_set_context(INPUT_MENU);

	storage_init();
	config_load(&config);
	recent_load(&recent);
	render_set_inverted(config.invert);
	rebuild_menu();

	report("Waiting for input", "");

	while (pmMainLoop()) {
		swiWaitForVBlank();
		input_update();

		uint32_t actions = input_actions();
		int x = 0, y = 0;
		bool tapped = input_tap(&x, &y);

		if (box_visible) {
			// The error box waits for A or B, then the screen underneath comes back.
			if (actions & (ACT_CONFIRM | ACT_BACK)) {
				box_visible = false;
				main_menu_invalidate(&menu);
				library.redraw = true;
			}
		} else if (current == SCREEN_MAIN_MENU) {
			update_main_menu(actions, tapped, x, y);
		} else {
			update_library(actions, tapped, x, y);
		}

		if (!box_visible) {
			if (current == SCREEN_MAIN_MENU) main_menu_draw(&menu, SCREEN_BOTTOM);
			else library_draw(&library, SCREEN_BOTTOM);
		}
	}

	return 0;
}
