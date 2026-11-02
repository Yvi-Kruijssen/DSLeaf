#include "bookmark_menu.h"

#include <string.h>

#include "input.h"

static void keep_visible(ListMenu *m, int count, int visible) {
	if (visible < 1) visible = 1;
	if (m->selected >= count) m->selected = count - 1;
	if (m->selected < 0) m->selected = 0;
	if (m->selected < m->top) m->top = m->selected;
	if (m->selected >= m->top + visible) m->top = m->selected - visible + 1;
	int max_top = count > visible ? count - visible : 0;
	if (m->top > max_top) m->top = max_top;
	if (m->top < 0) m->top = 0;
}

// ---- A list of any kind ----

void list_menu_open(ListMenu *m, int count, int start, int visible) {
	m->selected = start;
	m->top = 0;
	m->changed = true;
	keep_visible(m, count, visible);
}

void list_menu_clamp(ListMenu *m, int count, int visible) {
	keep_visible(m, count, visible);
	m->changed = true;
}

void list_menu_select(ListMenu *m, int count, int index, int visible) {
	if (index < 0 || index >= count) return;
	if (m->selected != index) m->changed = true;
	m->selected = index;
	keep_visible(m, count, visible);
}

BmEvent list_menu_update(ListMenu *m, int count, uint32_t actions, int visible, bool can_delete) {
	BmEvent e = { BM_EVENT_NONE, 0 };
	int before = m->selected, before_top = m->top;
	if (visible < 1) visible = 1;

	if (actions & ACT_BACK) {
		e.kind = BM_EVENT_CLOSE;
		return e;
	}
	if (count == 0) return e; // nothing to move over, only B does anything

	if (actions & ACT_UP) m->selected--;
	else if (actions & ACT_DOWN) m->selected++;
	else if (actions & ACT_LEFT) m->selected -= visible;
	else if (actions & ACT_RIGHT) m->selected += visible;
	else if (actions & ACT_CONFIRM) {
		e.kind = BM_EVENT_JUMP;
		e.index = m->selected;
		return e;
	} else if ((actions & ACT_REMOVE) && can_delete) {
		e.kind = BM_EVENT_DELETE;
		e.index = m->selected;
		return e;
	}

	keep_visible(m, count, visible);
	if (m->selected != before || m->top != before_top) m->changed = true;
	return e;
}

// ---- The bookmarks ----

void bookmark_menu_open(BookmarkMenu *m, const BookMarks *marks, int chapter, uint32_t offset, int visible) {
	int start = marks->count > 0 ? marks->count - 1 : 0;
	for (int i = 0; i < marks->count; i++) {
		const Mark *k = &marks->mark[i];
		if (k->chapter > chapter || (k->chapter == chapter && k->offset >= offset)) {
			start = i;
			break;
		}
	}
	list_menu_open(m, marks->count, start, visible);
}

void bookmark_menu_clamp(BookmarkMenu *m, const BookMarks *marks, int visible) {
	list_menu_clamp(m, marks->count, visible);
}

BmEvent bookmark_menu_update(BookmarkMenu *m, const BookMarks *marks, uint32_t actions, int visible) {
	return list_menu_update(m, marks->count, actions, visible, true);
}

// ---- The tabs ----

ReadingTab reading_tab_step(ReadingTab tab, int step, bool with_page) {
	int count = with_page ? 3 : 2;
	int t = ((int)tab + step) % count;
	if (t < 0) t += count;
	return (ReadingTab)t;
}
