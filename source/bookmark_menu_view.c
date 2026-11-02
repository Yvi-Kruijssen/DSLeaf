// The pop-up menu over the reading page (decisions 066, 067 and 071): a panel with a shadow, a bar of tabs with an
// L and an R button, a list with a selection bar and a scrollbar, and the keys as small buttons. It is a picture
// drawn over the page, in the 8x8 menu font whatever font the book uses, and it turns with the page when the page
// is rotated. Where things are is worked out in popup.c, which touch uses too.
#include <stdio.h>
#include <string.h>

#include "bookmark_menu.h"
#include "popup.h"

int bookmark_menu_visible(void) {
	return popup_geometry(render_is_rotated()).rows;
}

// A filled rectangle with rounded corners (two pixels at the most).
static void round_rect(Screen screen, int x, int y, int w, int h, BackdropColour colour) {
	render_backdrop_rect(screen, x + 2, y, w - 4, 1, colour);
	render_backdrop_rect(screen, x + 1, y + 1, w - 2, 1, colour);
	render_backdrop_rect(screen, x, y + 2, w, h - 4, colour);
	render_backdrop_rect(screen, x + 1, y + h - 2, w - 2, 1, colour);
	render_backdrop_rect(screen, x + 2, y + h - 1, w - 4, 1, colour);
}

// A small key: the letters on a light button.
static void key_button(Screen screen, int x, int y, const char *keys) {
	int w = (int)strlen(keys) * 8 + 4;
	round_rect(screen, x, y, w, 12, BD_BTN_EDGE);
	round_rect(screen, x + 1, y + 1, w - 2, 10, BD_BTN);
	render_backdrop_rect(screen, x + 3, y + 1, w - 6, 1, BD_BTN_LIGHT);
	render_backdrop_text(screen, x + 2, y + 2, keys, BD_BTN_TEXT);
}

// The icon of a tab, 8 by 8 pixels: lines for the contents, a ribbon for the bookmarks, a page for the page number.
static void tab_icon(Screen screen, int x, int y, ReadingTab tab, BackdropColour c) {
	switch (tab) {
	case TAB_CONTENTS:
		render_backdrop_rect(screen, x, y + 1, 8, 1, c);
		render_backdrop_rect(screen, x, y + 4, 8, 1, c);
		render_backdrop_rect(screen, x, y + 7, 8, 1, c);
		break;
	case TAB_BOOKMARKS:
		render_backdrop_rect(screen, x + 1, y, 6, 6, c);          // the ribbon with a V cut in its lower edge
		render_backdrop_rect(screen, x + 1, y + 6, 2, 2, c);
		render_backdrop_rect(screen, x + 5, y + 6, 2, 2, c);
		render_backdrop_rect(screen, x + 3, y + 5, 2, 1, BD_RULE);
		break;
	default:
		render_backdrop_rect(screen, x + 1, y, 6, 1, c);          // a sheet: outline and two lines
		render_backdrop_rect(screen, x + 1, y + 7, 6, 1, c);
		render_backdrop_rect(screen, x + 1, y, 1, 8, c);
		render_backdrop_rect(screen, x + 6, y, 1, 8, c);
		render_backdrop_rect(screen, x + 3, y + 3, 2, 1, c);
		render_backdrop_rect(screen, x + 3, y + 5, 2, 1, c);
		break;
	}
}

// The corners of the pop-up (decision 071): the two top corners are the shoulder buttons of a DS, with a larger round than
// the bottom ones. A shape is drawn row by row; these say how far in each row starts.
static const int corner_top_outer[] = { 4, 2, 1 };
static const int corner_top_inner[] = { 2, 1, 0 };     // one pixel in from the outline, which is the outer round
static const int corner_bottom_outer[] = { 2, 1 };
static const int corner_bottom_inner[] = { 1, 0 };

static void panel_shape(Screen screen, int x, int y, int w, int h, bool inner, BackdropColour colour) {
	const int *top = inner ? corner_top_inner : corner_top_outer, *bottom = inner ? corner_bottom_inner : corner_bottom_outer;
	for (int row = 0; row < h; row++) {
		int in = row < 3 ? top[row] : (h - 1 - row < 2 ? bottom[h - 1 - row] : 0);
		render_backdrop_rect(screen, x + in, y + row, w - 2 * in, 1, colour);
	}
}

