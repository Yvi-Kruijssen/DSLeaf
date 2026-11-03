// DSLeaf: the startup logo, then the main menu, Library, Settings and the reader.
#include <nds.h>
#include <stdio.h>

#include "about.h"
#include "bookmark_menu.h"
#include "bookmarks.h"
#include "config.h"
#include "cjk.h"
#include "epub.h"
#include "finished.h"
#include "input.h"
#include "library.h"
#include "main_menu.h"
#include "message_box.h"
#include "pagecache.h"
#include "page_menu.h"
#include "popup.h"
#include "preview.h"
#include "reader.h"
#include "recent.h"
#include "render.h"
#include "settings.h"
#include "startup.h"
#include "storage.h"
#include "toc.h"

typedef enum { SCREEN_MAIN_MENU, SCREEN_LIBRARY, SCREEN_READER, SCREEN_SETTINGS, SCREEN_PREVIEW, SCREEN_ABOUT } AppScreen;

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
static PageMenu page_menu;
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

// The page of each bookmark in the Bookmarks tab (decision 081), kept for the book.
typedef struct { int chapter; uint32_t offset; int page; } MarkPage;
static MarkPage mark_cache[BOOKMARKS_MAX];
static int mark_cache_count, mark_cache_next;
static int bm_pages[BOOKMARKS_MAX]; // for the marks as listed, 0 when not known

// The books that have been read to the end (decision 086), as the text of finished.txt.
static char finished_text[FINISHED_CAP];
static bool book_finished; // the open book is in the list
static int checked_chapter = -1; // the place last looked at
static uint32_t checked_offset;

static void mark_finished_if_at_end(void) {
	// Only looked at when the place changes: finding out whether later chapters have anything can open them.
	if (book_finished) return;
	uint32_t offset = reader_offset(&reader);
	if (reader_chapter(&reader) == checked_chapter && offset == checked_offset) return;
	checked_chapter = reader_chapter(&reader);
	checked_offset = offset;
	if (!reader_at_end(&reader)) return;
	book_finished = true;
	if (finished_add(finished_text, sizeof finished_text, book_path)) finished_save(finished_text);
	reader_set_notice(&reader, "Finished");
}

static bool dual_screen(void) { return config.display == DISPLAY_DUAL; }

// Opens a book and starts reading it. Returns true if it worked. On failure an
// error box is shown.
static bool open_book(const char *path) {
	font_load_named(&reading_font, config.font); // falls back to the built-in font
	reading_font.line_gap = config.line_spacing;  // only pixel fonts use it (decision 050)
	reading_font.safe = config_vmargin_px(&config); // space above and below the text (decision 062)
	font_set_bookmark_glyph(&reading_font);        // the icon of the status row (decision 054)
	int cols, rows;
	reader_geometry(&reading_font, config.margin, config.rotate, &cols, &rows);
	LayoutStyle style = { config.paragraph_gap, config.paragraph_indent, (uint8_t)(font_scripts(&reading_font) | (config.hyphenation ? SCRIPT_HYPHEN : 0)) };

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
	mark_cache_count = mark_cache_next = 0; // other layout, other book
	snprintf(book_path, sizeof book_path, "%s", path);
	book_finished = finished_has(finished_text, path);
	checked_chapter = -1; // a new book: the place is looked at again
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
	PageLayout layout = { reader.cols, reader.rows, reader.style.paragraph_gap, reader.style.paragraph_indent, reader.style.scripts };
	if (n > 0 && n <= PAGECACHE_CHAPTERS_MAX && pagecache_load(book_path, book_size, &layout, page_counts, n)) {
		reader_import_pages(&reader, page_counts, n);
	}
}

