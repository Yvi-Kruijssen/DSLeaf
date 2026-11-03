#include "txtbook.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mbcs.h"
#include "storage.h"
#include "textx.h"

#define CHAPTER_MIN_BYTES 1500  // a heading with less than this after it is not a chapter (a list of chapters)
#define CHAPTER_FIRST_MIN 400   // a heading this close to the start is the start of the first chapter
#define CHAPTER_LAST_MIN 200
#define RUN_GAP 150             // headings closer than this belong together

struct TxtBook {
	char path[256];
	TxtEncoding encoding;
	uint32_t data_start;   // after the byte order mark
	uint32_t file_size;
	bool line_mode;
	uint32_t *start;       // the file offset where each chapter starts, and the end of the file after the last
};

TxtEncoding txt_encoding(const Epub *book) {
	return book->txt ? book->txt->encoding : TXT_UTF8;
}

bool txt_is_path(const char *path) {
	size_t n = strlen(path);
	return n > 4 && strcasecmp(path + n - 4, ".txt") == 0;
}

// ---- Reading characters ----

static const uint16_t cp1252_high[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static const uint16_t cp1250_high[128] = {
	0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021,
	0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

static const uint16_t iso8859_2_high[128] = {
	0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
	0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
	0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
	0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
	0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
	0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
	0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
	0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

static const uint16_t iso8859_15_high[128] = {
	0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
	0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
	0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
	0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x20AC, 0x00A5, 0x0160, 0x00A7,
	0x0161, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x017D, 0x00B5, 0x00B6, 0x00B7,
	0x017E, 0x00B9, 0x00BA, 0x00BB, 0x0152, 0x0153, 0x0178, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

// The character of a byte above 0x7F in one of the 8 bit encodings (decision 088).
static int legacy_in(TxtEncoding e, int b) {
	if (b < 0x80) return b;
	switch (e) {
	case TXT_CP1250: return cp1250_high[b - 0x80];
	case TXT_ISO8859_2: return iso8859_2_high[b - 0x80];
	case TXT_ISO8859_15: return iso8859_15_high[b - 0x80];
	default: return b >= 0x80 && b < 0xA0 ? cp1252_high[b - 0x80] : b;
	}
}

static int legacy(int b) {
	return b >= 0x80 && b < 0xA0 ? cp1252_high[b - 0x80] : b;
}

static void dec_init(TxtDecoder *d, FILE *file, TxtEncoding encoding, uint32_t start, uint32_t end) {
	memset(d, 0, sizeof *d);
	d->file = file;
	d->encoding = encoding;
	d->buf_at = start;
	d->end = end;
	d->peek_cp = -2;
}

// Makes sure there is a byte to read. False at the end of the range.
static bool dec_fill(TxtDecoder *d) {
	if (d->buf_pos < d->buf_len) return true;
	uint32_t at = d->buf_at + (uint32_t)d->buf_len;
	if (at >= d->end) return false;
	uint32_t want = d->end - at;
	if (want > TXT_BUF) want = TXT_BUF;
	if (fseek(d->file, (long)at, SEEK_SET) != 0) return false;
	size_t n = fread(d->buf, 1, want, d->file);
	if (n == 0) return false;
	d->buf_at = at;
	d->buf_len = (int)n;
	d->buf_pos = 0;
	return true;
}

static int dec_byte(TxtDecoder *d) {
	return dec_fill(d) ? d->buf[d->buf_pos++] : -1;
}

static int dec_peek_byte(TxtDecoder *d) {
	return dec_fill(d) ? d->buf[d->buf_pos] : -1;
}

static uint32_t dec_pos(const TxtDecoder *d) {
	return d->buf_at + (uint32_t)d->buf_pos;
}

static int dec_unit16(TxtDecoder *d) {
	int a = dec_byte(d), b = dec_byte(d);
	if (a < 0 || b < 0) return -1;
	return d->encoding == TXT_UTF16LE ? a | (b << 8) : (a << 8) | b;
}

// The next character as a Unicode code point, or -1 at the end of the range.
static int dec_cp_raw(TxtDecoder *d) {
	int b = dec_byte(d);
	if (b < 0) return -1;
	switch (d->encoding) {
	case TXT_LATIN1:
	case TXT_CP1250:
	case TXT_ISO8859_2:
	case TXT_ISO8859_15: return legacy_in(d->encoding, b);
	case TXT_CP1251: return b < 0x80 ? b : cp1251_high[b - 0x80];
	case TXT_GBK:
	case TXT_BIG5:
	case TXT_SJIS:
	case TXT_EUCKR: {
		if (b < 0x80) return b;
		MbKind kind = d->encoding == TXT_GBK ? MB_GBK : d->encoding == TXT_BIG5 ? MB_BIG5 : d->encoding == TXT_SJIS ? MB_SJIS : MB_EUCKR;
		if (kind == MB_SJIS && b >= 0xA1 && b <= 0xDF) return 0xFF61 + (b - 0xA1); // half-width katakana
		if (mb_is_lead(kind, b)) {
			int trail = dec_peek_byte(d);
			uint16_t cp = trail >= 0 ? mb_decode(kind, b, trail) : 0;
			if (cp) {
				dec_byte(d);
				return cp;
			}
		}
		return legacy(b); // not a pair of this encoding: the byte as Windows-1252, like a stray byte in UTF-8
	}
	case TXT_UTF16LE:
	case TXT_UTF16BE: {
		int second = dec_byte(d);
		if (second < 0) return -1;
		int u = d->encoding == TXT_UTF16LE ? b | (second << 8) : (b << 8) | second;
		if (u >= 0xD800 && u < 0xDC00) { // the first half of a pair
			int low = dec_unit16(d);
			if (low >= 0xDC00 && low < 0xE000) return 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
			return 0xFFFD;
		}
		return u >= 0xDC00 && u < 0xE000 ? 0xFFFD : u;
	}
	default: break;
	}
	// UTF-8. A byte that does not fit (a Latin-1 or Windows-1252 text with a stray byte) is read as that character.
	if (b < 0x80) return b;
	int extra = b >= 0xF0 && b <= 0xF4 ? 3 : (b >= 0xE0 ? 2 : (b >= 0xC2 ? 1 : -1));
	if (b > 0xF4 || extra < 0) return legacy(b);
	int cp = b & (extra == 1 ? 0x1F : (extra == 2 ? 0x0F : 0x07));
	for (int i = 0; i < extra; i++) {
		int next = dec_peek_byte(d);
		if (next < 0x80 || next > 0xBF) return legacy(b); // not a continuation: the lead byte was a letter of its own
		dec_byte(d);
		cp = (cp << 6) | (next & 0x3F);
	}
	if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) return 0xFFFD;
	return cp;
}

static int dec_cp(TxtDecoder *d) {
	if (d->peek_cp != -2) {
		int cp = d->peek_cp;
		d->peek_cp = -2;
		return cp;
	}
	return dec_cp_raw(d);
}

static int encode_utf8(int cp, uint8_t out[4]) {
	if (cp < 0x80) {
		out[0] = (uint8_t)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (uint8_t)(0xC0 | (cp >> 6));
		out[1] = (uint8_t)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (uint8_t)(0xE0 | (cp >> 12));
		out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (uint8_t)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (uint8_t)(0xF0 | (cp >> 18));
	out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (uint8_t)(0x80 | (cp & 0x3F));
	return 4;
}

// ---- Lines ----

// Reads one piece of a line into `ln`: leading white space is counted and dropped, tabs and other white space
// become spaces, trailing spaces are dropped. A line longer than TXT_LINE_MAX bytes comes in pieces, cut at a space
// when there is one; the rest is kept in s->carry for the next call.
static void read_line(TxtStream *s, TxtLine *ln) {
	ln->len = 0;
	ln->indent = 0;
	ln->complete = ln->more = ln->eof = false;
	ln->continued = s->carry_len > 0;
	if (s->carry_len > 0) {
		memcpy(ln->text, s->carry, (size_t)s->carry_len);
		ln->len = s->carry_len;
		s->carry_len = 0;
	}
	bool leading = ln->len == 0 && !ln->continued;
	for (;;) {
		int cp = dec_cp(&s->dec);
		if (cp < 0) {
			ln->eof = true;
			break;
		}
		if (cp == '\n') {
			ln->complete = true;
			break;
		}
		if (cp == '\r') {
			int next = dec_cp(&s->dec);
			if (next != '\n' && next >= 0) s->dec.peek_cp = next; // a lone CR is a line break too
			ln->complete = true;
			break;
		}
		if (cp == 0 || cp == 0xFEFF) continue;
		if (cp == '\f' || cp == 0x0B) cp = ' ';
		if (cp == ' ' || cp == '\t') {
			if (leading) {
				ln->indent += cp == '\t' ? 4 : 1;
				continue;
			}
			cp = ' ';
		} else {
			leading = false;
		}
		uint8_t enc[4];
		int n = encode_utf8(cp, enc);
		if (ln->len + n > TXT_LINE_MAX) { // full: cut at the last space, and carry the rest
			int k = -1;
			for (int i = ln->len - 1; i > 0; i--) {
				if (ln->text[i] == ' ') {
					k = i;
					break;
				}
			}
			if (k > 0) {
				s->carry_len = ln->len - k - 1;
				memcpy(s->carry, ln->text + k + 1, (size_t)s->carry_len);
				ln->len = k;
			}
			memcpy(s->carry + s->carry_len, enc, (size_t)n);
			s->carry_len += n;
			ln->more = true;
			break;
		}
		memcpy(ln->text + ln->len, enc, (size_t)n);
		ln->len += n;
	}
	while (ln->len > 0 && ln->text[ln->len - 1] == ' ') ln->len--;
	ln->text[ln->len] = '\0';
}

static bool blank_line(const TxtLine *ln) {
	return ln->len == 0 && !ln->more && !ln->continued;
}

// A short line on its own without a full stop at the end, such as "Down the Rabbit-Hole" or "Who Stole the Tarts?": the title that some
// books put on the line under "CHAPTER I.".
static bool title_like(const TxtLine *ln) {
	if (ln->len < 2 || ln->len > 50 || !ln->complete || ln->more || ln->continued) return false;
	char last = ln->text[ln->len - 1];
	return last != '.' && last != ',' && last != ';' && last != ':' && last != '"';   // a title may end in ? or !
}

// ---- Headings ----

static int word_end(const char *t) {
	int n = 0;
	while (t[n] && t[n] != ' ' && t[n] != '.' && t[n] != ':' && t[n] != ')') n++;
	return n;
}

static bool is_roman(const char *t, int n) {
	if (n < 1 || n > 8) return false;
	for (int i = 0; i < n; i++) {
		if (!strchr("IVXLCDMivxlcdm", t[i])) return false;
	}
	return true;
}

static bool is_number_word(const char *t, int n) {
	static const char *const words[] = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
	                                      "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty",
	                                      "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth" };
	for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
		if ((int)strlen(words[i]) == n && strncasecmp(t, words[i], (size_t)n) == 0) return true;
	}
	return false;
}

static bool is_number(const char *t, int n) {
	if (n < 1 || n > 4) return false;
	for (int i = 0; i < n; i++) {
		if (t[i] < '0' || t[i] > '9') return false;
	}
	return true;
}

// A line such as "CHAPTER I.", "Chapter 12", "Book Two: The Golden Thread", "PART THE FIRST" or "Prologue", or just a
// Roman numeral. Used for headings and for cutting the file into chapters.
static bool looks_like_chapter(const char *t, int len) {
	if (len < 1 || len > 70) return false;
	int first = word_end(t);
	if (first == len && is_roman(t, first)) return true;                          // "XII"
	if (first + 1 == len && is_roman(t, first) && (t[first] == '.' || t[first] == ')')) return true; // "XII."

	// English first, then the same words in a few other languages of Latin letters (decision 090). The lines are UTF-8, and
	// the comparison ignores case for ASCII letters only, so the Polish word is listed in both cases.
	static const char *const alone[] = {
		"prologue", "epilogue", "preface", "introduction", "foreword", "afterword",
		"prolog", "epilog", "pr\xC3\xA9" "face", "vorwort", "pr\xC3\xB3" "logo", "ep\xC3\xADlogo", "prefazione", "voorwoord",
		"wst\xC4\x99p", "WST\xC4\x98P", "przedmowa", "zako\xC5\x84" "czenie", "ZAKO\xC5\x83" "CZENIE",
	};
	static const char *const numbered[] = {
		"chapter", "book", "part", "volume", "appendix", "act",
		"rozdzia\xC5\x82", "ROZDZIA\xC5\x81", "cz\xC4\x99\xC5\x9B\xC4\x87", "CZ\xC4\x98\xC5\x9A\xC4\x86", "tom", "akt", // Polish
		"kapitel", "teil", "buch", "chapitre", "partie", "livre", "cap\xC3\xADtulo", "capitulo", "parte", "libro",
		"capitolo", "hoofdstuk", "deel", "kapitola", "kapitel", "fejezet",
	};
	for (size_t i = 0; i < sizeof alone / sizeof alone[0]; i++) {
		if ((int)strlen(alone[i]) == first && strncasecmp(t, alone[i], (size_t)first) == 0) return len <= 40;
	}
	for (size_t i = 0; i < sizeof numbered / sizeof numbered[0]; i++) {
		if ((int)strlen(numbered[i]) != first || strncasecmp(t, numbered[i], (size_t)first) != 0) continue;
		const char *p = t + first;
		while (*p == ' ' || *p == '.' || *p == ':') p++;
		if (*p == '\0') return false; // "Chapter" on its own is a word of a sentence
		int n = word_end(p);
		if (strncasecmp(p, "the", 3) == 0 && n == 3) { // "PART THE FIRST"
			p += 3;
			while (*p == ' ') p++;
			n = word_end(p);
		}
		return is_number(p, n) || is_roman(p, n) || is_number_word(p, n);
	}
	return false;
}

// A heading in capital letters, such as "THE END" or "A SCANDAL IN BOHEMIA".
static bool looks_like_caps(const char *t, int len) {
	if (len < 3 || len > 60) return false;
	int letters = 0;
	for (int i = 0; i < len; i++) {
		unsigned char c = (unsigned char)t[i];
		if (c >= 'a' && c <= 'z') return false;
		if (c >= 'A' && c <= 'Z') letters++;
	}
	return letters >= 3 && letters * 10 >= len * 6;
}

// ---- Words and breaks ----

static void stream_init(TxtStream *s, FILE *file, TxtEncoding encoding, uint32_t start, uint32_t end, bool line_mode) {
	memset(s, 0, sizeof *s);
	dec_init(&s->dec, file, encoding, start, end);
	s->line_mode = line_mode;
	s->prev_blank = true;
}

// Moves on to the next line piece and decides what it is: a blank (ends a paragraph), the start of a paragraph or a
// heading (a BLOCK is due) or more of the paragraph. Returns false at the end.
static bool next_piece(TxtStream *s) {
	for (;;) {
		if (!s->started) {
			read_line(s, &s->cur);
			read_line(s, &s->next);
			s->started = true;
		} else {
			s->cur = s->next;
			read_line(s, &s->next);
		}
		TxtLine *cur = &s->cur;
		if (cur->eof && cur->len == 0) return false;
		if (blank_line(cur)) {
			s->prev_blank = true;
			s->in_paragraph = false;
			s->prev_indent = 0;
			s->prev_glue = false;
			continue;
		}

		bool starts = !cur->continued;
		bool next_blank = blank_line(&s->next) || (s->next.eof && s->next.len == 0);
		bool context = s->line_mode || (s->prev_blank && next_blank);
		bool heading = starts && !cur->more && context && cur->len <= 70 &&
		               (looks_like_chapter(cur->text, cur->len) || (!s->line_mode && looks_like_caps(cur->text, cur->len)));
		// "CHAPTER I." with its title on the next line, no blank line between: both are the heading.
		bool with_title = !heading && starts && !cur->more && !s->line_mode && s->prev_blank && !s->title_next &&
		                  cur->len <= 70 && looks_like_chapter(cur->text, cur->len) && title_like(&s->next);
		if (with_title) heading = true;
		if (s->title_next) { // this line is the title of the heading before it
			s->title_next = false;
			s->break_due = true;
			s->first_space = false;
			s->in_paragraph = false;
			s->prev_blank = false;
			s->prev_indent = cur->indent;
			s->prev_glue = false;
			s->word = cur->text;
			s->first_in_piece = true;
			return true;
		}
		s->title_next = with_title;
		bool new_para = !s->in_paragraph || s->line_mode || heading || (starts && cur->indent > s->prev_indent + 1);
		if (!starts) new_para = false;
		if (new_para) {
			s->block_due = true;
			s->block_kind = heading ? BLOCK_HEADING : BLOCK_PARAGRAPH;
			s->first_space = false;
		} else {
			s->first_space = !(starts && s->prev_glue);
		}
		s->in_paragraph = !heading;
		s->prev_blank = false;
		if (starts) s->prev_indent = cur->indent;
		// A line that ends in a hyphen after a letter joins the next line's first word without a space.
		s->prev_glue = cur->complete && cur->len >= 2 && cur->text[cur->len - 1] == '-' &&
		               (isalpha((unsigned char)cur->text[cur->len - 2]) || (unsigned char)cur->text[cur->len - 2] >= 0x80);
		s->word = cur->text;
		s->first_in_piece = true;
		return true;
	}
}

bool txt_chapter_next(EpubChapter *chapter, TextItem *item) {
	TxtStream *s = &chapter->txt;
	item->text[0] = '\0';
	item->space_before = false;
	item->block = BLOCK_PARAGRAPH;
	item->level = 0;
	for (;;) {
		if (s->ended) return false;
		if (s->break_due) {
			s->break_due = false;
			item->kind = TEXT_LINEBREAK;
			return true;
		}
		if (s->block_due) {
			s->block_due = false;
			item->kind = TEXT_BLOCK;
			item->block = (BlockKind)s->block_kind;
			item->level = s->block_kind == BLOCK_HEADING ? 1 : 0;
			return true;
		}
		if (s->word) {
			while (*s->word == ' ') s->word++;
			if (*s->word) {
				size_t n = 0;
				while (s->word[n] && s->word[n] != ' ' && n < TEXTX_WORD_MAX) n++;
				if (s->word[n] && s->word[n] != ' ') { // a word cut in the middle: do not cut a character in two
					while (n > 1 && ((unsigned char)s->word[n] & 0xC0) == 0x80) n--;
				}
				memcpy(item->text, s->word, n);
				item->text[n] = '\0';
				item->kind = TEXT_WORD;
				item->space_before = s->first_in_piece ? s->first_space : !s->glue_cut;
				s->first_in_piece = false;
				s->glue_cut = s->word[n] != '\0' && s->word[n] != ' '; // the next piece is part of the same word
				s->word += n;
				return true;
			}
			s->word = NULL;
		}
		if (!next_piece(s)) {
			s->ended = true;
			item->kind = TEXT_END;
			return true;
		}
	}
}

// ---- Opening a book ----

typedef struct {
	uint32_t offset;
	bool context;       // it stands on its own between blank lines
} Candidate;

static Candidate candidates[1024];
static TxtStream scan_stream; // 3 KB, kept off the stack

// Is the sample UTF-8? Counts the sequences that are valid UTF-8 and the bytes that are not. A file with no bad byte is
// UTF-8, and so is one that has at least as many good sequences as bad bytes (UTF-8 with a stray byte, which is then
// read as Windows-1252 on its own). Latin-1 text almost never contains a valid multi-byte sequence by chance.
static bool utf8_likely(const uint8_t *p, size_t n) {
	size_t good = 0, bad = 0;
	for (size_t i = 0; i < n;) {
		uint8_t b = p[i];
		if (b < 0x80) {
			i++;
			continue;
		}
		int extra = b >= 0xC2 && b <= 0xDF ? 1 : (b >= 0xE0 && b <= 0xEF ? 2 : (b >= 0xF0 && b <= 0xF4 ? 3 : -1));
		if (extra < 0) {
			bad++;
			i++;
			continue;
		}
		if (i + (size_t)extra >= n) break; // the sample ends inside a character
		bool ok = true;
		for (int k = 1; k <= extra; k++) {
			if ((p[i + (size_t)k] & 0xC0) != 0x80) ok = false;
		}
		if (ok) {
			good++;
			i += (size_t)extra + 1;
		} else {
			bad++;
			i++;
		}
	}
	return bad == 0 || good >= bad;
}

static bool cp_is_letter(int cp) {
	if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
	if (cp >= 0xC0 && cp <= 0x24F) return cp != 0xD7 && cp != 0xF7;
	return cp >= 0x370 && cp <= 0x4FF; // Greek and Cyrillic
}

// Which 8 bit encoding is a text that is not UTF-8? Each candidate is scored on the accented bytes that stand right
// after a letter (the inside or end of a word): a byte that is a letter in the encoding counts for it and one that
// is a symbol or a control counts against it. So Polish "bylo" with the l-stroke at 0xB3 is Windows-1250 (it would be a
// superscript three in Windows-1252), and French "coeur" with 0xBD is ISO-8859-15. Ties go to Windows-1252, the most
// common (decision 088). Windows-1250 and ISO-8859-2 are told apart by the bytes 0x80 to 0x9F, which only the first uses.
static TxtEncoding detect_legacy(const uint8_t *p, size_t n) {
	static const TxtEncoding candidates[4] = { TXT_LATIN1, TXT_CP1250, TXT_ISO8859_2, TXT_ISO8859_15 };
	int score[4] = { 0, 0, 0, 0 };
	bool has_c1 = false;
	int euro = 0;
	for (size_t i = 0; i < n; i++) {
		if (p[i] >= 0x80 && p[i] < 0xA0) has_c1 = true;
		if (p[i] == 0xA4) euro++;
	}
	for (size_t i = 1; i < n; i++) {
		if (p[i] < 0x80 || !cp_is_letter(p[i - 1])) continue;
		for (int c = 0; c < 4; c++) score[c] += cp_is_letter(legacy_in(candidates[c], p[i])) ? 1 : -3;
	}
	if (euro > 0 && !has_c1) score[3] += euro > 5 ? 10 : 2 * euro; // a euro sign, in a text with no Windows quotes
	int best = 0;
	for (int c = 1; c < 4; c++) {
		if (score[c] > score[best]) best = c;
	}
	return score[best] >= 3 && score[best] > score[0] ? candidates[best] : TXT_LATIN1;
}

// Is the sample English? A text that is English has about a quarter of its words among a few very common ones (18 per cent is
// asked for) and the word "the" among them (decision 093). Used to decide about hyphenation, which uses English patterns.
static bool looks_english(const uint8_t *p, size_t n) {
	static const char *const common[] = { "the", "and", "of", "to", "a", "in", "that", "is", "was", "he", "she", "it", "for", "with", "as", "his", "her", "had", "you", "not" };
	int words = 0, hits = 0, the = 0;
	for (size_t i = 0; i < n;) {
		if (!isalpha(p[i])) {
			i++;
			continue;
		}
		char w[8];
		size_t len = 0;
		while (i < n && (isalpha(p[i]) || p[i] >= 0x80)) {
			if (len < sizeof w - 1) w[len] = (char)tolower(p[i]);
			len++;
			i++;
		}
		words++;
		if (len >= sizeof w) continue;
		w[len] = '\0';
		if (strcmp(w, "the") == 0) the++;
		for (size_t k = 0; k < sizeof common / sizeof common[0]; k++) {
			if (strcmp(w, common[k]) == 0) {
				hits++;
				break;
			}
		}
	}
	return words >= 80 && hits * 100 >= words * 18 && the * 100 >= words * 2; // "the" is in no other language of Latin letters
}

// ---- The old double-byte encodings (decision 094) ----
typedef struct {
	int pairs, bad;     // valid pairs of two bytes, and high bytes that are not part of one
	int kana, hangul;   // decoded characters that are kana or Hangul syllables
	int halfkana;       // single bytes 0xA1 to 0xDF read as half-width katakana (Shift-JIS)
	int trail_low;      // pairs whose second byte is below 0x80
	int both_high;      // pairs whose two bytes are both 0xA1 or more (EUC-KR proper, not the other cp949 syllables)
	int level1;         // pairs of the most used characters of GB2312 (leads 0xB0 to 0xD7, trails 0xA1 or more)
	int big5_common;    // pairs whose lead is in the most used part of Big5 (0xA4 to 0xC6)
} MbScore;

static MbScore score_mb(MbKind kind, const uint8_t *p, size_t n) {
	MbScore s;
	memset(&s, 0, sizeof s);
	for (size_t i = 0; i < n;) {
		int b = p[i];
		if (b < 0x80) {
			i++;
			continue;
		}
		if (kind == MB_SJIS && b >= 0xA1 && b <= 0xDF) {
			s.halfkana++;
			i++;
			continue;
		}
		if (mb_is_lead(kind, b) && i + 1 < n) {
			int t = p[i + 1];
			uint16_t cp = mb_decode(kind, b, t);
			if (cp) {
				s.pairs++;
				if (cp >= 0x3040 && cp <= 0x30FF) s.kana++;
				if (cp >= 0xAC00 && cp <= 0xD7A3) s.hangul++;
				if (t < 0x80) s.trail_low++;
				if (b >= 0xA1 && t >= 0xA1) s.both_high++;
				if (b >= 0xB0 && b <= 0xD7 && t >= 0xA1) s.level1++;
				if (b >= 0xA4 && b <= 0xC6) s.big5_common++;
				i += 2;
				continue;
			}
		}
		s.bad++;
		i++;
	}
	return s;
}

// Is the text Windows-1251, Korean, Japanese or Chinese? A text of these has most of its bytes above 0x7F (a Western text
// has a few accents). Cyrillic in Windows-1251 is nearly all bytes 0xC0 to 0xFF; the others are told apart by what the pairs
// of bytes decode to: Korean is all Hangul syllables (EUC-KR rows 0xB0 to 0xC8), Japanese has kana, GBK mostly the common
// characters of GB2312 with trail bytes above 0x7F, Big5 mostly the common characters of Big5 with many trail bytes below
// 0x80. Returns false if it is none of them (then the 8 bit guess goes on).
static bool detect_wide(const uint8_t *p, size_t n, TxtEncoding *out) {
	size_t high = 0, cyr = 0;
	for (size_t i = 0; i < n; i++) {
		if (p[i] < 0x80) continue;
		high++;
		if (p[i] >= 0xC0 || p[i] == 0xA8 || p[i] == 0xB8) cyr++;
	}
	if (high < 40 || high * 4 < n) return false; // not enough: a Western text, or too little to judge
	if (cyr * 100 >= high * 90) {
		*out = TXT_CP1251;
		return true;
	}
	MbScore ko = score_mb(MB_EUCKR, p, n), ja = score_mb(MB_SJIS, p, n), gb = score_mb(MB_GBK, p, n), b5 = score_mb(MB_BIG5, p, n);
	if (ko.pairs >= 10 && ko.bad * 10 <= ko.pairs && ko.hangul * 100 >= ko.pairs * 90 && ko.both_high * 100 >= ko.pairs * 90) {
		*out = TXT_EUCKR;
		return true;
	}
	if (ja.pairs >= 10 && ja.bad * 10 <= ja.pairs && ja.kana * 100 >= ja.pairs * 8 && ja.halfkana * 10 <= ja.pairs) {
		*out = TXT_SJIS;
		return true;
	}
	if (gb.pairs >= 10 && gb.bad * 10 <= gb.pairs && gb.level1 * 100 >= gb.pairs * 50 && gb.trail_low * 100 <= gb.pairs * 12) {
		*out = TXT_GBK;
		return true;
	}
	if (b5.pairs >= 10 && b5.bad * 10 <= b5.pairs && b5.big5_common * 100 >= b5.pairs * 50 && b5.trail_low * 100 >= b5.pairs * 15) {
		*out = TXT_BIG5;
		return true;
	}
	return false;
}

EpubResult txt_open(Epub *book, const char *path) {
	memset(book, 0, sizeof *book);
	FILE *f = storage_fopen(path, "rb");
	if (!f) return EPUB_ERR_READ;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return EPUB_ERR_READ;
	}
	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return size == 0 ? EPUB_ERR_NO_CHAPTERS : EPUB_ERR_READ;
	}
	rewind(f);

	// The encoding, from the byte order mark or the first bytes.
	static uint8_t head[4096];
	size_t got = fread(head, 1, sizeof head, f);
	TxtEncoding enc = TXT_UTF8;
	uint32_t data_start = 0;
	if (got >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
		data_start = 3;
	} else if (got >= 2 && head[0] == 0xFF && head[1] == 0xFE) {
		enc = TXT_UTF16LE;
		data_start = 2;
	} else if (got >= 2 && head[0] == 0xFE && head[1] == 0xFF) {
		enc = TXT_UTF16BE;
		data_start = 2;
	} else {
		size_t control = 0;
		for (size_t i = 0; i < got; i++) {
			if (head[i] == 0) { // a binary file, not text
				fclose(f);
				return EPUB_ERR_NO_CHAPTERS;
			}
			if (head[i] < 32 && head[i] != '\n' && head[i] != '\r' && head[i] != '\t' && head[i] != '\f' && head[i] != 0x1B) control++;
		}
		if (control * 20 > got) { // more than one byte in twenty is a control character: not text
			fclose(f);
			return EPUB_ERR_NO_CHAPTERS;
		}
		if (!utf8_likely(head, got) && !detect_wide(head, got, &enc)) enc = detect_legacy(head, got);
		else if (!utf8_likely(head, got)) { /* enc was set by detect_wide */ }
	}

	bool english = enc != TXT_UTF16LE && enc != TXT_UTF16BE && looks_english(head, got);
	struct TxtBook *t = calloc(1, sizeof *t);
	if (!t) {
		fclose(f);
		return EPUB_ERR_MEMORY;
	}
	snprintf(t->path, sizeof t->path, "%s", path);
	t->encoding = enc;
	t->data_start = data_start;
	t->file_size = (uint32_t)size;

	// One pass over the file: is there any text, are there blank lines, and where are the chapter headings.
	stream_init(&scan_stream, f, enc, data_start, t->file_size, false);
	TxtStream *s = &scan_stream;
	int cand_count = 0;
	long blank_lines = 0, text_lines = 0, indented_lines = 0;
	bool prev_blank = true, pending = false;
	uint32_t pending_off = 0;
	bool pending_context = false;
	uint32_t data_begin = data_start, data_end = 0;
	bool marker_start = false;
	for (;;) {
		uint32_t at = dec_pos(&s->dec);
		if (s->carry_len > 0) at = 0; // a piece of a long line: never a heading
		read_line(s, &s->cur);
		TxtLine *ln = &s->cur;
		if (ln->eof && ln->len == 0) break;
		bool blank = blank_line(ln);
		// Project Gutenberg wraps the book in a licence: the book is what lies between these two lines.
		if (!ln->continued && ln->len > 12 && ln->text[0] == '*' && ln->text[1] == '*' && ln->text[2] == '*') {
			if (strncasecmp(ln->text + 3 + (ln->text[3] == ' '), "START OF", 8) == 0) {
				data_begin = dec_pos(&s->dec);
				if (s->next.len > 0 || s->carry_len > 0) { /* the lookahead is not used while scanning, the position is right */ }
				cand_count = 0;
				pending = false;
				prev_blank = true;
				text_lines = indented_lines = blank_lines = 0;
				marker_start = true;
				continue;
			}
			if (strncasecmp(ln->text + 3 + (ln->text[3] == ' '), "END OF", 6) == 0 && marker_start) {
				data_end = at;
				break;
			}
		}
		if (pending && !ln->continued) { // the line after a heading candidate
			if (cand_count < (int)(sizeof candidates / sizeof candidates[0])) {
				candidates[cand_count].offset = pending_off;
				candidates[cand_count].context = pending_context && (blank || title_like(ln)); // the title may be on the next line
				cand_count++;
			}
			pending = false;
		}
		if (blank) {
			blank_lines++;
			prev_blank = true;
			continue;
		}
		if (!ln->continued) {
			text_lines++;
			if (ln->indent >= 2) indented_lines++;
		}
		if (!ln->continued && !ln->more && looks_like_chapter(ln->text, ln->len)) {
			pending = true;
			pending_off = at;
			pending_context = prev_blank;
		}
		prev_blank = false;
	}
	if (pending && cand_count < (int)(sizeof candidates / sizeof candidates[0])) {
		candidates[cand_count].offset = pending_off;
		candidates[cand_count].context = pending_context;
		cand_count++;
	}
	fclose(f);
	t->data_start = data_begin;
	if (data_end > data_begin) t->file_size = data_end; // the end of the book, before the licence at the end
	if (text_lines == 0) {
		free(t);
		return EPUB_ERR_NO_CHAPTERS;
	}
	// A file with no blank lines has a paragraph on every line, unless a few of the lines (3 to 60 per cent) are indented:
	// then the indented ones start the paragraphs and the others continue them.
	bool indent_style = blank_lines == 0 && indented_lines * 100 >= text_lines * 3 && indented_lines * 100 <= text_lines * 60;
	t->line_mode = blank_lines == 0 && !indent_style;

	// The chapters: the headings that stand on their own (or all of them in a file without blank lines), when
	// enough text follows. The text before the first one is the first chapter.
	t->start = malloc(sizeof *t->start * (TXT_CHAPTERS_MAX + 2));
	book->chapter_size = malloc(sizeof *book->chapter_size * (TXT_CHAPTERS_MAX + 1));
	book->chapter = calloc((size_t)TXT_CHAPTERS_MAX + 1, sizeof *book->chapter);
	if (!t->start || !book->chapter_size || !book->chapter) {
		free(t->start);
		free(book->chapter_size);
		free(book->chapter);
		book->chapter_size = NULL;
		book->chapter = NULL;
		free(t);
		return EPUB_ERR_MEMORY;
	}
	// Candidates that stand together (less than RUN_GAP bytes apart) are a run. A run of three or more is a list of
	// chapters (a contents list), which is left out. Of a run of two (such as "BOOK ONE" and "I") the first counts.
	// Then a heading only counts when enough text follows it before the next one.
	int usable = 0;
	for (int i = 0; i < cand_count; i++) {
		if (t->line_mode || candidates[i].context) candidates[usable++] = candidates[i];
	}
	static Candidate chosen[1024];
	int chosen_count = 0;
	for (int i = 0; i < usable;) {
		int j = i + 1;
		while (j < usable && candidates[j].offset - candidates[j - 1].offset < RUN_GAP) j++;
		if (j - i <= 2) chosen[chosen_count++] = candidates[i];
		i = j;
	}
	int count = 1;
	t->start[0] = data_begin;
	for (int i = 0; i < chosen_count && count < TXT_CHAPTERS_MAX; i++) {
		uint32_t off = chosen[i].offset;
		uint32_t next = i + 1 < chosen_count ? chosen[i + 1].offset : t->file_size;
		uint32_t need = i + 1 < chosen_count ? CHAPTER_MIN_BYTES : CHAPTER_LAST_MIN;
		if (next - off < need) continue;                                   // too little text after it: not a chapter
		if (off - t->start[count - 1] < (count == 1 ? CHAPTER_FIRST_MIN : 1)) continue;   // right at the start of the file
		t->start[count++] = off;
	}
	t->start[count] = t->file_size;
	for (int i = 0; i < count; i++) book->chapter_size[i] = t->start[i + 1] - t->start[i];
	book->chapter_count = count;
	if (english) snprintf(book->lang, sizeof book->lang, "en");
	book->txt = t;

	// The title is the file name without the ending.
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	size_t len = strlen(name);
	if (len > 4) len -= 4;
	if (len >= sizeof book->title) len = sizeof book->title - 1;
	memcpy(book->title, name, len);
	book->title[len] = '\0';
	return EPUB_OK;
}

void txt_close(Epub *book) {
	if (!book->txt) return;
	free(book->txt->start);
	free(book->txt);
	free(book->chapter);
	free(book->chapter_size);
	book->txt = NULL;
	book->chapter = NULL;
	book->chapter_size = NULL;
}

bool txt_chapter_open(Epub *book, int index, EpubChapter *chapter) {
	memset(chapter, 0, sizeof *chapter);
	struct TxtBook *t = book->txt;
	if (!t || index < 0 || index >= book->chapter_count) return false;
	FILE *f = storage_fopen(t->path, "rb");
	if (!f) return false;
	stream_init(&chapter->txt, f, t->encoding, t->start[index], t->start[index + 1], t->line_mode);
	return true;
}

void txt_chapter_close(EpubChapter *chapter) {
	if (chapter->txt.dec.file) fclose(chapter->txt.dec.file);
	chapter->txt.dec.file = NULL;
}