// One shoulder button: a top corner of the pop-up, with the round of the DS shoulder buttons on its outer top corner,
// an outline, a face with a light top edge and a shaded lower edge, and the letter. The outline is the outline of the panel
// at its outer side, so the corner of the pop-up is the corner of the button.
static int round_in(int row) {
	return row < 3 ? corner_top_outer[row] : 0;
}

static void shoulder_shape(Screen screen, const PopupRect *r, bool left, bool face, BackdropColour colour) {
	int from = face ? 1 : 0, to = face ? r->h - 1 : r->h;   // the face leaves the outline on the top, the bottom and the inner side
	for (int row = from; row < to; row++) {
		int in = round_in(row);
		int x = left ? r->x + in : r->x, w = r->w - in;
		if (face) {
			x = left ? x + 1 : r->x + 1;
			w -= 2;
		}
		render_backdrop_rect(screen, x, r->y + row, w, 1, colour);
	}
}

static void shoulder_button(Screen screen, const PopupRect *r, bool left, const char *letter) {
	shoulder_shape(screen, r, left, false, BD_BTN_EDGE);
	shoulder_shape(screen, r, left, true, BD_BTN);
	render_backdrop_rect(screen, r->x + (left ? 6 : 2), r->y + 1, r->w - 8, 1, BD_BTN_LIGHT);
	render_backdrop_rect(screen, r->x + 2, r->y + r->h - 3, r->w - 4, 1, BD_BTN_SHADE);
	render_backdrop_text(screen, r->x + (r->w - 8) / 2, r->y + (r->h - 8) / 2, letter, BD_BTN_TEXT);
}

// The tab bar: the top strip of the panel, over its whole width. The L and R buttons are its two top corners and the tabs
// are between them. The tab in use reaches up to the top edge and is joined to the panel below it, in the green ink;
// the others are dim text on the strip.
static void tab_bar(Screen screen, const PopupGeometry *g, int tabs, ReadingTab active) {
	render_backdrop_rect(screen, g->x + 1, g->y + 1, g->w - 2, POPUP_HEADER_H - 1, BD_RULE);

	PopupTabBox boxes[POPUP_TABS_MAX];
	PopupRect prev, next;
	popup_tab_layout(g, tabs, active, boxes, &prev, &next);

	// The line under the strip, which the tab in use leaves out.
	render_backdrop_rect(screen, g->x + 1, g->y + POPUP_HEADER_H, g->w - 2, 1, BD_EDGE);
	shoulder_button(screen, &prev, true, "L");
	shoulder_button(screen, &next, false, "R");

	for (int i = 0; i < tabs; i++) {
		const PopupTabBox *t = &boxes[i];
		BackdropColour ink = t->active ? BD_ACCENT : BD_BTN_DIM;
		int text_y = t->box.y + (t->active ? 4 : 5);
		if (t->active) {
			int bottom = g->y + POPUP_HEADER_H;                                    // down to the line under the strip, joined to the panel
			render_backdrop_rect(screen, t->box.x, t->box.y, 1, bottom - t->box.y, BD_EDGE);
			render_backdrop_rect(screen, t->box.x + t->box.w - 1, t->box.y, 1, bottom - t->box.y, BD_EDGE);
			render_backdrop_rect(screen, t->box.x + 1, t->box.y, t->box.w - 2, bottom - t->box.y + 1, BD_PANEL);
		}
		// The icon, and the name when there is one, centred in the tab.
		int name_w = t->name ? (int)strlen(t->name) * 8 + 3 : 0;
		int at = t->box.x + (t->box.w - (POPUP_ICON_W + name_w)) / 2;
		tab_icon(screen, at, text_y, t->tab, ink);
		if (t->name) render_backdrop_text(screen, at + POPUP_ICON_W + 3, text_y, t->name, ink);
	}
}

