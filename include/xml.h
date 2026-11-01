// Small streaming XML/XHTML reader (docs: formats.md, decision 039).
//
// Reads one event at a time from a byte source, so a 168 KB chapter and a
// 1 KB description file use the same few hundred bytes of memory. It is
// forgiving, because real EPUBs are not always tidy: unknown entities, odd
// quoting and truncated files do not stop it, and it never needs a closing tag
// to match an opening one.
//
// What it does:
// - Elements and attributes with namespace prefixes lose the prefix
//   ("dc:title" becomes "title"). Names keep their letter case, so compare with
//   strcasecmp for HTML-style files.
// - A tag like <br/> gives a START event and then an END event.
// - Text has entities decoded to UTF-8: the five XML ones, numeric ones, and
//   the common HTML ones (nbsp, mdash, eacute and so on). Unknown entities
//   become U+FFFD. CDATA is returned as text.
// - Comments, processing instructions and the DOCTYPE are skipped.
// - Long text comes in pieces of up to XML_TEXT_MAX bytes, never split inside
//   a UTF-8 character or an entity.
// - Limits: names are cut at XML_NAME_MAX, at most XML_ATTR_MAX attributes are
//   kept (the rest are skipped) and values are cut at XML_ATTR_VALUE_MAX.
// - The text is assumed to be UTF-8. A declared other encoding is ignored.
#ifndef DSLEAF_XML_H
#define DSLEAF_XML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XML_NAME_MAX 32
#define XML_ATTR_MAX 8
#define XML_ATTR_NAME_MAX 24
#define XML_ATTR_VALUE_MAX 192
#define XML_TEXT_MAX 128

typedef enum {
	XML_EOF,   // no more input
	XML_START, // `name` and the attributes are set
	XML_END,   // `name` is set
	XML_TEXT,  // `text` holds a piece of text, NUL terminated, `text_len` bytes
} XmlEvent;

// Supplies bytes. Returns how many were written to `buf`, or 0 at the end.
typedef size_t (*XmlReadFn)(void *user, uint8_t *buf, size_t cap);

typedef struct {
	char name[XML_ATTR_NAME_MAX];
	char value[XML_ATTR_VALUE_MAX];
} XmlAttr;

typedef struct {
	XmlReadFn read;
	void *user;
	uint8_t buf[512];
	size_t pos, len;
	bool eof;
	int pushed; // one character of lookahead, or -1
	bool pending_end;
	bool in_cdata;

	// The current event.
	char name[XML_NAME_MAX];
	XmlAttr attr[XML_ATTR_MAX];
	int attr_count;
	bool self_closing; // the START tag was written like <br/>. An END event follows.
	char text[XML_TEXT_MAX + 1];
	size_t text_len;
} XmlReader;

void xml_init(XmlReader *r, XmlReadFn read, void *user);

XmlEvent xml_next(XmlReader *r);

// Value of an attribute of the current START event, or NULL if it has none
// with that exact name.
const char *xml_attr(const XmlReader *r, const char *name);

#endif
