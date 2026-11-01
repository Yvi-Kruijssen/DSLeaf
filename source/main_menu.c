#include "main_menu.h"

#include <stdio.h>
#include <string.h>

#include "input.h"

#define PRESS_FRAMES 8 // how long the Open button shows as pressed

static MainMenuEvent event(MenuEventKind kind, int index) {
	MainMenuEvent e = { kind, index };
	return e;
}

static void set_focus(MainMenu *m, MenuFocus focus, int book) {
	if (m->focus != focus || m->book != book) m->redraw = true;
	m->focus = focus;
	m->book = book;
}

void main_menu_init(MainMenu *m, const char *const *titles, int count) {
	memset(m, 0, sizeof *m);
	if (count > RECENT_MAX) count = RECENT_MAX;
	for (int i = 0; i < count; i++) {
		strncpy(m->title[i], titles[i], TITLE_MAX - 1); // memset left a terminator
	}
	m->count = count;
	m->focus = count > 0 ? FOCUS_BOOK : FOCUS_LIBRARY;
	m->redraw = true;
}

static void start_open(MainMenu *m, int book) {
	m->press_frames = PRESS_FRAMES;
	m->press_book = book;
	m->redraw = true;
}

// Returns true if the actions moved the cursor.
static bool navigate(MainMenu *m, uint32_t actions) {
	if (m->focus == FOCUS_BOOK) {
		if (actions & ACT_UP) {
			if (m->book > 0) set_focus(m, FOCUS_BOOK, m->book - 1);
			return true;
		}
		if (actions & ACT_DOWN) {
			if (m->book < m->count - 1) set_focus(m, FOCUS_BOOK, m->book + 1);
			else set_focus(m, FOCUS_LIBRARY, 0);
			return true;
		}
		return (actions & (ACT_LEFT | ACT_RIGHT)) != 0;
	}

	// Button row. Open is greyed out here, so Library is the leftmost button.
	if (actions & ACT_UP) {
		if (m->count > 0) set_focus(m, FOCUS_BOOK, m->count - 1);
		return true;
	}
	if (actions & ACT_RIGHT) {
		if (m->focus == FOCUS_LIBRARY) set_focus(m, FOCUS_SETTINGS, 0);
		return true;
	}
	if (actions & ACT_LEFT) {
		if (m->focus == FOCUS_SETTINGS) set_focus(m, FOCUS_LIBRARY, 0);
		return true;
	}
	return (actions & ACT_DOWN) != 0;
}

static MainMenuEvent remove_focused(MainMenu *m) {
	int removed = m->book;
	for (int i = removed; i < m->count - 1; i++) {
		memcpy(m->title[i], m->title[i + 1], TITLE_MAX);
	}
	m->count--;
	memset(m->title[m->count], 0, TITLE_MAX);

	if (m->count == 0) set_focus(m, FOCUS_LIBRARY, 0);
	else if (m->book >= m->count) set_focus(m, FOCUS_BOOK, m->count - 1);
	m->redraw = true;
	return event(MENU_EVENT_REMOVE_BOOK, removed);
}

static bool on_button(int x, int y, int i) {
	// A few pixels more than the drawn button, so a finger does not have to be exact.
	return x >= MENU_BUTTON_X(i) - 2 && x < MENU_BUTTON_X(i) + MENU_BUTTON_W + 2 && y >= MENU_BUTTON_Y - 4 &&
	       y < MENU_BUTTON_Y + MENU_BUTTON_H + 4;
}

static MainMenuEvent handle_tap(MainMenu *m, int x, int y) {
	for (int i = 0; i < m->count; i++) {
		if (y >= MENU_BAND_Y(i) && y < MENU_BAND_Y(i) + MENU_BAND_H) {
			set_focus(m, FOCUS_BOOK, i);
			return event(MENU_EVENT_NONE, 0);
		}
	}
	if (on_button(x, y, 0)) {
		if (m->focus == FOCUS_BOOK) start_open(m, m->book); // greyed out otherwise
		return event(MENU_EVENT_NONE, 0);
	}
	if (on_button(x, y, 1)) {
		set_focus(m, FOCUS_LIBRARY, 0);
		return event(MENU_EVENT_OPEN_LIBRARY, 0);
	}
	if (on_button(x, y, 2)) {
		set_focus(m, FOCUS_SETTINGS, 0);
		return event(MENU_EVENT_OPEN_SETTINGS, 0);
	}
	return event(MENU_EVENT_NONE, 0);
}

MainMenuEvent main_menu_update(MainMenu *m, uint32_t actions, bool tapped, int tap_x, int tap_y) {
	// While the Open button shows as pressed, input is ignored.
	if (m->press_frames > 0) {
		if (--m->press_frames == 0) {
			m->redraw = true;
			return event(MENU_EVENT_OPEN_BOOK, m->press_book);
		}
		return event(MENU_EVENT_NONE, 0);
	}

	if (navigate(m, actions)) return event(MENU_EVENT_NONE, 0);

	if (actions & ACT_CONFIRM) {
		switch (m->focus) {
		case FOCUS_BOOK:
			start_open(m, m->book);
			return event(MENU_EVENT_NONE, 0);
		case FOCUS_LIBRARY:  return event(MENU_EVENT_OPEN_LIBRARY, 0);
		case FOCUS_SETTINGS: return event(MENU_EVENT_OPEN_SETTINGS, 0);
		}
	}

	if ((actions & ACT_REMOVE) && m->focus == FOCUS_BOOK) return remove_focused(m);

	// B does nothing on the main menu (decision 015).

	if (tapped) return handle_tap(m, tap_x, tap_y);

	return event(MENU_EVENT_NONE, 0);
}

void main_menu_invalidate(MainMenu *m) { m->redraw = true; }

void main_menu_set_notice(MainMenu *m, const char *text) {
	char next[sizeof m->notice] = "";
	if (text) snprintf(next, sizeof next, "%s", text);
	if (strcmp(next, m->notice) == 0) return;
	memcpy(m->notice, next, sizeof next);
	m->redraw = true;
}
