#include "txtbook.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

bool txt_is_path(const char *path) {
	size_t n = strlen(path);
	return n > 4 && strcasecmp(path + n - 4, ".txt") == 0;
}

// ---- Reading characters ----

static const uint16_t cp1252_high[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

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
	case TXT_LATIN1: return legacy(b);
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

	static const char *const alone[] = { "prologue", "epilogue", "preface", "introduction", "foreword", "afterword" };
	static const char *const numbered[] = { "chapter", "book", "part", "volume", "appendix", "act" };
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
		if (!utf8_likely(head, got)) enc = TXT_LATIN1;
	}

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
