// Main menu and Library backed by the SD card. Settings and the reader are not
// built yet, so opening a book only reports what would happen. Replaced by the
// real app as it is built.
#include <nds.h>
#include <stdio.h>

#include "bookmark_menu.h"
#include "bookmarks.h"
#include "config.h"
#include "epub.h"
#include "input.h"
#include "library.h"
#include "main_menu.h"
#include "message_box.h"
#include "reader.h"
#include "recent.h"
#include "render.h"
#include "settings.h"
#include "storage.h"

typedef enum { SCREEN_MAIN_MENU, SCREEN_LIBRARY, SCREEN_READER, SCREEN_SETTINGS } AppScreen;

static Font ui_font;      // the menus always use the built-in font
static Font reading_font; // the book uses the font from the settings
static Config config;
static RecentList recent;
static MainMenu menu;
static SettingsMenu settings_menu;
static Library library; // large, so not on the stack
static Reader reader;   // the open book while reading
static BookMarks marks; // the bookmarks and last position of the open book
static char book_path[RECENT_PATH_MAX];
static uint32_t book_size;
static int turns_unsaved; // page turns since the position was last saved
static BookmarkMenu bm_menu;
static bool bm_open;     // the bookmark menu is showing

#define SAVE_EVERY_TURNS 10 // how often the position is saved while reading
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
	snprintf(buf, sizeof buf, "Theme: %s  Font: %s",
	         config.theme == THEME_DARK ? "dark" : config.theme == THEME_SEPIA ? "sepia" : "normal", config.font);
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

static bool dual_screen(void) { return config.display == DISPLAY_DUAL; }

// Opens a book and starts reading it. Returns true if it worked. On failure an
// error box is shown.
static bool open_book(const char *path) {
	font_load_named(&reading_font, config.font); // falls back to the built-in font
	int cols, rows;
	reader_geometry(&reading_font, config.margin, &cols, &rows);
	LayoutStyle style = { config.paragraph_gap, config.paragraph_indent };

	// Pick up where the book was left off, and load its bookmarks. A book is
	// recognised by its path and size.
	long size = storage_file_size(path);
	uint32_t book_id = size > 0 ? (uint32_t)size : 0;
	bool known = bookmarks_load(path, book_id, &marks);
	bool resume = known && marks.has_last;

	EpubResult result = reader_open(&reader, path, resume ? marks.last_chapter : 0, resume ? marks.last_offset : 0,
	                                cols, rows, dual_screen() ? 2 : 1, &style);
	if (result != EPUB_OK) {
		show_error(path, result);
		return false;
	}
	snprintf(book_path, sizeof book_path, "%s", path);
	book_size = book_id;
	turns_unsaved = 0;
	bm_open = false;
	if (resume && (marks.last_chapter > 0 || marks.last_offset > 0)) reader_set_notice(&reader, "Resumed");
	render_set_font(&reading_font); // also clears both screens

	recent_touch(&recent, path);
	save_recent();
	rebuild_menu();

	current = SCREEN_READER;
	input_set_context(INPUT_READING);
	reader.redraw = true;
	return true;
}

// Saves the current place and the bookmarks. Returns true if it worked.
static bool save_position(void) {
	marks.has_last = true;
	marks.last_chapter = (uint16_t)reader_chapter(&reader);
	marks.last_offset = reader_offset(&reader);
	turns_unsaved = 0;
	bool ok = bookmarks_save(book_path, book_size, &marks);
	if (!ok) save_failed = true;
	return ok;
}

static void show_main_menu(void) {
	if (current == SCREEN_READER) {
		reader_close(&reader);
		render_set_font(&ui_font);
	}
	current = SCREEN_MAIN_MENU;
	input_set_context(INPUT_MENU);
	main_menu_invalidate(&menu);
	report("Waiting for input", ""); // the top screen shows the logo later
}

static void show_settings(void) {
	settings_open(&settings_menu);
	current = SCREEN_SETTINGS;
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
		show_settings();
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
	}
}

