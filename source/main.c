// DSLeaf: the startup logo, then the main menu, Library, Settings and the reader.
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
#include "pagecache.h"
#include "preview.h"
#include "reader.h"
#include "recent.h"
#include "render.h"
#include "settings.h"
#include "startup.h"
#include "storage.h"
#include "toc.h"

typedef enum { SCREEN_MAIN_MENU, SCREEN_LIBRARY, SCREEN_READER, SCREEN_SETTINGS, SCREEN_PREVIEW } AppScreen;

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
static bool bm_open;     // the bookmark and contents box is showing
static ReadingTab menu_tab = TAB_BOOKMARKS; // the tab that was used last
static Toc toc;          // the table of contents of the open book, read when first needed
static bool toc_tried;   // reading it has been tried for this book
static ListMenu toc_menu;
static int toc_here;     // the entry the reader is in, or -1

#define SAVE_EVERY_TURNS 10 // how often the position is saved while reading

// Counting the pages of the rest of the book while the reader is idle (decision 055).
#define COUNT_AFTER_FRAMES 90  // idle frames (1.5 s) before a counting step
#define COUNT_GAP_FRAMES 45    // idle frames between two steps
#define SAVE_COUNTS_FRAMES 300 // idle frames (5 s) before new counts are saved
static int idle_frames;
static bool counting_done;     // every chapter is known, or counting is not possible
static uint16_t page_counts[PAGECACHE_CHAPTERS_MAX];
static void load_page_counts(void);
static AppScreen current = SCREEN_MAIN_MENU;
static bool box_visible;
static bool save_failed;

// The warning on the main menu: the SD card is missing, or saving did not work.
static void apply_menu_notice(void) {
	main_menu_set_notice(&menu, !storage_available() ? "No SD card: nothing is saved"
	                            : save_failed         ? "Could not save changes"
	                                                  : NULL);
}

// The logo on the top screen. It stays there in the menus, and while reading
// in single-screen mode, because only the bottom screen is used then.
static bool top_logo;

static void show_top_logo(void) {
	if (top_logo) return;
	render_show_logo(SCREEN_TOP);
	top_logo = true;
}

static void hide_top_logo(void) {
	if (!top_logo) return;
	render_hide_logo(SCREEN_TOP);
	top_logo = false;
}

static void rebuild_menu(void) {
	static char titles[RECENT_MAX][TITLE_MAX];
	const char *list[RECENT_MAX];
	for (int i = 0; i < recent.count; i++) {
		recent_title_from_path(recent.path[i], titles[i], TITLE_MAX);
		list[i] = titles[i];
	}
	main_menu_init(&menu, list, recent.count);
	apply_menu_notice();
}

