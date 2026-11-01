#include "textx.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const void_names[] = {
	"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param",
	"source", "track", "wbr", "command", "keygen",
};

// Contents are left out, and the element is a break in the text.
static const char *const skip_names[] = { "head", "style", "script", "svg", "math", "noscript", "title", "template" };

static const char *const block_names[] = {
	"p", "div", "ul", "ol", "dl", "dt", "dd", "table", "tr", "section", "article", "aside",
	"header", "footer", "nav", "main", "figure", "figcaption", "address", "pre", "body",
	"center", "caption", "fieldset", "form", "details", "summary", "tbody", "thead", "tfoot",
};

static bool in_list(const char *const *list, size_t count, const char *name) {
	for (size_t i = 0; i < count; i++) {
		if (strcasecmp(list[i], name) == 0) return true;
	}
	return false;
}
#define IN_LIST(list, name) in_list(list, sizeof list / sizeof list[0], name)

static bool contains_word(const char *list, const char *word) {
	size_t n = strlen(word);
	for (const char *p = list; *p;) {
		while (*p == ' ') p++;
		if (strncasecmp(p, word, n) == 0 && (p[n] == ' ' || p[n] == '\0')) return true;
		while (*p && *p != ' ') p++;
	}
	return false;
}

void textx_init(TextExtractor *tx, XmlReadFn read, void *user) {
	memset(tx, 0, sizeof *tx);
	xml_init(&tx->xml, read, user);
	tx->need_block = true;
}