// The bookmark menu is open: its keys are the menu ones (decision 033).
static void update_bookmark_menu(uint32_t actions) {
	int visible = bookmark_menu_visible();
	BmEvent e = bookmark_menu_update(&bm_menu, &marks, actions, visible);

	switch (e.kind) {
	case BM_EVENT_CLOSE:
	case BM_EVENT_JUMP:
		if (e.kind == BM_EVENT_JUMP) {
			const Mark *k = &marks.mark[e.index];
			reader_goto(&reader, k->chapter, k->offset);
			save_position();
		}
		bm_open = false;
		input_set_context(INPUT_READING);
		reader.redraw = true;
		break;
	case BM_EVENT_DELETE:
		marks_remove_at(&marks, e.index);
		save_position();
		bookmark_menu_clamp(&bm_menu, &marks, visible);
		break;
	case BM_EVENT_NONE:
		break;
	}
	if (bm_open && bm_menu.changed) reader.redraw = true; // the box is drawn after the page
}

static void update_settings(uint32_t actions) {
	SettingsEvent e = settings_update(&settings_menu, &config, actions);

	if (e.kind == SETTINGS_EVENT_CHANGED) {
		if (e.setting == SETTING_THEME) render_set_theme(config.theme); // the rest apply to the next book
		save_failed = !config_save(&config);
		settings_menu.redraw = true;
		report("Settings", "Changes are saved at once"); // refreshes the values shown on the top screen
	} else if (e.kind == SETTINGS_EVENT_BACK) {
		show_main_menu();
	}
}

static void update_reader(uint32_t actions) {
	if (bm_open) {
		update_bookmark_menu(actions);
		return;
	}

	bool turned = false;
	if ((actions & ACT_NEXT_PAGE) && reader_next(&reader)) turned = true;
	if ((actions & ACT_PREV_PAGE) && reader_prev(&reader)) turned = true;
	if (turned && ++turns_unsaved >= SAVE_EVERY_TURNS) save_position();

	if (actions & ACT_BOOKMARK) {
		if (reader_toggle_bookmark(&reader, &marks) != TOGGLE_FULL) save_position();
	}
	if (actions & ACT_BOOKMARK_MENU) {
		bookmark_menu_open(&bm_menu, &marks, reader_chapter(&reader), reader_offset(&reader), bookmark_menu_visible());
		bm_open = true;
		input_set_context(INPUT_MENU);
		reader.redraw = true;
	}

	if (actions & ACT_INVERT) {
		// B cycles through the themes: normal, dark, sepia (decision 042).
		config.theme = (Theme)((config.theme + 1) % THEME_COUNT);
		render_set_theme(config.theme);
		save_failed = !config_save(&config);
		reader.redraw = true;
	}
	if (actions & ACT_MENU) {
		save_position();
		show_main_menu();
	}
}

int main(void) {
	render_init();
	font_load_default(&ui_font);
	render_set_font(&ui_font);
	render_set_rotated(false); // menus are never rotated (decision 027)
	input_init();
	input_set_context(INPUT_MENU);

	storage_init();
	config_load(&config);
	recent_load(&recent);
	render_set_theme(config.theme);
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
		} else if (current == SCREEN_LIBRARY) {
			update_library(actions, tapped, x, y);
		} else if (current == SCREEN_SETTINGS) {
			update_settings(actions);
		} else {
			update_reader(actions);
		}

		if (!box_visible) {
			if (current == SCREEN_MAIN_MENU) main_menu_draw(&menu, SCREEN_BOTTOM);
			else if (current == SCREEN_LIBRARY) library_draw(&library, SCREEN_BOTTOM);
			else if (current == SCREEN_SETTINGS) settings_draw(&settings_menu, &config, SCREEN_BOTTOM);
			else if (reader_draw(&reader, dual_screen(), &marks) && bm_open) {
				bookmark_menu_draw(&bm_menu, &marks, reader.first_chapter, SCREEN_BOTTOM);
				bm_menu.changed = false;
			}
		}
		render_present(SCREEN_TOP);
		render_present(SCREEN_BOTTOM);
	}

	return 0;
}