// Saves the page counts when there is anything new.
static void save_page_counts(void) {
	if (!reader.open || !reader.pages_changed) return;
	int n = reader.book.chapter_count;
	if (n > PAGECACHE_CHAPTERS_MAX || reader_export_pages(&reader, page_counts, PAGECACHE_CHAPTERS_MAX) == 0) return;
	PageLayout layout = { reader.cols, reader.rows, reader.style.paragraph_gap, reader.style.paragraph_indent, reader.style.scripts };
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
	reading_font.safe = config_vmargin_px(&config);
	int cols, rows;
	reader_geometry(&reading_font, config.margin, config.rotate, &cols, &rows);
	LayoutStyle style = { config.paragraph_gap, config.paragraph_indent, (uint8_t)(font_scripts(&reading_font) | (config.hyphenation ? SCRIPT_HYPHEN : 0)) };
	preview_build(&preview, cols, rows, dual_screen() ? 2 : 1, &style);
	if (dual_screen()) hide_top_logo(); // the top screen shows the first page
	render_set_view(&reading_font, config.rotate);
	preview_draw(&preview, config.footer);
	current = SCREEN_PREVIEW;
}

static void close_preview(void) {
	render_set_view(&ui_font, false);
	show_top_logo();
	settings_menu.redraw = true;
	current = SCREEN_SETTINGS;
}

static About about; // 3 KB of line tables, kept off the stack

static void show_about(void) {
	about_open(&about);
	current = SCREEN_ABOUT;
}