// Describes what kind of block a tag name starts, if any. Returns false for inline tags.
static bool block_kind(const char *name, BlockKind *kind, int *level) {
	*level = 0;
	if ((name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') {
		*kind = BLOCK_HEADING;
		*level = name[1] - '0';
		return true;
	}
	if (strcasecmp(name, "li") == 0) {
		*kind = BLOCK_LIST_ITEM;
		return true;
	}
	if (strcasecmp(name, "blockquote") == 0) {
		*kind = BLOCK_QUOTE;
		return true;
	}
	if (IN_LIST(block_names, name)) {
		*kind = BLOCK_PARAGRAPH;
		return true;
	}
	return false;
}

static void push_block(TextExtractor *tx, const char *name, BlockKind kind, int level) {
	if (tx->stack_depth >= TEXTX_STACK) return;
	TextBlock *b = &tx->stack[tx->stack_depth++];
	size_t n = strlen(name); // block names are all short, so this never cuts one
	if (n >= sizeof b->name) n = sizeof b->name - 1;
	memcpy(b->name, name, n);
	b->name[n] = '\0';
	b->kind = kind;
	b->level = level;
}

// Closes the block with this name. A missing closing tag for something inside
// it is forgiven. A closing tag with no matching opening one is ignored.
static void pop_block(TextExtractor *tx, const char *name) {
	for (int i = tx->stack_depth - 1; i >= 0; i--) {
		if (strncasecmp(tx->stack[i].name, name, sizeof tx->stack[i].name - 1) == 0) {
			tx->stack_depth = i;
			return;
		}
	}
}

static void current_block(const TextExtractor *tx, BlockKind *kind, int *level) {
	// Walk down to the nearest heading, list item or quote, else a plain paragraph.
	*kind = BLOCK_PARAGRAPH;
	*level = 0;
	for (int i = tx->stack_depth - 1; i >= 0; i--) {
		if (tx->stack[i].kind != BLOCK_PARAGRAPH) {
			*kind = tx->stack[i].kind;
			*level = tx->stack[i].level;
			return;
		}
	}
}

static void put_word(TextExtractor *tx, TextItem *item) {
	item->kind = TEXT_WORD;
	memcpy(item->text, tx->word, (size_t)tx->word_len);
	item->text[tx->word_len] = '\0';
	item->space_before = tx->word_space;
	tx->word_len = 0;
}

static void put_block(TextExtractor *tx, TextItem *item) {
	item->kind = TEXT_BLOCK;
	current_block(tx, &item->block, &item->level);
	tx->need_block = false;
	tx->block_start = true;
}

// Length of the UTF-8 character starting with `lead`, at least 1.
static int char_len(unsigned char lead) {
	if (lead < 0x80) return 1;
	if ((lead & 0xE0) == 0xC0) return 2;
	if ((lead & 0xF0) == 0xE0) return 3;
	if ((lead & 0xF8) == 0xF0) return 4;
	return 1;
}

typedef enum { CH_WORD, CH_SPACE, CH_DROP } CharClass;

// Spaces separate words. A few invisible characters are dropped. Everything
// else, including the no-break space, belongs to a word.
static CharClass classify(const char *s, int len) {
	unsigned char c = (unsigned char)s[0];
	if (len == 1) return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') ? CH_SPACE : CH_WORD;
	if (len == 2 && c == 0xC2 && (unsigned char)s[1] == 0xAD) return CH_DROP; // soft hyphen
	if (len == 3 && c == 0xE2) {
		unsigned char b1 = (unsigned char)s[1], b2 = (unsigned char)s[2];
		if (b1 == 0x80 && (b2 >= 0x82 && b2 <= 0x8A)) return CH_SPACE;       // en, em, thin and other spaces
		if (b1 == 0x80 && (b2 == 0x8B || b2 == 0x8C || b2 == 0x8D)) return CH_DROP; // zero width
		if (b1 == 0x80 && b2 == 0xAF) return CH_SPACE;                        // narrow no-break space
		if (b1 == 0x81 && b2 == 0xA0) return CH_DROP;                         // word joiner
	}
	if (len == 3 && c == 0xE3 && (unsigned char)s[1] == 0x80 && (unsigned char)s[2] == 0x80) return CH_SPACE; // ideographic space
	if (len == 3 && c == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) return CH_DROP;  // byte order mark
	return CH_WORD;
}

// Scans the current text piece until a word is complete. Returns true with
// the word in `item`, or false when the piece is used up.
static bool scan_text(TextExtractor *tx, TextItem *item) {
	const char *text = tx->xml.text;
	size_t len = tx->xml.text_len;

	while (tx->scan < len) {
		int n = char_len((unsigned char)text[tx->scan]);
		if (tx->scan + (size_t)n > len) n = (int)(len - tx->scan); // cut-off character
		const char *ch = text + tx->scan;

		switch (classify(ch, n)) {
		case CH_DROP:
			tx->scan += (size_t)n;
			break;
		case CH_SPACE:
			tx->scan += (size_t)n;
			if (tx->word_len > 0) {
				put_word(tx, item);
				tx->pending_space = true;
				return true;
			}
			tx->pending_space = true;
			break;
		case CH_WORD:
			if (tx->word_len + n > TEXTX_WORD_MAX) {
				// Too long: hand out what we have and carry on in a new word.
				put_word(tx, item);
				tx->word_space = false;
				tx->pending_space = false;
				return true;
			}
			if (tx->word_len == 0) {
				// No space in front of the first word of a block.
				tx->word_space = tx->pending_space && !tx->block_start;
				tx->pending_space = false;
				tx->block_start = false;
			}
			memcpy(tx->word + tx->word_len, ch, (size_t)n);
			tx->word_len += n;
			tx->scan += (size_t)n;
			break;
		}
	}
	return false;
}

static void put_image(TextItem *item, const char *src) {
	item->kind = TEXT_IMAGE;
	snprintf(item->text, sizeof item->text, "%s", src ? src : "");
}

bool textx_next(TextExtractor *tx, TextItem *item) {
	if (tx->done) return false;
	item->text[0] = '\0';
	item->space_before = false;
	item->block = BLOCK_PARAGRAPH;
	item->level = 0;

	for (;;) {
		if (!tx->have_event) {
			tx->event = xml_next(&tx->xml);
			tx->have_event = true;
			tx->scan = 0;
		}
		XmlReader *x = &tx->xml;

		if (tx->event == XML_EOF) {
			if (tx->word_len > 0) {
				put_word(tx, item);
				return true;
			}
			tx->done = true;
			item->kind = TEXT_END;
			return true;
		}

		if (tx->event == XML_TEXT) {
			if (tx->skip_depth > 0) {
				tx->have_event = false;
				continue;
			}
			// A block item has to come before the first word of a block.
			if (tx->need_block && tx->word_len == 0) {
				// Only if this piece has a word in it.
				size_t i = 0;
				bool has_word = false;
				while (i < x->text_len && !has_word) {
					int n = char_len((unsigned char)x->text[i]);
					if (i + (size_t)n > x->text_len) n = (int)(x->text_len - i);
					if (classify(x->text + i, n) == CH_WORD) has_word = true;
					i += (size_t)n;
				}
				if (!has_word) {
					tx->have_event = false;
					tx->pending_space = true;
					continue;
				}
				put_block(tx, item);
				return true;
			}
			if (scan_text(tx, item)) return true;
			tx->have_event = false;
			continue;
		}

		// A tag. Work out what it is.
		const char *name = x->name;
		bool is_void = IN_LIST(void_names, name);

		if (tx->event == XML_END) {
			if (is_void) {
				tx->have_event = false;
				continue;
			}
			if (tx->skip_depth > 0) {
				tx->skip_depth--;
				tx->have_event = false;
				if (tx->skip_depth == 0 && tx->skip_is_block) tx->need_block = true;
				continue;
			}
			BlockKind kind;
			int level;
			if (block_kind(name, &kind, &level)) {
				// A block ends: flush the word first, then close it.
				if (tx->word_len > 0) {
					put_word(tx, item);
					return true;
				}
				pop_block(tx, name);
				tx->need_block = true;
			} else if (strcasecmp(name, "td") == 0 || strcasecmp(name, "th") == 0) {
				// Table cells are separated like words.
				if (tx->word_len > 0) {
					put_word(tx, item);
					return true;
				}
				tx->pending_space = true;
			}
			tx->have_event = false;
			continue;
		}

		// A start tag.
		if (tx->skip_depth > 0) {
			// Inside a skipped part, only images are reported.
			if (strcasecmp(name, "image") == 0) {
				const char *href = xml_attr(x, "href");
				put_image(item, href);
				tx->have_event = false;
				if (!is_void) tx->skip_depth += 1;
				return true;
			}
			if (!is_void) tx->skip_depth++;
			tx->have_event = false;
			continue;
		}

		// Anything that is a break in the text first lets a pending word out.
		BlockKind kind;
		int level;
		bool is_block = block_kind(name, &kind, &level);
		bool is_skip = IN_LIST(skip_names, name);
		const char *type = xml_attr(x, "type"), *role = xml_attr(x, "role");
		bool is_pagebreak = (type && contains_word(type, "pagebreak")) || (role && contains_word(role, "doc-pagebreak"));
		bool is_br = strcasecmp(name, "br") == 0, is_img = strcasecmp(name, "img") == 0;
		bool is_hr = strcasecmp(name, "hr") == 0;

		bool is_cell = strcasecmp(name, "td") == 0 || strcasecmp(name, "th") == 0;
		if ((is_block || is_skip || is_br || is_img || is_hr || is_cell) && tx->word_len > 0) {
			put_word(tx, item);
			return true; // the tag is handled on the next call
		}

		tx->have_event = false;
		if (is_hr) {
			item->kind = TEXT_BLOCK;
			item->block = BLOCK_RULE;
			tx->need_block = true;
			return true;
		}
		if (is_br) {
			item->kind = TEXT_LINEBREAK;
			return true;
		}
		if (is_img) {
			put_image(item, xml_attr(x, "src"));
			return true;
		}
		if (is_skip || is_pagebreak) {
			if (!is_void) tx->skip_depth = 1;
			tx->skip_is_block = is_skip;
			if (is_skip) tx->need_block = true;
			continue;
		}
		if (is_block) {
			push_block(tx, name, kind, level);
			tx->need_block = true;
			continue;
		}
		if (is_cell) tx->pending_space = true;
		// Everything else (b, i, span, a, ...) is inline and changes nothing.
	}
}