// The frame: shadow, outline, panel, the tab bar, and the key hints. `position` is shown at the right of the
// first hint line (such as "3/20") when there is room for it, or empty.
static PopupGeometry draw_frame(Screen screen, ReadingTab tab, bool with_page, const char *position) {
	PopupGeometry g = popup_geometry(render_is_rotated());
	render_popup_begin(screen, g.x, g.y, g.w + 4, g.h + 4);
	panel_shape(screen, g.x + 3, g.y + 3, g.w, g.h, false, BD_SHADOW);
	panel_shape(screen, g.x, g.y, g.w, g.h, false, BD_EDGE);
	panel_shape(screen, g.x + 1, g.y + 1, g.w - 2, g.h - 2, true, BD_PANEL);
	tab_bar(screen, &g, with_page ? 3 : 2, tab);

	// The hint area: a line, the key buttons with what they do, and the position at the right.
	int area_y = g.y + g.h - g.footer_h;
	render_backdrop_rect(screen, g.x + 1, area_y, g.w - 2, 1, BD_RULE);
	PopupHintBox hints[POPUP_HINTS_MAX];
	int n = popup_hint_layout(&g, tab, hints);
	for (int i = 0; i < n; i++) {
		int keys_w = (int)strlen(hints[i].hint.keys) * 8 + 4;
		key_button(screen, hints[i].box.x, hints[i].box.y, hints[i].hint.keys);
		render_backdrop_text(screen, hints[i].box.x + keys_w + 4, hints[i].box.y + 2, hints[i].hint.label, BD_BTN_DIM);
	}
	if (position[0]) {
		int w = (int)strlen(position) * 8;
		if (popup_position_fits(&g, tab, w)) render_backdrop_text(screen, g.x + g.w - 8 - w, area_y + 8, position, BD_BTN_DIM);
	}
	return g;
}

// A scrollbar at the right of the list, when it is longer than what fits.
static void scrollbar(Screen screen, const PopupGeometry *g, int count, int top, int visible) {
	if (count <= visible) return;
	int x = g->x + g->w - 8, y = g->y + g->list_y, h = visible * POPUP_ROW_H;
	render_backdrop_rect(screen, x, y, 3, h, BD_RULE);
	int thumb = h * visible / count;
	if (thumb < 8) thumb = 8;
	int at = (h - thumb) * top / (count - visible);
	render_backdrop_rect(screen, x, y + at, 3, thumb, BD_ACCENT);
}

// One row of a list: the selected one has a bar and the green text.
static void row(Screen screen, const PopupGeometry *g, int i, bool selected, bool here, int indent, const char *text) {
	int y = g->y + g->list_y + i * POPUP_ROW_H;
	int text_w = g->w - 14 - 14 - indent;
	if (selected) {
		render_backdrop_rect(screen, g->x + 4, y, g->w - 12, POPUP_ROW_H, BD_RULE);
		render_backdrop_rect(screen, g->x + 4, y, 3, POPUP_ROW_H, BD_ACCENT);
	} else if (here) {
		render_backdrop_rect(screen, g->x + 9, y + 6, 4, 4, BD_ACCENT); // where you are in the book
	}
	render_backdrop_text_utf8(screen, g->x + 14 + indent, y + 4, text, selected ? BD_ACCENT : BD_BTN_TEXT, text_w);
}

static void empty_message(Screen screen, const PopupGeometry *g, const char *l1, const char *l2) {
	render_backdrop_text_utf8(screen, g->x + 14, g->y + g->list_y + 12, l1, BD_BTN_TEXT, g->w - 28);
	render_backdrop_text_utf8(screen, g->x + 14, g->y + g->list_y + 26, l2, BD_BTN_DIM, g->w - 28);
}

static void position_text(char *out, size_t cap, int selected, int count) {
	if (count > 0) snprintf(out, cap, "%d/%d", selected + 1, count);
	else out[0] = '\0';
}

