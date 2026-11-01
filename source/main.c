// Main menu backed by the SD card: settings from config.txt and the Recent
// Reads list from recent.txt. Library, Settings and opening a book are not
// built yet, so they only report what would happen. Replaced by the real app
// as it is built.
#include <nds.h>
#include <stdio.h>

#include "config.h"
#include "input.h"
#include "main_menu.h"
#include "recent.h"
#include "render.h"
#include "storage.h"

static Font font;
static Config config;
static RecentList recent;
static MainMenu menu;
static bool save_failed;

// Placeholder top screen. The real one shows the logo.
static void report(const char *line1, const char *line2) {
	render_clear(SCREEN_TOP);
	render_puts(SCREEN_TOP, 1, 1, "DSLeaf", STYLE_HIGHLIGHT);
	render_puts_utf8(SCREEN_TOP, 1, 4, line1, STYLE_NORMAL);
	render_puts_utf8(SCREEN_TOP, 1, 5, line2, STYLE_NORMAL);

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

		int x = 0, y = 0;
		bool tapped = input_tap(&x, &y);
		MainMenuEvent e = main_menu_update(&menu, input_actions(), tapped, x, y);

		char buf[RECENT_PATH_MAX + 8];
		switch (e.kind) {
		case MENU_EVENT_OPEN_BOOK:
			// Opening moves the book to the top of Recent Reads.
			snprintf(buf, sizeof buf, "%s", recent.path[e.index]);
			recent_touch(&recent, buf);
			save_recent();
			rebuild_menu();
			report("Would open:", buf);
			break;
		case MENU_EVENT_OPEN_LIBRARY:  report("Would open the Library", ""); break;
		case MENU_EVENT_OPEN_SETTINGS: report("Would open Settings", ""); break;
		case MENU_EVENT_REMOVE_BOOK:
			recent_remove(&recent, e.index);
			save_recent();
			snprintf(buf, sizeof buf, "Removed entry %d from the list", e.index + 1);
			report(buf, "");
			break;
		case MENU_EVENT_NONE: break;
		}

		main_menu_draw(&menu, SCREEN_BOTTOM);
	}

	return 0;
}