static void save_recent(void) {
	save_failed = !recent_save(&recent);
	apply_menu_notice();
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
	reading_font.line_gap = config.line_spacing;  // only pixel fonts use it (decision 050)
	font_set_bookmark_glyph(&reading_font);        // the icon of the status row (decision 054)
	int cols, rows;
	reader_geometry(&reading_font, config.margin, config.rotate, &cols, &rows);
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
	toc_free(&toc);
	toc_tried = false;
	snprintf(book_path, sizeof book_path, "%s", path);
	book_size = book_id;
	load_page_counts();
	idle_frames = 0;
	counting_done = false;
	turns_unsaved = 0;
	bm_open = false;
	if (resume && (marks.last_chapter > 0 || marks.last_offset > 0)) reader_set_notice(&reader, "Resumed");
	if (dual_screen()) hide_top_logo(); // the top screen shows the second page
	render_set_view(&reading_font, config.rotate); // also clears both screens. Menus are never rotated (decision 049)

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

// Loads the saved page counts of the open book, if they are for this page layout.
static void load_page_counts(void) {
	int n = reader.book.chapter_count;
	PageLayout layout = { reader.cols, reader.rows, reader.style.paragraph_gap, reader.style.paragraph_indent };
	if (n > 0 && n <= PAGECACHE_CHAPTERS_MAX && pagecache_load(book_path, book_size, &layout, page_counts, n)) {
		reader_import_pages(&reader, page_counts, n);
	}
}

// Saves the page counts when there is anything new.
static void save_page_counts(void) {
	if (!reader.open || !reader.pages_changed) return;
	int n = reader.book.chapter_count;
	if (n > PAGECACHE_CHAPTERS_MAX || reader_export_pages(&reader, page_counts, PAGECACHE_CHAPTERS_MAX) == 0) return;
	PageLayout layout = { reader.cols, reader.rows, reader.style.paragraph_gap, reader.style.paragraph_indent };
	if (pagecache_save(book_path, book_size, &layout, page_counts, n)) reader.pages_changed = false;
}

static void show_main_menu(void) {
	if (current == SCREEN_READER) {
		save_page_counts();
		toc_free(&toc);
		toc_tried = false;
		reader_close(&reader);
		render_set_view(&ui_font, false);
	}
	current = SCREEN_MAIN_MENU;
	input_set_context(INPUT_MENU);
	main_menu_invalidate(&menu);
	apply_menu_notice();
	show_top_logo();
}

static void show_settings(void) {
	settings_open(&settings_menu);
	current = SCREEN_SETTINGS;
}

// The sample page of the Settings screen, laid out like a book would be.
static Preview preview; // 4 KB, kept off the stack

static void show_preview(void) {
	font_load_named(&reading_font, config.font);
	reading_font.line_gap = config.line_spacing;
	int cols, rows;
	reader_geometry(&reading_font, config.margin, config.rotate, &cols, &rows);
	LayoutStyle style = { config.paragraph_gap, config.paragraph_indent };
	preview_build(&preview, cols, rows, dual_screen() ? 2 : 1, &style);
	if (dual_screen()) hide_top_logo(); // the top screen shows the first page
	render_set_view(&reading_font, config.rotate);
	preview_draw(&preview);
	current = SCREEN_PREVIEW;
}

static void close_preview(void) {
	render_set_view(&ui_font, false);
	show_top_logo();
	settings_menu.redraw = true;
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

// Reads the table of contents the first time it is needed, which can take a moment for a big one.
static void ensure_toc(void) {
	if (toc_tried) return;
	toc_tried = true;
	toc_load(&toc, &reader.book);
}

static void close_reading_menu(void) {
	bm_open = false;
	input_set_context(INPUT_READING);
	reader.redraw = true;
}

// Opens the box on a tab. The contents start on the entry the reader is in.
static void open_reading_menu(ReadingTab tab) {
	menu_tab = tab;
	int visible = bookmark_menu_visible();

	if (tab == TAB_CONTENTS) {
		ensure_toc();
		toc_here = -1;
		if (toc.count > 0) {
			// Where the entries of this chapter are is looked up first, so the right one is found.
			toc_resolve(&toc, &reader.book, reader_chapter(&reader));
			toc_here = toc_current(&toc, reader_chapter(&reader), reader_offset(&reader));
		}
		list_menu_open(&toc_menu, toc.count, toc_here >= 0 ? toc_here : 0, visible);
	} else {
		bookmark_menu_open(&bm_menu, &marks, reader_chapter(&reader), reader_offset(&reader), visible);
	}
	bm_open = true;
	input_set_context(INPUT_MENU);
	reader.redraw = true;
}

// Goes to a table of contents entry.
static void go_to_entry(int index) {
	TocEntry *e = &toc.entry[index];
	if (e->offset == TOC_OFFSET_UNKNOWN) toc_resolve(&toc, &reader.book, e->chapter);
	reader_goto(&reader, e->chapter, e->offset == TOC_OFFSET_UNKNOWN ? 0 : e->offset);
	save_position();
}

// The box is open: its keys are the menu ones (decision 033). L and R switch tabs.
static void update_bookmark_menu(uint32_t actions) {
	if (actions & (ACT_TAB_NEXT | ACT_TAB_PREV)) {
		open_reading_menu(menu_tab == TAB_CONTENTS ? TAB_BOOKMARKS : TAB_CONTENTS);
		return;
	}
	int visible = bookmark_menu_visible();

	if (menu_tab == TAB_CONTENTS) {
		BmEvent e = list_menu_update(&toc_menu, toc.count, actions, visible, false);
		if (e.kind == BM_EVENT_JUMP) go_to_entry(e.index);
		if (e.kind == BM_EVENT_JUMP || e.kind == BM_EVENT_CLOSE) close_reading_menu();
		else if (toc_menu.changed) reader.redraw = true; // the box is drawn after the page
		return;
	}

	BmEvent e = bookmark_menu_update(&bm_menu, &marks, actions, visible);
	switch (e.kind) {
	case BM_EVENT_CLOSE:
	case BM_EVENT_JUMP:
		if (e.kind == BM_EVENT_JUMP) {
			const Mark *k = &marks.mark[e.index];
			reader_goto(&reader, k->chapter, k->offset);
			save_position();
		}
		close_reading_menu();
		break;
	case BM_EVENT_DELETE:
		marks_remove_at(&marks, e.index);
		save_position();
		bookmark_menu_clamp(&bm_menu, &marks, visible);
		break;
	case BM_EVENT_NONE:
		break;
	}
	if (bm_open && bm_menu.changed) reader.redraw = true;
}

static void update_settings(uint32_t actions) {
	SettingsEvent e = settings_update(&settings_menu, &config, actions);

	if (e.kind == SETTINGS_EVENT_CHANGED) {
		if (e.setting == SETTING_THEME || e.setting == SETTING_RESET) render_set_theme(config.theme); // the rest apply to the next book
		save_failed = !config_save(&config);
		settings_menu.redraw = true;
	} else if (e.kind == SETTINGS_EVENT_PREVIEW) {
		show_preview();
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
	if (actions & ACT_BOOKMARK_MENU) open_reading_menu(menu_tab);

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

static volatile int vblanks; // frames since the program started, counted in the vertical blank interrupt

static void count_vblank(void) { vblanks++; }

int main(void) {
	render_init();
	font_load_default(&ui_font);
	render_set_font(&ui_font);
	render_set_rotated(false); // menus are never rotated (decision 027)
	input_init();
	input_set_context(INPUT_MENU);

	// The logo goes up on both screens straight away, and the card and the
	// settings load while it is showing (decision 047).
	irqSet(IRQ_VBLANK, count_vblank);
	render_show_logo(SCREEN_TOP);
	top_logo = true;
	render_show_logo(SCREEN_BOTTOM);

	storage_init();
	config_load(&config);
	recent_load(&recent);
	render_set_theme(config.theme);
	rebuild_menu();

	// Three seconds, or until a button or the touchscreen is pressed. The bottom
	// screen fades out and the main menu fades in. The top screen keeps the logo.
	Startup startup;
	startup_init(&startup);
	while (startup.phase != STARTUP_DONE && pmMainLoop()) {
		swiWaitForVBlank();
		input_update();

		render_fade(SCREEN_BOTTOM, startup_update(&startup, vblanks, input_any_pressed()));
		if (startup.switch_now) {
			render_hide_logo(SCREEN_BOTTOM); // black now: swap the logo for the menu
			main_menu_invalidate(&menu);
			main_menu_draw(&menu, SCREEN_BOTTOM);
			render_present(SCREEN_BOTTOM);
		}
	}
	render_fade(SCREEN_BOTTOM, 0);

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
		} else if (current == SCREEN_PREVIEW) {
			if (actions || tapped) close_preview(); // any key or a tap goes back
		} else {
			update_reader(actions);
		}

		// While reading and idle, count the pages of the chapters not seen yet, one small step at a time.
		if (current == SCREEN_READER && !bm_open && !box_visible) {
			if (actions || tapped) {
				idle_frames = 0;
			} else {
				idle_frames++;
				if (!counting_done && idle_frames >= COUNT_AFTER_FRAMES) {
					bool was_exact = reader_book_pages_exact(&reader);
					counting_done = !reader_count_step(&reader);
					idle_frames = COUNT_AFTER_FRAMES - COUNT_GAP_FRAMES;
					if (reader_book_pages_exact(&reader) != was_exact) reader.redraw = true; // "calculating..." becomes the page count
				}
				if (idle_frames >= SAVE_COUNTS_FRAMES || (counting_done && reader.pages_changed && idle_frames >= COUNT_AFTER_FRAMES)) save_page_counts();
			}
		}

		if (!box_visible) {
			if (current == SCREEN_MAIN_MENU) main_menu_draw(&menu, SCREEN_BOTTOM);
			else if (current == SCREEN_LIBRARY) library_draw(&library, SCREEN_BOTTOM);
			else if (current == SCREEN_SETTINGS) settings_draw(&settings_menu, &config, SCREEN_BOTTOM);
			else if (current == SCREEN_PREVIEW) { /* drawn once when it opened */ }
			else if (reader_draw(&reader, dual_screen(), &marks, config.footer) && bm_open) {
				if (menu_tab == TAB_CONTENTS) toc_menu_draw(&toc_menu, &toc, toc_here, marks.count, SCREEN_BOTTOM);
				else bookmark_menu_draw(&bm_menu, &marks, reader.first_chapter, SCREEN_BOTTOM);
				bm_menu.changed = false;
				toc_menu.changed = false;
			}
		}
		render_present(SCREEN_TOP);
		render_present(SCREEN_BOTTOM);
	}

	return 0;
}
