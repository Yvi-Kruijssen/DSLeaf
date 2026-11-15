#include "inflate.h"

#include <stddef.h>
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
	// The input buffer, the window and the tables are written before they are read (a distance is checked against
	// the bytes produced so far), so the 40 KB of them are not cleared for every file that is opened.
	memset(s, 0, offsetof(Inflate, inbuf));
	s->read = read;
	s->user = user;
	s->state = ST_HEADER;
}

static inline int next_byte(Inflate *s) {
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

// Fills a lookup table from a code that was just built: every code of at most `bits` bits, at every place the bits that
// follow it can have. The stream holds a code from its first bit, which is the highest of the code, so the index is the
// code with its bits reversed.
static void build_fast(uint16_t *table, int bits, const uint16_t *count, const uint16_t *symbol) {
	memset(table, 0, sizeof table[0] << bits);
	unsigned code = 0;
	int index = 0;
	for (int len = 1; len <= 15; len++) {
		for (int k = 0; k < count[len]; k++, code++) {
			int sym = symbol[index++];
			if (len > bits) continue;
			unsigned rev = 0;
			for (int b = 0; b < len; b++) rev |= ((code >> b) & 1u) << (len - 1 - b);
			for (unsigned at = rev; at < (1u << bits); at += 1u << len) table[at] = (uint16_t)((sym << 4) | len);
		}
		code <<= 1;
	}
}

// Decodes one symbol: from the table if its code is in it, otherwise bit by bit. Returns -1 on bad input.
static inline int decode_fast(Inflate *s, const uint16_t *table, int bits, const uint16_t *count, const uint16_t *symbol) {
	while (s->bitcnt < bits) { // as many bits as the table needs, fewer at the very end of the input
		int b = next_byte(s);
		if (b < 0) break;
		s->bitbuf |= (uint32_t)b << s->bitcnt;
		s->bitcnt += 8;
	}
	unsigned e = table[s->bitbuf & ((1u << bits) - 1)];
	int len = (int)(e & 15);
	if (e != 0 && len <= s->bitcnt) {
		s->bitbuf >>= len;
		s->bitcnt -= len;
		return (int)(e >> 4);
	}
	return decode(s, count, symbol);
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
	build_fast(s->lfast, INFLATE_LIT_BITS, s->lcount, s->lsym);
	build_fast(s->dfast, INFLATE_DIST_BITS, s->dcount, s->dsym);
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
	build_fast(s->lfast, INFLATE_LIT_BITS, s->lcount, s->lsym);
	build_fast(s->dfast, INFLATE_DIST_BITS, s->dcount, s->dsym);
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
		s->bitbuf >>= s->bitcnt & 7; // skip to the next byte boundary (whole bytes read ahead stay)
		s->bitcnt -= s->bitcnt & 7;
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
			uint32_t pos = s->wpos, from = pos - (uint32_t)s->pending_dist;
			int left = s->pending_len;
			size_t room = cap - n;
			if ((size_t)left > room) left = (int)room;
			s->pending_len -= left;
			s->total += (uint32_t)left;
			while (left-- > 0) {
				uint8_t b = s->window[from++ & WMASK];
				s->window[pos++ & WMASK] = b;
				out[n++] = b;
			}
			s->wpos = pos;
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
			// The usual case, as one tight loop (decision 114): while there are 8 bytes of input in the buffer (more
			// than one literal or match can use) the bits are kept in local variables and nothing has to be checked
			// for the end of the input. A code that is not in the tables, a match that does not fit in what the
			// caller asked for, and the last bytes of the input leave the loop for the careful code below.
			if (s->in_len - s->in_pos >= 8) {
				const uint8_t *in = s->inbuf;
				size_t ip = s->in_pos, in_safe = s->in_len - 8;
				uint32_t bb = s->bitbuf, wpos = s->wpos;
				int bc = s->bitcnt;
				size_t n0 = n;
				bool bad = false;
				uint8_t *window = s->window;
				const uint16_t *lfast = s->lfast, *dfast = s->dfast;
				while (n < cap && ip <= in_safe) {
					while (bc <= 24) {
						bb |= (uint32_t)in[ip++] << bc;
						bc += 8;
					}
					unsigned e = lfast[bb & ((1u << INFLATE_LIT_BITS) - 1)];
					if (e == 0) break; // a long code
					int symbol = (int)(e >> 4);
					if (symbol < 256) {
						bb >>= e & 15;
						bc -= (int)(e & 15);
						window[wpos++ & WMASK] = (uint8_t)symbol;
						out[n++] = (uint8_t)symbol;
						continue;
					}
					if (symbol == 256) break; // the end of the block: left to the code below
					symbol -= 257;
					if (symbol >= 29) break;  // (the code below reports it)
					// At least 25 bits: the code (10 at most) and the extra bits of the length (5 at most).
					uint32_t b2 = bb >> (e & 15);
					int c2 = bc - (int)(e & 15);
					int len = len_base[symbol] + (int)(b2 & ((1u << len_extra[symbol]) - 1));
					b2 >>= len_extra[symbol];
					c2 -= len_extra[symbol];
					size_t ip2 = ip;
					while (c2 <= 24) {
						b2 |= (uint32_t)in[ip2++] << c2;
						c2 += 8;
					}
					unsigned d = dfast[b2 & ((1u << INFLATE_DIST_BITS) - 1)];
					if (d == 0 || (size_t)len > cap - n) break; // a long code, or no room: nothing of the match is used up yet
					int dsym = (int)(d >> 4);
					if (dsym >= 30) break;
					b2 >>= d & 15;
					c2 -= (int)(d & 15);
					// At least 18 bits are left: the extra bits of the distance are 13 at most.
					uint32_t dist = dist_base[dsym] + (b2 & ((1u << dist_extra[dsym]) - 1));
					b2 >>= dist_extra[dsym];
					c2 -= dist_extra[dsym];
					bb = b2;
					bc = c2;
					ip = ip2;
					if (dist > s->total + (uint32_t)(n - n0) || dist > INFLATE_WINDOW) {
						bad = true;
						break;
					}
					uint32_t from = wpos - dist;
					do {
						uint8_t b = window[from++ & WMASK];
						window[wpos++ & WMASK] = b;
						out[n++] = b;
					} while (--len > 0);
				}
				s->in_pos = ip;
				s->bitbuf = bb;
				s->bitcnt = bc;
				s->wpos = wpos;
				s->total += (uint32_t)(n - n0);
				if (bad) return fail(s);
				if (n >= cap) break;
			}

			int symbol = decode_fast(s, s->lfast, INFLATE_LIT_BITS, s->lcount, s->lsym);
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

				int dsym = decode_fast(s, s->dfast, INFLATE_DIST_BITS, s->dcount, s->dsym);
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
