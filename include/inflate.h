// Streaming deflate decoder (docs: formats.md, decision 037).
//
// Decodes a raw deflate stream (as stored in zip files) on demand, so a large
// chapter never has to be in memory all at once. Uses a 32 KB window.
//
// The structure follows the well known "puff" decoder by Mark Adler (zlib
// licence): canonical Huffman codes decoded bit by bit. It is small and easy
// to check. A table lookup would be faster if speed turns out to matter.
#ifndef DSLEAF_INFLATE_H
#define DSLEAF_INFLATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Supplies compressed bytes. Returns how many were written to `buf` (at most
// `cap`), or 0 when there is no more input.
typedef size_t (*InflateReadFn)(void *user, uint8_t *buf, size_t cap);

#define INFLATE_WINDOW 32768

typedef struct {
	InflateReadFn read;
	void *user;

	uint8_t inbuf[512];
	size_t in_pos, in_len;
	bool in_eof;

	uint32_t bitbuf;
	int bitcnt;

	uint8_t window[INFLATE_WINDOW];
	uint32_t wpos;
	uint32_t total; // bytes produced so far

	int state;
	bool final_block;
	uint32_t stored_left;
	int pending_len, pending_dist; // a match that did not fit in the caller's buffer

	uint16_t lcount[16], lsym[288]; // literal and length codes
	uint16_t dcount[16], dsym[30];  // distance codes
} Inflate;

void inflate_init(Inflate *s, InflateReadFn read, void *user);

// Produces up to `cap` bytes. Returns the number produced, 0 at the end of the
// stream, or -1 if the data is damaged or ends too early.
int inflate_read(Inflate *s, uint8_t *out, size_t cap);

#endif
