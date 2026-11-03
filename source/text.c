#include "text.h"

#include "cjk.h"
#include "font_ext.h"
#include "translit.h"

static unsigned cjk_missed; // wide characters turned into ? because the font cannot draw them (decision 105)

unsigned text_cjk_missed(void) { return cjk_missed; }
void text_cjk_missed_reset(void) { cjk_missed = 0; }

static int one(Glyph out[3], Glyph a) {
	out[0] = a;
	return 1;
}

// Letters the 256-glyph fonts do not have (Latin Extended-A and B: Polish, Czech, Hungarian, Turkish, Romanian ...)
// are shown as the plain letter they are built on (decision 088). Two letters for the ligatures (OE, IJ).
static const uint8_t fold_ext[0x150][2] = {
	{ 'A', 0 }, { 'a', 0 }, { 'A', 0 }, { 'a', 0 }, { 'A', 0 }, { 'a', 0 },
	{ 'C', 0 }, { 'c', 0 }, { 'C', 0 }, { 'c', 0 }, { 'C', 0 }, { 'c', 0 },
	{ 'C', 0 }, { 'c', 0 }, { 'D', 0 }, { 'd', 0 }, { 'D', 0 }, { 'd', 0 },
	{ 'E', 0 }, { 'e', 0 }, { 'E', 0 }, { 'e', 0 }, { 'E', 0 }, { 'e', 0 },
	{ 'E', 0 }, { 'e', 0 }, { 'E', 0 }, { 'e', 0 }, { 'G', 0 }, { 'g', 0 },
	{ 'G', 0 }, { 'g', 0 }, { 'G', 0 }, { 'g', 0 }, { 'G', 0 }, { 'g', 0 },
	{ 'H', 0 }, { 'h', 0 }, { 'H', 0 }, { 'h', 0 }, { 'I', 0 }, { 'i', 0 },
	{ 'I', 0 }, { 'i', 0 }, { 'I', 0 }, { 'i', 0 }, { 'I', 0 }, { 'i', 0 },
	{ 'I', 0 }, { 'i', 0 }, { 'I', 'J' }, { 'i', 'j' }, { 'J', 0 }, { 'j', 0 },
	{ 'K', 0 }, { 'k', 0 }, { 'k', 0 }, { 'L', 0 }, { 'l', 0 }, { 'L', 0 },
	{ 'l', 0 }, { 'L', 0 }, { 'l', 0 }, { 'L', 0 }, { 'l', 0 }, { 'L', 0 },
	{ 'l', 0 }, { 'N', 0 }, { 'n', 0 }, { 'N', 0 }, { 'n', 0 }, { 'N', 0 },
	{ 'n', 0 }, { '\'', 'n' }, { 'N', 0 }, { 'n', 0 }, { 'O', 0 }, { 'o', 0 },
	{ 'O', 0 }, { 'o', 0 }, { 'O', 0 }, { 'o', 0 }, { 'O', 'E' }, { 'o', 'e' },
	{ 'R', 0 }, { 'r', 0 }, { 'R', 0 }, { 'r', 0 }, { 'R', 0 }, { 'r', 0 },
	{ 'S', 0 }, { 's', 0 }, { 'S', 0 }, { 's', 0 }, { 'S', 0 }, { 's', 0 },
	{ 'S', 0 }, { 's', 0 }, { 'T', 0 }, { 't', 0 }, { 'T', 0 }, { 't', 0 },
	{ 'T', 0 }, { 't', 0 }, { 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 },
	{ 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 },
	{ 'U', 0 }, { 'u', 0 }, { 'W', 0 }, { 'w', 0 }, { 'Y', 0 }, { 'y', 0 },
	{ 'Y', 0 }, { 'Z', 0 }, { 'z', 0 }, { 'Z', 0 }, { 'z', 0 }, { 'Z', 0 },
	{ 'z', 0 }, { 's', 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 'E', 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 'O', 0 }, { 'o', 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 'U', 0 }, { 'u', 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 'Z', 0 }, { 'z', 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 'D', 'Z' }, { 'D', 'z' },
	{ 'd', 'z' }, { 'L', 'J' }, { 'L', 'j' }, { 'l', 'j' }, { 'N', 'J' }, { 'N', 'j' },
	{ 'n', 'j' }, { 'A', 0 }, { 'a', 0 }, { 'I', 0 }, { 'i', 0 }, { 'O', 0 },
	{ 'o', 0 }, { 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 }, { 'U', 0 },
	{ 'u', 0 }, { 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 }, { 'e', 0 },
	{ 'A', 0 }, { 'a', 0 }, { 'A', 0 }, { 'a', 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 'G', 0 }, { 'g', 0 }, { 'K', 0 }, { 'k', 0 },
	{ 'O', 0 }, { 'o', 0 }, { 'O', 0 }, { 'o', 0 }, { 0, 0 }, { 0, 0 },
	{ 'j', 0 }, { 'D', 'Z' }, { 'D', 'z' }, { 'd', 'z' }, { 'G', 0 }, { 'g', 0 },
	{ 0, 0 }, { 0, 0 }, { 'N', 0 }, { 'n', 0 }, { 'A', 0 }, { 'a', 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 'A', 0 }, { 'a', 0 },
	{ 'A', 0 }, { 'a', 0 }, { 'E', 0 }, { 'e', 0 }, { 'E', 0 }, { 'e', 0 },
	{ 'I', 0 }, { 'i', 0 }, { 'I', 0 }, { 'i', 0 }, { 'O', 0 }, { 'o', 0 },
	{ 'O', 0 }, { 'o', 0 }, { 'R', 0 }, { 'r', 0 }, { 'R', 0 }, { 'r', 0 },
	{ 'U', 0 }, { 'u', 0 }, { 'U', 0 }, { 'u', 0 }, { 'S', 0 }, { 's', 0 },
	{ 'T', 0 }, { 't', 0 }, { 0, 0 }, { 0, 0 }, { 'H', 0 }, { 'h', 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 'A', 0 }, { 'a', 0 }, { 'E', 0 }, { 'e', 0 }, { 'O', 0 }, { 'o', 0 },
	{ 'O', 0 }, { 'o', 0 }, { 'O', 0 }, { 'o', 0 }, { 'O', 0 }, { 'o', 0 },
	{ 'Y', 0 }, { 'y', 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
};

static int map_codepoint(uint32_t cp, Glyph out[3], int scripts) {
	switch (cp) {
	case 0x00A0: return one(out, ' ');  // no-break space
	case 0x2018:
	case 0x2019: return one(out, '\''); // curly single quotes
	case 0x201C:
	case 0x201D: return one(out, '"');  // curly double quotes
	case 0x2010:                        // hyphen, non-breaking hyphen
	case 0x2011:
	case 0x2212:
	case 0x2013: return one(out, '-');  // en dash, minus
	case 0x201A: return one(out, ',');  // low quotes
	case 0x201E: return one(out, '"');
	case 0x201B:
	case 0x2032: return one(out, '\'');  // prime
	case 0x2033: return one(out, '"');
	case 0x2039: return one(out, '<');  // single angle quotes
	case 0x203A: return one(out, '>');
	case 0x2022: return one(out, '*');  // bullet
	case 0x2122:                        // trade mark
		out[0] = 'T';
		out[1] = 'M';
		return 2;
	case 0x20AC:                        // euro
		out[0] = 'E';
		out[1] = 'U';
		out[2] = 'R';
		return 3;
	case 0x2014:                        // em dash
		out[0] = out[1] = '-';
		return 2;
	case 0x2026:                        // ellipsis
		out[0] = out[1] = out[2] = '.';
		return 3;
	}

	if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return one(out, ' ');
	if (cp <= 0xFF) return one(out, (Glyph)cp);
	if ((scripts & SCRIPT_CJK) && cjk_is_wide(cp) && cjk_has(cp) && cp != 0x3000) { // a wide character: its code, and a fill cell
		out[0] = (Glyph)cp;
		out[1] = GLYPH_FILL;
		return 2;
	}
	if (cp != 0x3000 && !(scripts & SCRIPT_CJK) && cjk_is_wide(cp)) cjk_missed++; // shown as ?, see text_cjk_missed
	if (cp == 0x3000) return one(out, ' ');                          // ideographic space
	if (cp >= 0xFF01 && cp <= 0xFF5E) return one(out, (Glyph)(cp - 0xFEE0)); // fullwidth ASCII (decision 091)
	if (cp >= 0x370 && cp <= 0x4FF) { // Greek and Cyrillic: the font's own letters if it has them, otherwise Latin letters
		if ((scripts & SCRIPT_ALPHABETS) && font_ext_has(cp)) return one(out, (Glyph)cp);
		uint8_t t[3];
		int n = translit_letter(cp, t);
		if (n > 0) {
			for (int i = 0; i < n; i++) out[i] = t[i];
			return n;
		}
	}
	if (cp >= 0x100 && cp < 0x250 && fold_ext[cp - 0x100][0]) {
		out[0] = fold_ext[cp - 0x100][0];
		if (!fold_ext[cp - 0x100][1]) return 1;
		out[1] = fold_ext[cp - 0x100][1];
		return 2;
	}
	return one(out, '?');
}

int text_utf8_next_ext(const char **s, Glyph out[3], int scripts) {
	const uint8_t *p = (const uint8_t *)*s;
	uint32_t cp;
	int extra;

	if (p[0] == 0) return one(out, ' ');

	if (p[0] < 0x80) {
		cp = p[0];
		extra = 0;
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		extra = 1;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		extra = 2;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s += 1; // stray continuation byte or invalid lead byte
		return one(out, '?');
	}

	int used = 1;
	for (int i = 1; i <= extra; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			// Truncated sequence: report one '?' and resume at the bad byte.
			*s += used;
			return one(out, '?');
		}
		cp = (cp << 6) | (p[i] & 0x3F);
		used++;
	}

	*s += used;
	return map_codepoint(cp, out, scripts);
}

int text_utf8_next(const char **s, uint8_t out[3]) {
	Glyph g[3];
	int n = text_utf8_next_ext(s, g, 0);
	for (int i = 0; i < n; i++) out[i] = g[i] > 0xFF ? '?' : (uint8_t)g[i];
	return n;
}
