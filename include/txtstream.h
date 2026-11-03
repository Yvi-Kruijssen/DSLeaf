// A plain text file read as a stream of words and breaks (docs: formats.md, decision 074). The structure of
// the stream state is here so that EpubChapter (epub.h) can hold one; the functions are in txtbook.h.
#ifndef DSLEAF_TXTSTREAM_H
#define DSLEAF_TXTSTREAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TXT_LINE_MAX 480 // bytes of UTF-8 in one piece of a line; a longer line comes in several pieces
#define TXT_BUF 512

typedef enum {
	TXT_UTF8,
	TXT_LATIN1,  // Windows-1252, which is Latin-1 with printable characters at 0x80 to 0x9F
	TXT_UTF16LE,
	TXT_UTF16BE,
	TXT_CP1250,      // Windows-1250 (Central European)
	TXT_ISO8859_2,   // ISO-8859-2 (Central European)
	TXT_ISO8859_15,  // ISO-8859-15 (Latin-1 with the euro and a few French and Finnish letters)
	TXT_CP1251,      // Windows-1251 (Cyrillic)
	TXT_GBK,         // GBK and GB2312 (simplified Chinese), two bytes a character (decision 094)
	TXT_BIG5,        // Big5 (traditional Chinese)
	TXT_SJIS,        // Shift-JIS (Japanese)
	TXT_EUCKR,       // EUC-KR (Korean)
} TxtEncoding;

// Reads characters (code points) from a byte range of a file, decoding the encoding.
typedef struct {
	FILE *file;
	TxtEncoding encoding;
	uint32_t end;      // the range ends here (a file offset)
	uint32_t buf_at;   // the file offset of buf[0]
	int buf_len, buf_pos;
	int peek_cp;       // a character read ahead, or -2 for none
	uint8_t buf[TXT_BUF];
} TxtDecoder;

// One piece of a line: its text as UTF-8 without the leading spaces, and what was seen.
typedef struct {
	char text[TXT_LINE_MAX + 8];
	int len;
	int indent;         // the leading white space, tabs counting 4
	bool complete;      // ended with a line break
	bool more;          // cut short, the rest of the line comes in the next piece
	bool continued;     // this piece continues a line that was cut short
	bool eof;           // the end of the range was reached
} TxtLine;

typedef struct {
	TxtDecoder dec;
	bool line_mode;     // every line is a paragraph (the file has no blank lines)
	char carry[TXT_LINE_MAX + 8]; // the rest of a line that was cut short
	int carry_len;
	TxtLine cur, next;  // the line being turned into words, and the one after it
	bool started;
	bool prev_blank;    // the line before was blank (or there was none)
	bool in_paragraph;
	int prev_indent;
	bool prev_glue;     // the line before ended in a hyphen after a letter, so the next word joins it
	bool block_due;     // a BLOCK item is to be given before the next word
	bool title_next;    // the next line is the title of the chapter heading that was just given
	bool break_due;     // a LINEBREAK item is to be given before the next word (between a heading and its title)
	int block_kind;     // BlockKind of the block that is due
	bool first_in_piece; // the next word is the first of its line piece
	bool first_space;    // whether that word has a space before it
	bool glue_cut;       // the next word is the rest of a word that was cut
	const char *word;   // the rest of cur.text
	bool ended;
} TxtStream;

#endif