static void show_library(void) {
	library_rescan(&library); // the folder and the selection stay as they were (decision 086)
	library_apply_finished(&library, finished_text);
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
	} else if (e.kind == LIBRARY_EVENT_TOGGLE_DONE) {
		// Select marks a book as read, or takes the mark off (decision 086).
		char path[RECENT_PATH_MAX];
		library_path(&library, e.index, path, sizeof path);
		LibraryEntry *entry = &library.entry[e.index];
		if (entry->done) finished_remove(finished_text, path);
		else finished_add(finished_text, sizeof finished_text, path);
		entry->done = !entry->done;
		library.redraw = true;
		save_failed = !finished_save(finished_text);
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

// The page of each bookmark, shown in the Bookmarks tab (decision 081). Worked out when the tab opens, and kept
// for the book: a page takes a pass over its chapter, which is slow on a DS.

static void fill_mark_pages(void) {
	bool exact = reader_book_pages_exact(&reader);
	for (int i = 0; i < marks.count; i++) {
		const Mark *k = &marks.mark[i];
		bm_pages[i] = 0;
		if (!exact) continue;
		for (int c = 0; c < mark_cache_count; c++) {
			if (mark_cache[c].chapter == (int)k->chapter && mark_cache[c].offset == k->offset) bm_pages[i] = mark_cache[c].page;
		}
		if (bm_pages[i] > 0) continue;
		bm_pages[i] = reader_page_of(&reader, k->chapter, k->offset);
		if (bm_pages[i] > 0) {
			MarkPage *slot = &mark_cache[mark_cache_count < BOOKMARKS_MAX ? mark_cache_count++ : mark_cache_next++ % BOOKMARKS_MAX];
			*slot = (MarkPage){ (int)k->chapter, k->offset, bm_pages[i] };
		}
	}
}

// Opens the box on a tab. The contents start on the entry the reader is in.
static void open_reading_menu(ReadingTab tab) {
	if (tab == TAB_PAGE && !reader_book_pages_exact(&reader)) tab = TAB_BOOKMARKS; // the Page tab needs the counts to be complete
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
	} else if (tab == TAB_PAGE) {
		page_menu_open(&page_menu, reader_book_page(&reader, 0), reader_book_pages(&reader));
	} else {
		fill_mark_pages();
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

// A touch on the pop-up (decision 067). Everything a key does can be done with a touch: a tab, the L and R buttons,
// a row (the first touch selects it and a touch on the selected row goes there), the scrollbar (a page up or down),
// the key buttons (Go, Back, Del), the digits of the Page tab (a touch on a digit picks it, above or below it changes it),
// and outside the pop-up closes it. Most of them become the key they stand for, which is returned, so that keys and
// touches go through the same code. Returns 0 when the touch was handled here (a tab was opened, a row selected).
static uint32_t popup_touch(int x, int y) {
	bool rot = render_is_rotated();
	PopupGeometry g = popup_geometry(rot);
	int lx, ly;
	popup_screen_to_pixel(rot, x, y, &lx, &ly);
	bool with_page = reader_book_pages_exact(&reader);
	PopupHit h = popup_hit(&g, menu_tab, with_page ? 3 : 2, menu_tab == TAB_PAGE ? page_menu.digits : 0, lx, ly);

	ListMenu *list = menu_tab == TAB_CONTENTS ? &toc_menu : &bm_menu;
	int count = menu_tab == TAB_CONTENTS ? toc.count : marks.count;
	switch (h.kind) {
	case POPUP_HIT_OUTSIDE: return ACT_BACK;
	case POPUP_HIT_TAB:
		if (h.index != (int)menu_tab && (h.index != (int)TAB_PAGE || with_page)) open_reading_menu((ReadingTab)h.index);
		return 0;
	case POPUP_HIT_TAB_PREV: open_reading_menu(reading_tab_step(menu_tab, -1, with_page)); return 0;
	case POPUP_HIT_TAB_NEXT: open_reading_menu(reading_tab_step(menu_tab, 1, with_page)); return 0;
	case POPUP_HIT_ROW: {
		int index = list->top + h.index;
		if (index >= count) return 0;
		if (index == list->selected) return ACT_CONFIRM; // a touch on the selected row goes there
		list_menu_select(list, count, index, bookmark_menu_visible());
		if (list->changed) reader.redraw = true;
		return 0;
	}
	case POPUP_HIT_SCROLL_UP: return count > g.rows ? ACT_LEFT : 0;   // a page up or down (Left and Right in the list)
	case POPUP_HIT_SCROLL_DOWN: return count > g.rows ? ACT_RIGHT : 0;
	case POPUP_HIT_HINT:
		if (h.index == POPUP_GO) return ACT_CONFIRM;
		if (h.index == POPUP_BACK) return ACT_BACK;
		return ACT_REMOVE;
	case POPUP_HIT_DIGIT:
	case POPUP_HIT_DIGIT_UP:
	case POPUP_HIT_DIGIT_DOWN:
		if (page_menu_tap(&page_menu, h.index, h.kind == POPUP_HIT_DIGIT_UP ? 1 : (h.kind == POPUP_HIT_DIGIT_DOWN ? -1 : 0))) reader.redraw = true;
		return 0;
	default: return 0;
	}
}

// The box is open: its keys are the menu ones (decision 033). L and R switch tabs.
static void update_bookmark_menu(uint32_t actions, bool tapped, int tap_x, int tap_y) {
	if (tapped) actions |= popup_touch(tap_x, tap_y);
	if (actions & (ACT_TAB_NEXT | ACT_TAB_PREV)) {
		open_reading_menu(reading_tab_step(menu_tab, (actions & ACT_TAB_NEXT) ? 1 : -1, reader_book_pages_exact(&reader)));
		return;
	}
	int visible = bookmark_menu_visible();

	if (menu_tab == TAB_PAGE) {
		PageEvent e = page_menu_update(&page_menu, actions);
		if (e == PAGE_EVENT_GO) {
			reader_goto_book_page(&reader, page_menu.value);
			save_position();
		}
		if (e == PAGE_EVENT_GO || e == PAGE_EVENT_CLOSE) close_reading_menu();
		else if (page_menu.changed) reader.redraw = true;
		return;
	}

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
		for (int i = e.index; i < marks.count; i++) bm_pages[i] = bm_pages[i + 1];
		save_position();
		bookmark_menu_clamp(&bm_menu, &marks, visible);
		break;
	case BM_EVENT_NONE:
		break;
	}
	if (bm_open && bm_menu.changed) reader.redraw = true;
}

static void update_settings(uint32_t actions, bool tapped, int x, int y) {
	if (tapped) actions |= settings_touch(&settings_menu, &config, x, y);
	SettingsEvent e = settings_update(&settings_menu, &config, actions);

	if (e.kind == SETTINGS_EVENT_CHANGED) {
		if (e.setting == SETTING_THEME || e.setting == SETTING_RESET) render_set_theme(config.theme); // the rest apply to the next book
		save_failed = !config_save(&config);
		settings_menu.redraw = true;
	} else if (e.kind == SETTINGS_EVENT_PREVIEW) {
		show_preview();
	} else if (e.kind == SETTINGS_EVENT_ABOUT) {
		show_about();
	} else if (e.kind == SETTINGS_EVENT_BACK) {
		show_main_menu();
	}
}

static void update_reader(uint32_t actions, bool tapped, int tap_x, int tap_y) {
	if (bm_open) {
		update_bookmark_menu(actions, tapped, tap_x, tap_y);
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
	mark_finished_if_at_end();

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
	render_set_ui_font(&ui_font); // the text of pictures (the pop-up over the page) is always 8x8
	render_set_font(&ui_font);
	render_set_rotated(false); // menus are never rotated (decision 027)
	input_init();
	input_set_context(INPUT_MENU);

	// The screens are black while the card and the settings load, so that the logo comes up in the colours of the
	// theme and not in one theme's colours first (decision 095). Then it goes up on the bottom screen (the top one is
	// plain background colour), and it later slides up onto the top screen (decisions 047 and 084).
	render_fade(SCREEN_TOP, -16);
	render_fade(SCREEN_BOTTOM, -16);
	irqSet(IRQ_VBLANK, count_vblank);
	storage_init();
	config_load(&config);
	render_set_theme(config.theme);
	render_logo_start();
	top_logo = true; // it is on the top screen when the slide is over
	render_fade(SCREEN_TOP, 0);
	render_fade(SCREEN_BOTTOM, 0);
	int logo_frame0 = vblanks;

	cjk_load(); // the optional CJK font on the card (decision 092)
	recent_load(&recent);
	finished_load(finished_text, sizeof finished_text);
	rebuild_menu();

	// Three seconds, or until a button or the touchscreen is pressed, then the logo slides up
	// onto the top screen in two seconds. The bottom screen fades out and the main menu fades in.
	Startup startup;
	startup_init(&startup);
	while (startup.phase != STARTUP_DONE && pmMainLoop()) {
		swiWaitForVBlank();
		input_update();

		// The time the card took counts towards the three seconds, up to two seconds of it.
		int elapsed = vblanks - logo_frame0 + (logo_frame0 < 120 ? logo_frame0 : 120);
		render_fade(SCREEN_BOTTOM, startup_update(&startup, elapsed, input_any_pressed()));
		if (startup.phase == STARTUP_SLIDE || startup.slide_done) render_logo_slide(startup.slide);
		if (startup.switch_now) {
			render_logo_slide_end();
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
			update_settings(actions, tapped, x, y);
		} else if (current == SCREEN_ABOUT) {
			if (tapped) actions |= about_touch(x, y);
			if (about_update(&about, actions)) {
				settings_menu.redraw = true;
				current = SCREEN_SETTINGS;
			}
		} else if (current == SCREEN_PREVIEW) {
			if (actions || tapped) close_preview(); // any key or a tap goes back
		} else {
			update_reader(actions, tapped, x, y);
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
			else if (current == SCREEN_ABOUT) about_draw(&about, SCREEN_BOTTOM);
			else if (current == SCREEN_PREVIEW) { /* drawn once when it opened */ }
			else if (reader_draw(&reader, dual_screen(), &marks, config.footer) && bm_open) {
				bool with_page = reader_book_pages_exact(&reader);
				if (menu_tab == TAB_PAGE) page_menu_draw(&page_menu, reader_book_page(&reader, 0), marks.count, SCREEN_BOTTOM);
				else if (menu_tab == TAB_CONTENTS) toc_menu_draw(&toc_menu, &toc, toc_here, NULL, with_page, SCREEN_BOTTOM);
				else bookmark_menu_draw(&bm_menu, &marks, reader.first_chapter, bm_pages, with_page, SCREEN_BOTTOM);
				bm_menu.changed = false;
				page_menu.changed = false;
				toc_menu.changed = false;
			}
		}
		render_present(SCREEN_TOP);
		render_present(SCREEN_BOTTOM);
	}

	return 0;
}
