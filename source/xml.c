#include "xml.h"

#include <ctype.h>
#include <string.h>

#define REPLACEMENT 0xFFFD
#define ENTITY_MAX 10

typedef struct {
	const char *name;
	uint32_t code;
} Entity;

// The HTML Latin-1 entities, in the order of U+00A0 to U+00FF.
static const char *const latin1_names[96] = {
	"nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf",
	"laquo", "not", "shy", "reg", "macr", "deg", "plusmn", "sup2", "sup3", "acute", "micro",
	"para", "middot", "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest",
	"Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute",
	"Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute",
	"Ocirc", "Otilde", "Ouml", "times", "Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute",
	"THORN", "szlig", "agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig", "ccedil",
	"egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml", "eth", "ntilde",
	"ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave", "uacute",
	"ucirc", "uuml", "yacute", "thorn", "yuml",
};

static const Entity other_entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
	{ "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 },
	{ "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
	{ "sbquo", 0x201A }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bdquo", 0x201E },
	{ "dagger", 0x2020 }, { "Dagger", 0x2021 }, { "bull", 0x2022 }, { "hellip", 0x2026 },
	{ "permil", 0x2030 }, { "prime", 0x2032 }, { "Prime", 0x2033 }, { "lsaquo", 0x2039 },
	{ "rsaquo", 0x203A }, { "euro", 0x20AC }, { "trade", 0x2122 }, { "larr", 0x2190 },
	{ "rarr", 0x2192 }, { "minus", 0x2212 }, { "OElig", 0x0152 }, { "oelig", 0x0153 },
	{ "Scaron", 0x0160 }, { "scaron", 0x0161 }, { "Yuml", 0x0178 }, { "fnof", 0x0192 },
	{ "circ", 0x02C6 }, { "tilde", 0x02DC }, { "zwnj", 0x200C }, { "zwj", 0x200D },
};

static uint32_t lookup_entity(const char *name) {
	for (int i = 0; i < 96; i++) {
		if (strcmp(name, latin1_names[i]) == 0) return 0xA0u + (uint32_t)i;
	}
	for (size_t i = 0; i < sizeof other_entities / sizeof other_entities[0]; i++) {
		if (strcmp(name, other_entities[i].name) == 0) return other_entities[i].code;
	}
	return REPLACEMENT;
}

static int utf8_encode(uint32_t cp, char *out) {
	if (cp < 0x80) { out[0] = (char)cp; return 1; }
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

void xml_init(XmlReader *r, XmlReadFn read, void *user) {
	memset(r, 0, sizeof *r);
	r->read = read;
	r->user = user;
	r->pushed = -1;
}

static int rd(XmlReader *r) {
	if (r->pushed >= 0) {
		int c = r->pushed;
		r->pushed = -1;
		return c;
	}
	if (r->pos == r->len) {
		if (r->eof) return -1;
		size_t n = r->read(r->user, r->buf, sizeof r->buf);
		if (n == 0) {
			r->eof = true;
			return -1;
		}
		r->len = n;
		r->pos = 0;
	}
	return r->buf[r->pos++];
}

static void unrd(XmlReader *r, int c) {
	if (c >= 0) r->pushed = c;
}

static bool is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Somewhere to put decoded characters. Characters that do not fit whole are dropped.
typedef struct {
	char *buf;
	size_t len, cap;
} Sink;

static void sink_byte(Sink *s, char c) {
	if (s->len < s->cap) s->buf[s->len++] = c;
}

static void sink_cp(Sink *s, uint32_t cp) {
	char tmp[4];
	int n = utf8_encode(cp, tmp);
	if (s->len + (size_t)n > s->cap) return;
	memcpy(s->buf + s->len, tmp, (size_t)n);
	s->len += (size_t)n;
}

// Called after '&'. Decodes an entity into `cp` and returns true. If what
// follows is not an entity, returns false with the characters read so far in
// `literal` (without the '&'), and any character that ended the search is put back.
static bool read_entity(XmlReader *r, uint32_t *cp, char *literal, int *literal_len) {
	char name[ENTITY_MAX + 1];
	int n = 0;
	for (;;) {
		int c = rd(r);
		if (c == ';') break;
		if (c < 0 || c == '<' || c == '&' || is_space(c) || n >= ENTITY_MAX) {
			unrd(r, c);
			memcpy(literal, name, (size_t)n);
			*literal_len = n;
			return false;
		}
		name[n++] = (char)c;
	}
	name[n] = '\0';

	if (name[0] == '#') {
		const char *digits = name + 1;
		int base = 10;
		if (*digits == 'x' || *digits == 'X') {
			base = 16;
			digits++;
		}
		uint32_t value = 0;
		bool ok = *digits != '\0';
		for (; *digits && ok; digits++) {
			int d = isdigit((unsigned char)*digits) ? *digits - '0'
			        : (base == 16 && isxdigit((unsigned char)*digits)) ? (tolower((unsigned char)*digits) - 'a' + 10)
			                                                           : -1;
			if (d < 0) ok = false;
			else value = value * (uint32_t)base + (uint32_t)d;
			if (value > 0x10FFFF) ok = false;
		}
		if (!ok || value == 0 || (value >= 0xD800 && value <= 0xDFFF)) value = REPLACEMENT;
		*cp = value;
		return true;
	}
	*cp = lookup_entity(name);
	return true;
}

// Reads text with entities up to `stop` (or one of `stop` in unquoted mode).
static void put_entity_or_literal(XmlReader *r, Sink *out) {
	uint32_t cp;
	char literal[ENTITY_MAX + 1];
	int literal_len;
	if (read_entity(r, &cp, literal, &literal_len)) {
		sink_cp(out, cp);
	} else {
		sink_byte(out, '&');
		for (int i = 0; i < literal_len; i++) sink_byte(out, literal[i]);
	}
}

// Skips to the end of a comment, processing instruction or declaration.
static void skip_until(XmlReader *r, const char *end) {
	size_t len = strlen(end), matched = 0;
	int c;
	while (matched < len && (c = rd(r)) >= 0) {
		if (c == end[matched]) matched++;
		else matched = (c == end[0]) ? 1 : 0;
	}
}

// Skips a <!DOCTYPE ...>, which may have an [internal subset] with its own '>' characters.
static void skip_declaration(XmlReader *r) {
	int depth = 0, quote = 0, c;
	while ((c = rd(r)) >= 0) {
		if (quote) {
			if (c == quote) quote = 0;
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '[') {
			depth++;
		} else if (c == ']') {
			if (depth > 0) depth--;
		} else if (c == '>' && depth == 0) {
			return;
		}
	}
}

static void finish_text(XmlReader *r, Sink *s) {
	r->text_len = s->len;
	r->text[s->len] = '\0';
}

// Text up to the next '<'. Returns true if there is any.
static bool read_text(XmlReader *r, int first) {
	Sink s = { r->text, 0, XML_TEXT_MAX };
	int c = first;
	for (;;) {
		if (c == '<') {
			unrd(r, c);
			break;
		}
		if (c == '&') {
			put_entity_or_literal(r, &s);
		} else {
			sink_byte(&s, (char)c);
		}
		c = rd(r);
		if (c < 0) break;
		// Start a new piece at a character boundary when this one is nearly full.
		if (s.len >= XML_TEXT_MAX - 8 && (c & 0xC0) != 0x80) {
			unrd(r, c);
			break;
		}
	}
	finish_text(r, &s);
	return s.len > 0;
}

static bool read_cdata(XmlReader *r) {
	Sink s = { r->text, 0, XML_TEXT_MAX };
	int brackets = 0, c;
	while ((c = rd(r)) >= 0) {
		if (c == ']') {
			if (brackets == 2) sink_byte(&s, ']');
			else brackets++;
			continue;
		}
		if (c == '>' && brackets == 2) {
			r->in_cdata = false;
			break;
		}
		for (; brackets > 0; brackets--) sink_byte(&s, ']');
		sink_byte(&s, (char)c);
		if (s.len >= XML_TEXT_MAX - 8 && (c & 0x80) == 0) break;
	}
	if (c < 0) {
		for (; brackets > 0; brackets--) sink_byte(&s, ']');
		r->in_cdata = false;
	}
	finish_text(r, &s);
	return s.len > 0;
}

// Copies a name without its prefix ("dc:title" gives "title").
static void store_name(char *dst, size_t cap, const char *raw) {
	const char *colon = strrchr(raw, ':');
	const char *local = colon ? colon + 1 : raw;
	size_t n = strlen(local);
	if (n >= cap) n = cap - 1;
	memcpy(dst, local, n);
	dst[n] = '\0';
}

static void read_attr_value(XmlReader *r, char *dst, size_t cap) {
	Sink s = { dst, 0, cap - 1 };
	int q = rd(r);

	if (q == '"' || q == '\'') {
		int c;
		while ((c = rd(r)) >= 0 && c != q) {
			if (c == '&') put_entity_or_literal(r, &s);
			else sink_byte(&s, (char)c);
		}
	} else {
		// Unquoted, as in sloppy HTML: runs until a space or the end of the tag.
		int c = q;
		while (c >= 0 && !is_space(c) && c != '>') {
			if (c == '&') put_entity_or_literal(r, &s);
			else sink_byte(&s, (char)c);
			c = rd(r);
		}
		unrd(r, c);
	}
	dst[s.len] = '\0';
}

static void skip_spaces(XmlReader *r) {
	int c;
	do c = rd(r); while (is_space(c));
	unrd(r, c);
}

// After '<' and the first name character. Returns XML_START.
static XmlEvent read_start_tag(XmlReader *r, int first) {
	char raw[XML_NAME_MAX + 16];
	size_t n = 0;
	int c = first;
	while (c >= 0 && !is_space(c) && c != '/' && c != '>') {
		if (n < sizeof raw - 1) raw[n++] = (char)c;
		c = rd(r);
	}
	raw[n] = '\0';
	unrd(r, c);
	store_name(r->name, sizeof r->name, raw);
	r->attr_count = 0;

	for (;;) {
		skip_spaces(r);
		c = rd(r);
		if (c < 0 || c == '>') break;
		if (c == '/') {
			int next = rd(r);
			if (next == '>') {
				r->pending_end = true;
				break;
			}
			unrd(r, next);
			continue;
		}

		char attr_raw[XML_ATTR_NAME_MAX + 16];
		size_t len = 0;
		while (c >= 0 && !is_space(c) && c != '=' && c != '>' && c != '/') {
			if (len < sizeof attr_raw - 1) attr_raw[len++] = (char)c;
			c = rd(r);
		}
		attr_raw[len] = '\0';
		unrd(r, c);

		XmlAttr scratch;
		XmlAttr *slot = r->attr_count < XML_ATTR_MAX ? &r->attr[r->attr_count] : &scratch;
		store_name(slot->name, sizeof slot->name, attr_raw);
		slot->value[0] = '\0';

		skip_spaces(r);
		c = rd(r);
		if (c == '=') {
			skip_spaces(r);
			read_attr_value(r, slot->value, sizeof slot->value);
		} else {
			unrd(r, c);
		}
		if (slot != &scratch) r->attr_count++;
	}
	return XML_START;
}

static XmlEvent read_end_tag(XmlReader *r) {
	char raw[XML_NAME_MAX + 16];
	size_t n = 0;
	int c;
	skip_spaces(r);
	while ((c = rd(r)) >= 0 && c != '>' && !is_space(c)) {
		if (n < sizeof raw - 1) raw[n++] = (char)c;
	}
	raw[n] = '\0';
	while (c >= 0 && c != '>') c = rd(r);
	store_name(r->name, sizeof r->name, raw);
	return XML_END;
}

XmlEvent xml_next(XmlReader *r) {
	if (r->pending_end) {
		r->pending_end = false;
		return XML_END;
	}

	for (;;) {
		if (r->in_cdata) {
			if (read_cdata(r)) return XML_TEXT;
			continue;
		}

		int c = rd(r);
		if (c < 0) return XML_EOF;
		if (c != '<') {
			if (read_text(r, c)) return XML_TEXT;
			continue;
		}

		c = rd(r);
		if (c < 0) return XML_EOF;
		if (c == '/') return read_end_tag(r);
		if (c == '?') {
			skip_until(r, "?>");
		} else if (c == '!') {
			int d = rd(r);
			if (d == '-') {
				rd(r); // the second '-'
				skip_until(r, "-->");
			} else if (d == '[') {
				// <![CDATA[ ... ]]>
				const char *want = "CDATA[";
				size_t i = 0;
				int e;
				while (want[i] && (e = rd(r)) == want[i]) i++;
				if (want[i] == '\0') r->in_cdata = true;
				else skip_declaration(r);
			} else {
				unrd(r, d);
				skip_declaration(r);
			}
		} else if (is_space(c) || c == '>') {
			// A stray '<' in text: treat it as a literal character.
			r->text[0] = '<';
			r->text_len = 1;
			r->text[1] = '\0';
			unrd(r, c);
			return XML_TEXT;
		} else {
			return read_start_tag(r, c);
		}
	}
}

const char *xml_attr(const XmlReader *r, const char *name) {
	for (int i = 0; i < r->attr_count; i++) {
		if (strcmp(r->attr[i].name, name) == 0) return r->attr[i].value;
	}
	return NULL;
}
