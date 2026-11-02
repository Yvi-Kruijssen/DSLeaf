#include "about.h"

#include <string.h>

#include "input.h"

static void add_line(About *a, int para, int from, int len, bool heading) {
	if (a->count >= ABOUT_LINES_MAX) return;
	a->para[a->count] = (uint16_t)para;
	a->from[a->count] = (uint16_t)from;
	a->len[a->count] = (uint8_t)len;
	a->heading[a->count] = heading;
	a->count++;
}

void about_open(About *a) {
	memset(a, 0, sizeof *a);
	for (int p = 0; p < about_paragraph_count; p++) {
		const char *t = about_paragraphs[p];
		bool heading = t[0] == '#';
		int from = heading ? 1 : 0, n = (int)strlen(t);
		if (n == from) { // blank line
			add_line(a, p, from, 0, false);
			continue;
		}
		while (from < n) {
			int end = from + ABOUT_COLS;
			if (end >= n) {
				end = n;
			} else {
				int cut = end; // break at the last space that fits
				while (cut > from && t[cut] != ' ') cut--;
				end = cut > from ? cut : end; // a word longer than the line is cut
			}
			add_line(a, p, from, end - from, heading);
			from = end;
			while (from < n && t[from] == ' ') from++;
		}
	}
	a->redraw = true;
}

void about_line(const About *a, int line, char *out, bool *heading) {
	if (line < 0 || line >= a->count) {
		out[0] = '\0';
		*heading = false;
		return;
	}
	memcpy(out, about_paragraphs[a->para[line]] + a->from[line], a->len[line]);
	out[a->len[line]] = '\0';
	*heading = a->heading[line];
}

static int max_top(const About *a) {
	return a->count > ABOUT_ROWS ? a->count - ABOUT_ROWS : 0;
}

static void scroll(About *a, int delta) {
	int top = a->top + delta;
	if (top < 0) top = 0;
	if (top > max_top(a)) top = max_top(a);
	if (top != a->top) {
		a->top = top;
		a->redraw = true;
	}
}

bool about_update(About *a, uint32_t actions) {
	if (actions & ACT_BACK) return true;
	if (actions & ACT_UP) scroll(a, -1);
	else if (actions & ACT_DOWN) scroll(a, 1);
	else if (actions & (ACT_LEFT | ACT_TAB_PREV)) scroll(a, -(ABOUT_ROWS - 1));
	else if (actions & (ACT_RIGHT | ACT_TAB_NEXT)) scroll(a, ABOUT_ROWS - 1);
	return false;
}

int about_pages(const About *a) {
	int step = ABOUT_ROWS - 1;
	return max_top(a) == 0 ? 1 : 1 + (max_top(a) + step - 1) / step;
}

int about_page(const About *a) {
	int step = ABOUT_ROWS - 1;
	int page = 1 + (a->top + step - 1) / step;
	if (a->top >= max_top(a)) page = about_pages(a);
	return page;
}

uint32_t about_touch(int x, int y) {
	(void)x;
	if (y >= 160) return ACT_BACK;
	if (y < 15 || y >= 40 + ABOUT_ROWS * 8) return ACT_NONE;
	return y < 40 + ABOUT_ROWS * 4 ? ACT_LEFT : ACT_RIGHT;
}