void bookmark_menu_draw(const BookmarkMenu *m, const BookMarks *marks, int first_chapter, const int *pages, bool with_page, Screen screen) {
	char pos[24];
	position_text(pos, sizeof pos, m->selected, marks->count);
	PopupGeometry g = draw_frame(screen, TAB_BOOKMARKS, with_page, pos);

	if (marks->count == 0) {
		empty_message(screen, &g, "No bookmarks yet.", "A in a book adds one.");
		return;
	}
	for (int i = 0; i < g.rows; i++) {
		int index = m->top + i;
		if (index >= marks->count) break;
		const Mark *k = &marks->mark[index];
		char line[BOOKMARKS_PATH_MAX];
		const char *text = k->text[0] ? k->text : "(no text)";
		if (pages && pages[index] > 0) {
			snprintf(line, sizeof line, "Page %d %s", pages[index], text); // the page in the book (decision 081)
		} else {
			int chapter = (int)k->chapter - first_chapter + 1; // the pages are not counted yet
			snprintf(line, sizeof line, "Ch %d %s", chapter < 1 ? 1 : chapter, text);
		}
		row(screen, &g, i, index == m->selected, false, 0, line);
	}
	scrollbar(screen, &g, marks->count, m->top, g.rows);
}

void toc_menu_draw(const ListMenu *m, const Toc *toc, int here, int bookmark_count, bool with_page, Screen screen) {
	(void)bookmark_count;
	int count = toc ? toc->count : 0;
	char pos[24];
	position_text(pos, sizeof pos, m->selected, count);
	PopupGeometry g = draw_frame(screen, TAB_CONTENTS, with_page, pos);

	if (count == 0) {
		empty_message(screen, &g, "No contents found", "in this book.");
		return;
	}
	for (int i = 0; i < g.rows; i++) {
		int index = m->top + i;
		if (index >= count) break;
		const TocEntry *e = &toc->entry[index];
		int level = e->level > 2 ? 2 : e->level; // at most two levels deep
		row(screen, &g, i, index == m->selected, index == here, level * 8, e->title);
	}
	scrollbar(screen, &g, count, m->top, g.rows);
}

// A small triangle: `up` points up. Used above and below the digit being changed.
static void triangle(Screen screen, int cx, int y, bool up, BackdropColour colour) {
	for (int i = 0; i < 4; i++) {
		int w = up ? 2 * i + 1 : 2 * (3 - i) + 1;
		render_backdrop_rect(screen, cx - w / 2, y + i, w, 1, colour);
	}
}

void page_menu_draw(const PageMenu *m, int current, int bookmark_count, Screen screen) {
	(void)bookmark_count;
	PopupGeometry g = draw_frame(screen, TAB_PAGE, true, "");

	// The number in large digits with the one being changed in green, a small arrow above and below it
	// (Up and Down change it, and so does a touch there) and a line under it (Left and Right move it), then "/ total".
	int x0, big_y;
	popup_digit_layout(&g, m->digits, &x0, &big_y);
	char total[16];
	snprintf(total, sizeof total, "/ %d", m->total);
	for (int i = 0; i < m->digits; i++) {
		char d[2] = { (char)('0' + page_menu_digit(m, i)), 0 };
		bool cursor = i == m->cursor;
		int dx = x0 + i * POPUP_DIGIT_W;
		render_backdrop_text_scaled(screen, dx, big_y, d, cursor ? BD_ACCENT : BD_BTN_TEXT, 2);
		if (cursor) {
			triangle(screen, dx + 8, big_y - 9, true, BD_ACCENT);
			render_backdrop_rect(screen, dx - 1, big_y + 18, 18, 2, BD_ACCENT);
			triangle(screen, dx + 8, big_y + 23, false, BD_ACCENT);
		}
	}
	render_backdrop_text(screen, x0 + m->digits * POPUP_DIGIT_W + 8, big_y + 4, total, BD_BTN_DIM);

	char now[32];
	snprintf(now, sizeof now, "Now on page %d", current);
	render_backdrop_text_utf8(screen, g.x + 14, g.y + g.list_y + 70, now, BD_BTN_DIM, g.w - 28);
	render_backdrop_text_utf8(screen, g.x + 14, g.y + g.list_y + 84, "Up/Down: change", BD_BTN_TEXT, g.w - 28);
	render_backdrop_text_utf8(screen, g.x + 14, g.y + g.list_y + 96, "Left/Right: move", BD_BTN_TEXT, g.w - 28);
}
