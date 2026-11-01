#include "inflate.h"

#include <string.h>

#define WMASK (INFLATE_WINDOW - 1)

enum { ST_HEADER, ST_STORED, ST_CODES, ST_DONE, ST_ERROR };

static const uint16_t len_base[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint8_t len_extra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t dist_base[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const uint8_t dist_extra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

void inflate_init(Inflate *s, InflateReadFn read, void *user) {
	memset(s, 0, sizeof *s);
	s->read = read;
	s->user = user;
	s->state = ST_HEADER;
}

static int next_byte(Inflate *s) {
	if (s->in_pos == s->in_len) {
		if (s->in_eof) return -1;
		size_t n = s->read(s->user, s->inbuf, sizeof s->inbuf);
		if (n == 0) {
			s->in_eof = true;
			return -1;
		}
		s->in_len = n;
		s->in_pos = 0;
	}
	return s->inbuf[s->in_pos++];
}

// Reads `need` bits (0 to 16), least significant first. Returns -1 if the input ends.
static int get_bits(Inflate *s, int need) {
	uint32_t value = s->bitbuf;
	while (s->bitcnt < need) {
		int b = next_byte(s);
		if (b < 0) return -1;
		value |= (uint32_t)b << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = value >> need;
	s->bitcnt -= need;
	return (int)(value & ((1u << need) - 1));
}

// Builds the decoding tables from code lengths. Returns 0 for a complete code,
// a positive number for an incomplete one, and a negative number if the code
// is over-subscribed.
static int construct(uint16_t *count, uint16_t *symbol, const uint16_t *length, int n) {
	uint16_t offs[16];
	for (int len = 0; len <= 15; len++) count[len] = 0;
	for (int sym = 0; sym < n; sym++) count[length[sym]]++;
	if (count[0] == n) return 0;

	int left = 1;
	for (int len = 1; len <= 15; len++) {
		left <<= 1;
		left -= count[len];
		if (left < 0) return left;
	}

	offs[1] = 0;
	for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + count[len]);
	for (int sym = 0; sym < n; sym++) {
		if (length[sym] != 0) symbol[offs[length[sym]]++] = (uint16_t)sym;
	}
	return left;
}

// Decodes one symbol bit by bit. Returns -1 on bad input.
static int decode(Inflate *s, const uint16_t *count, const uint16_t *symbol) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		int bit = get_bits(s, 1);
		if (bit < 0) return -1;
		code |= bit;
		int c = count[len];
		if (code - c < first) return symbol[index + (code - first)];
		index += c;
		first += c;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static void build_fixed(Inflate *s) {
	uint16_t lengths[288];
	int i = 0;
	for (; i < 144; i++) lengths[i] = 8;
	for (; i < 256; i++) lengths[i] = 9;
	for (; i < 280; i++) lengths[i] = 7;
	for (; i < 288; i++) lengths[i] = 8;
	construct(s->lcount, s->lsym, lengths, 288);

	for (i = 0; i < 30; i++) lengths[i] = 5;
	construct(s->dcount, s->dsym, lengths, 30);
}

static bool build_dynamic(Inflate *s) {
	static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
	uint16_t lengths[320];
	uint16_t code_count[16], code_sym[19];

	int nlen = get_bits(s, 5), ndist = get_bits(s, 5), ncode = get_bits(s, 4);
	if (nlen < 0 || ndist < 0 || ncode < 0) return false;
	nlen += 257;
	ndist += 1;
	ncode += 4;
	if (nlen > 286 || ndist > 30) return false;

	for (int i = 0; i < 19; i++) lengths[i] = 0;
	for (int i = 0; i < ncode; i++) {
		int v = get_bits(s, 3);
		if (v < 0) return false;
		lengths[order[i]] = (uint16_t)v;
	}
	if (construct(code_count, code_sym, lengths, 19) != 0) return false;

	int index = 0;
	while (index < nlen + ndist) {
		int symbol = decode(s, code_count, code_sym);
		if (symbol < 0) return false;
		if (symbol < 16) {
			lengths[index++] = (uint16_t)symbol;
			continue;
		}

		int len = 0, repeat;
		if (symbol == 16) {
			if (index == 0) return false;
			len = lengths[index - 1];
			repeat = get_bits(s, 2);
			if (repeat < 0) return false;
			repeat += 3;
		} else if (symbol == 17) {
			repeat = get_bits(s, 3);
			if (repeat < 0) return false;
			repeat += 3;
		} else {
			repeat = get_bits(s, 7);
			if (repeat < 0) return false;
			repeat += 11;
		}
		if (index + repeat > nlen + ndist) return false;
		while (repeat--) lengths[index++] = (uint16_t)len;
	}

	if (lengths[256] == 0) return false; // no end-of-block code

	int err = construct(s->lcount, s->lsym, lengths, nlen);
	if (err && (err < 0 || nlen != s->lcount[0] + s->lcount[1])) return false;
	err = construct(s->dcount, s->dsym, lengths + nlen, ndist);
	if (err && (err < 0 || ndist != s->dcount[0] + s->dcount[1])) return false;
	return true;
}

static int fail(Inflate *s) {
	s->state = ST_ERROR;
	return -1;
}

static void emit(Inflate *s, uint8_t *out, size_t *n, uint8_t byte) {
	s->window[s->wpos++ & WMASK] = byte;
	s->total++;
	out[(*n)++] = byte;
}

// Reads a block header. Returns false on bad input.
static bool start_block(Inflate *s) {
	int final = get_bits(s, 1);
	int type = get_bits(s, 2);
	if (final < 0 || type < 0) return false;
	s->final_block = final != 0;

	if (type == 0) {
		s->bitbuf = 0; // skip to the next byte boundary
		s->bitcnt = 0;
		int len = get_bits(s, 16), nlen = get_bits(s, 16);
		if (len < 0 || nlen < 0 || (len ^ 0xFFFF) != nlen) return false;
		s->stored_left = (uint32_t)len;
		s->state = ST_STORED;
	} else if (type == 1) {
		build_fixed(s);
		s->state = ST_CODES;
	} else if (type == 2) {
		if (!build_dynamic(s)) return false;
		s->state = ST_CODES;
	} else {
		return false;
	}
	return true;
}

int inflate_read(Inflate *s, uint8_t *out, size_t cap) {
	size_t n = 0;
	if (s->state == ST_ERROR) return -1;
	if (cap > 0x7FFFFFFF) cap = 0x7FFFFFFF;

	while (n < cap) {
		if (s->pending_len > 0) {
			uint8_t b = s->window[(s->wpos - (uint32_t)s->pending_dist) & WMASK];
			emit(s, out, &n, b);
			s->pending_len--;
			continue;
		}

		switch (s->state) {
		case ST_DONE:
			return (int)n;

		case ST_HEADER:
			if (!start_block(s)) return fail(s);
			break;

		case ST_STORED:
			if (s->stored_left == 0) {
				s->state = s->final_block ? ST_DONE : ST_HEADER;
				break;
			}
			{
				int b = next_byte(s);
				if (b < 0) return fail(s);
				emit(s, out, &n, (uint8_t)b);
				s->stored_left--;
			}
			break;

		case ST_CODES: {
			int symbol = decode(s, s->lcount, s->lsym);
			if (symbol < 0) return fail(s);

			if (symbol < 256) {
				emit(s, out, &n, (uint8_t)symbol);
			} else if (symbol == 256) {
				s->state = s->final_block ? ST_DONE : ST_HEADER;
			} else {
				symbol -= 257;
				if (symbol >= 29) return fail(s);
				int extra = get_bits(s, len_extra[symbol]);
				if (extra < 0) return fail(s);
				int len = len_base[symbol] + extra;

				int dsym = decode(s, s->dcount, s->dsym);
				if (dsym < 0 || dsym >= 30) return fail(s);
				extra = get_bits(s, dist_extra[dsym]);
				if (extra < 0) return fail(s);
				int dist = dist_base[dsym] + extra;

				if ((uint32_t)dist > s->total || dist > INFLATE_WINDOW) return fail(s);
				s->pending_len = len;
				s->pending_dist = dist;
			}
			break;
		}

		default:
			return fail(s);
		}
	}
	return (int)n;
}
