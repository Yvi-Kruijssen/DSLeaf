// Main menu with placeholder books. Library, Settings and opening a book are
// not built yet, so they only report what would happen. Replaced by the real
// app as it is built.
#include <nds.h>
#include <stdio.h>

#include "input.h"
#include "main_menu.h"
#include "render.h"

static Font font;

static void report(const char *text) {
	render_clear(SCREEN_TOP);
	render_puts(SCREEN_TOP, 1, 1, "DSLeaf", STYLE_HIGHLIGHT);
	render_puts(SCREEN_TOP, 1, 3, "Placeholder screen", STYLE_DIM);
	render_puts_utf8(SCREEN_TOP, 1, 5, text, STYLE_NORMAL);
}

int main(void) {
	render_init();
	font_load_default(&font);
	render_set_font(&font);
	render_set_rotated(false); // menus are never rotated (decision 027)
	input_init();
	input_set_context(INPUT_MENU);

	static const char *const books[] = {
		"The Great Gatsby", "Alice in Wonderland", "Moby Dick",
	};
	MainMenu menu;
	main_menu_init(&menu, books, 3);

	report("Waiting for input");

	while (pmMainLoop()) {
		swiWaitForVBlank();
		input_update();

		int x = 0, y = 0;
		bool tapped = input_tap(&x, &y);
		MainMenuEvent e = main_menu_update(&menu, input_actions(), tapped, x, y);

		char buf[64];
		switch (e.kind) {
		case MENU_EVENT_OPEN_BOOK:
			snprintf(buf, sizeof buf, "Open: %s", menu.title[e.index]);
			report(buf);
			break;
		case MENU_EVENT_OPEN_LIBRARY:  report("Open the Library"); break;
		case MENU_EVENT_OPEN_SETTINGS: report("Open Settings"); break;
		case MENU_EVENT_REMOVE_BOOK:
			snprintf(buf, sizeof buf, "Removed entry %d from the list", e.index + 1);
			report(buf);
			break;
		case MENU_EVENT_NONE: break;
		}

		main_menu_draw(&menu, SCREEN_BOTTOM);
	}

	return 0;
}
