// Baseline JPEG decoder (docs: decision 075): 8 bit, Huffman coded, one interleaved scan, any
// sampling up to 4x4, restart markers. Progressive, arithmetic coded and CMYK files are refused
// (DEC_UNSUPPORTED). When the screen could not show all the detail the picture is decoded at 1/2,
// 1/4 or 1/8 of its size by using only the low frequencies of each block, which is much faster.
#include <stdlib.h>
#include <string.h>

#include "imagedec.h"

// IDCT tables for the reduced sizes (the 2 x 2 one is written out in idct): table[x][u] = a(u) * sqrt(N / 8) * cos((2x + 1) u pi / (2N)) * 4096
static const int16_t idct1[1][1] = {
	{ 1448 },
};
static const int16_t idct4[4][4] = {
	{ 1448, 1892, 1448, 784 },
	{ 1448, 784, -1448, -1892 },
	{ 1448, -784, -1448, 1892 },
	{ 1448, -1892, 1448, -784 },
};
static const int16_t idct8[8][8] = {
	{ 1448, 2009, 1892, 1703, 1448, 1138, 784, 400 },
	{ 1448, 1703, 784, -400, -1448, -2009, -1892, -1138 },
	{ 1448, 1138, -784, -2009, -1448, 400, 1892, 1703 },
	{ 1448, 400, -1892, -1138, 1448, 1703, -784, -2009 },
	{ 1448, -400, -1892, 1138, 1448, -1703, -784, 2009 },
	{ 1448, -1138, -784, 2009, -1448, -400, 1892, -1703 },
	{ 1448, -1703, 784, 400, -1448, 2009, -1892, 1138 },
	{ 1448, -2009, 1892, -1703, 1448, -1138, 784, -400 },
};

static const uint8_t zigzag[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

typedef struct {
	uint8_t bits[17];
	uint8_t vals[256];
	int32_t maxcode[18];
	int32_t valptr[17];
	int32_t mincode[17];
	uint16_t look[512]; // 9 bit lookahead: length << 8 | symbol, 0 when the code is longer
	bool present;
} Huff;

typedef struct {
	int id, h, v, tq;
	int dc_table, ac_table;
	int pred;
	uint8_t *plane;
	int plane_w, plane_h; // pixels of one MCU row of this component, at the decoded size
} Comp;

typedef struct {
	ImageReadFn read;
	void *user;
	uint8_t buf[4096]; // read in big pieces: each call to the zip reader and the card has a cost (decision 112)
	size_t pos, len;
	bool eof;

	uint32_t bitbuf;
	int bitcnt;
	int marker; // a marker met inside the data, 0 for none

	uint16_t qt[4][64]; // natural order
	bool qt_present[4];
	Huff dc[4], ac[4];
	// For the AC tables: for a code and its extra bits that fit in 9 bits, the whole coefficient at once (decision 112):
	// value << 16 | run << 8 | total bits, or just the length of the code and AC_EOB for the end of the block. 0 if not.
	uint32_t ac_fast[4][512];
	int restart_interval;
	int adobe_transform;
} Dec;

static int get_byte(Dec *d) {
	if (d->pos == d->len) {
		if (d->eof) return -1;
		d->len = d->read(d->user, d->buf, sizeof d->buf);
		d->pos = 0;
		if (d->len == 0) {
			d->eof = true;
			return -1;
		}
	}
	return d->buf[d->pos++];
}

static int get_u16(Dec *d) {
	int a = get_byte(d), b = get_byte(d);
	if (a < 0 || b < 0) return -1;
	return (a << 8) | b;
}

// The next byte of the entropy coded data with the 0xFF 0x00 stuffing taken out. A marker, or the end of the file,
// stops the data: zeros come after it (the marker is kept in d->marker).
static int entropy_byte(Dec *d) {
	if (d->marker) return 0;
	int b = get_byte(d);
	if (b < 0) {
		d->marker = 0xD9; // the file ends: feed zeros
		return 0;
	}
	if (b == 0xFF) {
		int n = get_byte(d);
		while (n == 0xFF) n = get_byte(d);
		if (n == 0) return 0xFF;
		d->marker = n < 0 ? 0xD9 : n;
		return 0;
	}
	return b;
}

// Fills the bit buffer (kept in locals by the block decoder) up to at least 25 bits. The common case, bytes that
// are in the buffer and not 0xFF, does not leave the loop.
#define REFILL(d, bb, bc)                                                           \
	do {                                                                            \
		while ((bc) <= 24) {                                                        \
			int b_;                                                                 \
			if ((d)->pos < (d)->len && !(d)->marker && (d)->buf[(d)->pos] != 0xFF) b_ = (d)->buf[(d)->pos++]; \
			else b_ = entropy_byte(d);                                              \
			(bb) |= (uint32_t)b_ << (24 - (bc));                                    \
			(bc) += 8;                                                              \
		}                                                                           \
	} while (0)

#define AC_EOB 0x1000

// Fills `fast` from the `look` of an AC table (see Dec.ac_fast).
static void build_fast_ac(const Huff *h, uint32_t fast[512]) {
	for (int i = 0; i < 512; i++) {
		fast[i] = 0;
		int e = h->look[i];
		if (!e) continue;
		int l = e >> 8, rs = e & 0xFF, r = rs >> 4, s = rs & 15;
		if (rs == 0) { // end of block
			fast[i] = AC_EOB | (uint32_t)l;
		} else if (s > 0 && l + s <= 9) {
			int v = (i >> (9 - l - s)) & ((1 << s) - 1);
			if (v < (1 << (s - 1))) v = v - (1 << s) + 1;
			fast[i] = ((uint32_t)(uint16_t)(int16_t)v << 16) | (uint32_t)(r << 8) | (uint32_t)(l + s);
		}
	}
}

static bool build_huff(Huff *h) {
	int code = 0, k = 0;
	memset(h->look, 0, sizeof h->look);
	for (int l = 1; l <= 16; l++) {
		h->valptr[l] = k;
		h->mincode[l] = code;
		for (int i = 0; i < h->bits[l]; i++, k++) {
			if (k >= 256 || code >= (1 << l)) return false;
			if (l <= 9) {
				int first = code << (9 - l), count = 1 << (9 - l);
				for (int j = 0; j < count; j++) h->look[first + j] = (uint16_t)((l << 8) | h->vals[k]);
			}
			code++;
		}
		h->maxcode[l] = h->bits[l] ? code - 1 : -1;
		code <<= 1;
	}
	h->maxcode[17] = 0x7FFFFFFF;
	h->present = true;
	return true;
}

// The code in the top bits of `bb` when it is longer than the 9 bits of the lookahead table. Returns the symbol and
// the length in *len, or -1 if there is no such code.
static int decode_long(const Huff *h, uint32_t bb, int *len) {
	int code = (int)(bb >> 22); // 10 bits
	int l = 10;
	while (l <= 16 && (h->maxcode[l] < 0 || code > h->maxcode[l])) {
		l++;
		code = (int)(bb >> (32 - l));
	}
	if (l > 16) return -1;
	*len = l;
	return h->vals[h->valptr[l] + code - h->mincode[l]];
}

// Decodes one block into `out` (natural order, dequantised, only the coefficients below n x n are kept and only
// those are cleared first). Returns false if the data is bad. *rows is one more than the highest row that has a
// coefficient, and *only_dc is true when there is nothing but the DC value, so the inverse DCT can take shortcuts.
// The bit buffer lives in local variables while the block is decoded (decision 112).
static bool decode_block(Dec *d, Comp *c, int n, int16_t out[64], int *rows, bool *only_dc) {
	for (int v = 0; v < n; v++) memset(out + v * 8, 0, (size_t)n * sizeof(int16_t));
	const Huff *dc = &d->dc[c->dc_table], *ac = &d->ac[c->ac_table];
	const uint32_t *fast_ac = d->ac_fast[c->ac_table];
	const uint16_t *q = d->qt[c->tq];
	uint32_t bb = d->bitbuf;
	int bc = d->bitcnt;
	int maxrow = 0;
	bool any_ac = false;

	// DC
	REFILL(d, bb, bc);
	int e = dc->look[bb >> 23], l, t;
	if (e) {
		l = e >> 8;
		t = e & 0xFF;
	} else if ((t = decode_long(dc, bb, &l)) < 0) {
		return false;
	}
	bb <<= l;
	bc -= l;
	if (t > 15) return false;
	if (t) {
		if (bc < t) REFILL(d, bb, bc);
		int v = (int)(bb >> (32 - t));
		bb <<= t;
		bc -= t;
		c->pred += v < (1 << (t - 1)) ? v - (1 << t) + 1 : v;
	}
	int dcv = c->pred * q[0];
	out[0] = (int16_t)(dcv > 2047 ? 2047 : (dcv < -2047 ? -2047 : dcv));

	for (int k = 1; k < 64;) {
		if (bc < 25) REFILL(d, bb, bc);
		uint32_t fa = fast_ac[bb >> 23];
		if (fa) { // a short code with its value: the common case
			l = (int)(fa & 0xFF);
			bb <<= l;
			bc -= l;
			if (fa & AC_EOB) break;
			k += (int)((fa >> 8) & 15);
			if (k > 63) return false;
			int pos = zigzag[k];
			if ((pos & 7) < n && (pos >> 3) < n) {
				int val = (int)(int16_t)(fa >> 16) * q[pos];
				val = val > 2047 ? 2047 : (val < -2047 ? -2047 : val);
				out[pos] = (int16_t)val;
				if (val) {
					any_ac = true;
					if ((pos >> 3) > maxrow) maxrow = pos >> 3;
				}
			}
			k++;
			continue;
		}
		e = ac->look[bb >> 23];
		int rs;
		if (e) {
			l = e >> 8;
			rs = e & 0xFF;
		} else if ((rs = decode_long(ac, bb, &l)) < 0) {
			return false;
		}
		bb <<= l;
		bc -= l;
		int r = rs >> 4, s = rs & 15;
		if (s == 0) {
			if (r != 15) break;
			k += 16;
			continue;
		}
		k += r;
		if (k > 63) return false;
		if (bc < s) REFILL(d, bb, bc);
		int pos = zigzag[k];
		if ((pos & 7) < n && (pos >> 3) < n) {
			int v = (int)(bb >> (32 - s));
			int coef = v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
			int val = coef * q[pos];
			val = val > 2047 ? 2047 : (val < -2047 ? -2047 : val);
			out[pos] = (int16_t)val;
			if (val) {
				any_ac = true;
				if ((pos >> 3) > maxrow) maxrow = pos >> 3;
			}
		} // a coefficient the reduced size does not use is only skipped
		bb <<= s;
		bc -= s;
		k++;
	}
	d->bitbuf = bb;
	d->bitcnt = bc;
	*rows = maxrow + 1;
	*only_dc = !any_ac;
	return true;
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Inverse DCT of the n x n low frequencies, written as n x n pixels at `dst` with row distance `stride`. `rows` is
// the number of rows of coefficients that can be non zero. The results do not depend on the shortcuts.
static void idct(const int16_t *f, int n, uint8_t *dst, int stride, int rows, bool only_dc) {
	if (only_dc) { // every pixel gets the same value (the first column of every table is 1448)
		int tmp0 = (1448 * f[0]) >> 9;
		uint8_t px = clamp8(((1448 * tmp0 + (1 << 14)) >> 15) + 128);
		for (int y = 0; y < n; y++) memset(dst + y * stride, px, (size_t)n);
		return;
	}
	if (n == 2) {
		int a0 = 1448 * (f[0] + f[1]) >> 9, a1 = 1448 * (f[0] - f[1]) >> 9; // row 0 of coefficients
		int b0 = 1448 * (f[8] + f[9]) >> 9, b1 = 1448 * (f[8] - f[9]) >> 9; // row 1
		dst[0] = clamp8(((1448 * a0 + 1448 * b0 + (1 << 14)) >> 15) + 128);
		dst[1] = clamp8(((1448 * a1 + 1448 * b1 + (1 << 14)) >> 15) + 128);
		dst[stride] = clamp8(((1448 * a0 - 1448 * b0 + (1 << 14)) >> 15) + 128);
		dst[stride + 1] = clamp8(((1448 * a1 - 1448 * b1 + (1 << 14)) >> 15) + 128);
		return;
	}
	if (n == 4) { // the same sums as the table below, with the symmetry of the cosines used (the results are the same)
		int tmp[4][4] = { { 0 } };
		for (int v = 0; v < rows; v++) {
			const int16_t *row = f + v * 8;
			int e0 = 1448 * (row[0] + row[2]), e1 = 1448 * (row[0] - row[2]);
			int o0 = 1892 * row[1] + 784 * row[3], o1 = 784 * row[1] - 1892 * row[3];
			tmp[v][0] = (e0 + o0) >> 9;
			tmp[v][3] = (e0 - o0) >> 9;
			tmp[v][1] = (e1 + o1) >> 9;
			tmp[v][2] = (e1 - o1) >> 9;
		}
		for (int x = 0; x < 4; x++) {
			int e0 = 1448 * (tmp[0][x] + tmp[2][x]), e1 = 1448 * (tmp[0][x] - tmp[2][x]);
			int o0 = 1892 * tmp[1][x] + 784 * tmp[3][x], o1 = 784 * tmp[1][x] - 1892 * tmp[3][x];
			dst[x] = clamp8(((e0 + o0 + (1 << 14)) >> 15) + 128);
			dst[3 * stride + x] = clamp8(((e0 - o0 + (1 << 14)) >> 15) + 128);
			dst[stride + x] = clamp8(((e1 + o1 + (1 << 14)) >> 15) + 128);
			dst[2 * stride + x] = clamp8(((e1 - o1 + (1 << 14)) >> 15) + 128);
		}
		return;
	}
	const int16_t *t;
	switch (n) {
	case 1: t = &idct1[0][0]; break;
	case 4: t = &idct4[0][0]; break;
	default: t = &idct8[0][0]; break;
	}
	int tmp[8][8];
	for (int v = 0; v < rows; v++) { // horizontal pass over the rows of coefficients
		const int16_t *row = f + v * 8;
		int last = -1;
		for (int u = 0; u < n; u++) if (row[u]) last = u;
		for (int x = 0; x < n; x++) {
			int sum = 0;
			for (int u = 0; u <= last; u++) sum += t[x * n + u] * row[u];
			tmp[v][x] = sum >> 9;
		}
	}
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) {
			int sum = 0;
			for (int v = 0; v < rows; v++) sum += t[y * n + v] * tmp[v][x];
			dst[y * stride + x] = clamp8(((sum + (1 << 14)) >> 15) + 128);
		}
	}
}

// Skips to after the next marker and returns it, or -1.
static int next_marker(Dec *d) {
	int b;
	do {
		do { b = get_byte(d); } while (b >= 0 && b != 0xFF);
		if (b < 0) return -1;
		do { b = get_byte(d); } while (b == 0xFF);
		if (b < 0) return -1;
	} while (b == 0);
	return b;
}

// YCbCr to RGB (the same sums as before, looked up): the products of each chroma value with the constants. Made on
// first use, 4 KB.
static int16_t cr_to_r[256], cb_to_b[256];
static int32_t cb_to_g[256], cr_to_g[256];
static bool ycc_ready;

static void init_ycc_tables(void) {
	if (ycc_ready) return;
	for (int i = 0; i < 256; i++) {
		int c = i - 128;
		cr_to_r[i] = (int16_t)((91881 * c) >> 16);
		cb_to_b[i] = (int16_t)((116130 * c) >> 16);
		cb_to_g[i] = 22554 * c;
		cr_to_g[i] = 46802 * c;
	}
	ycc_ready = true;
}

typedef struct {
	int width, height, ncomp;
	Comp comp[4];
} Frame;

DecResult jpeg_decode(ImageReadFn read, void *read_user, const ImageInfo *info, int want_w, int want_h,
                      const ImageSink *sink) {
	if (info->kind != IMAGE_JPEG) return DEC_BAD;
	if (info->progressive) return DEC_UNSUPPORTED;
	if (info->components != 1 && info->components != 3) return DEC_UNSUPPORTED;

	Dec *d = calloc(1, sizeof *d);
	if (!d) return DEC_MEMORY;
	d->read = read;
	d->user = read_user;
	d->adobe_transform = -1;

	Frame fr;
	memset(&fr, 0, sizeof fr);
	DecResult result = DEC_BAD;
	uint8_t *rgb = NULL;
	uint16_t *xmap[3] = { NULL, NULL, NULL };
	int16_t blk[64];
	bool have_frame = false;

	if (get_u16(d) != 0xFFD8) goto done;
	for (;;) {
		int m = next_marker(d);
		if (m < 0) goto done;
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
		if (m == 0xD9) goto done;
		int len = get_u16(d);
		if (len < 2) goto done;
		len -= 2;
		if (m == 0xDB) { // quantisation tables
			while (len > 0) {
				int pq = get_byte(d);
				if (pq < 0) goto done;
				int prec = pq >> 4, id = pq & 15;
				len--;
				if (id > 3 || prec > 1) goto done;
				for (int i = 0; i < 64; i++) {
					int v = prec ? get_u16(d) : get_byte(d);
					if (v < 0) goto done;
					d->qt[id][zigzag[i]] = (uint16_t)v;
				}
				len -= prec ? 128 : 64;
				d->qt_present[id] = true;
			}
		} else if (m == 0xC4) { // Huffman tables
			while (len > 0) {
				int tc = get_byte(d);
				if (tc < 0) goto done;
				len--;
				int cls = tc >> 4, id = tc & 15;
				if (cls > 1 || id > 3) goto done;
				Huff *h = cls ? &d->ac[id] : &d->dc[id];
				memset(h, 0, sizeof *h);
				int total = 0;
				for (int i = 1; i <= 16; i++) {
					int b = get_byte(d);
					if (b < 0) goto done;
					h->bits[i] = (uint8_t)b;
					total += b;
				}
				if (total > 256) goto done;
				for (int i = 0; i < total; i++) {
					int b = get_byte(d);
					if (b < 0) goto done;
					h->vals[i] = (uint8_t)b;
				}
				len -= 16 + total;
				if (!build_huff(h)) goto done;
				if (cls) build_fast_ac(h, d->ac_fast[id]);
			}
		} else if (m == 0xDD) {
			d->restart_interval = get_u16(d);
			len -= 2;
		} else if (m == 0xEE && len >= 12) { // Adobe
			uint8_t a[12];
			for (int i = 0; i < 12; i++) a[i] = (uint8_t)get_byte(d);
			len -= 12;
			if (memcmp(a, "Adobe", 5) == 0) d->adobe_transform = a[11];
		} else if (m == 0xC0 || m == 0xC1) {
			int prec = get_byte(d);
			fr.height = get_u16(d);
			fr.width = get_u16(d);
			fr.ncomp = get_byte(d);
			len -= 6;
			if (prec != 8) { result = DEC_UNSUPPORTED; goto done; }
			if (fr.width <= 0 || fr.height <= 0 || (fr.ncomp != 1 && fr.ncomp != 3)) { result = DEC_UNSUPPORTED; goto done; }
			for (int i = 0; i < fr.ncomp; i++) {
				fr.comp[i].id = get_byte(d);
				int hv = get_byte(d);
				fr.comp[i].tq = get_byte(d);
				len -= 3;
				fr.comp[i].h = hv >> 4;
				fr.comp[i].v = hv & 15;
				if (fr.comp[i].h < 1 || fr.comp[i].h > 4 || fr.comp[i].v < 1 || fr.comp[i].v > 4 || fr.comp[i].tq > 3) {
					result = DEC_UNSUPPORTED;
					goto done;
				}
			}
			have_frame = true;
		} else if ((m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) {
			result = DEC_UNSUPPORTED; // progressive, lossless or arithmetic coded
			goto done;
		} else if (m == 0xDA) {
			if (!have_frame) goto done;
			int ns = get_byte(d);
			len--;
			if (ns != fr.ncomp) { result = DEC_UNSUPPORTED; goto done; } // several scans
			for (int i = 0; i < ns; i++) {
				int cid = get_byte(d), tt = get_byte(d);
				len -= 2;
				int k = -1;
				for (int j = 0; j < fr.ncomp; j++) if (fr.comp[j].id == cid) k = j;
				if (k < 0) goto done;
				fr.comp[k].dc_table = (tt >> 4) & 3;
				fr.comp[k].ac_table = tt & 3;
			}
			while (len-- > 0) get_byte(d);
			break;
		}
		while (len-- > 0) if (get_byte(d) < 0) goto done;
	}

	for (int i = 0; i < fr.ncomp; i++) {
		Comp *c = &fr.comp[i];
		if (!d->qt_present[c->tq] || !d->dc[c->dc_table].present || !d->ac[c->ac_table].present) goto done;
	}

	// Decoded size: the smallest of 1/8, 2/8, 4/8 and 8/8 that keeps what the screen can show.
	int n = 8;
	for (int cand = 1; cand <= 4; cand *= 2) {
		if ((fr.width * cand + 7) / 8 >= want_w && (fr.height * cand + 7) / 8 >= want_h) {
			n = cand;
			break;
		}
	}
	int ow = (fr.width * n + 7) / 8, oh = (fr.height * n + 7) / 8;

	int hmax = 1, vmax = 1;
	for (int i = 0; i < fr.ncomp; i++) {
		if (fr.comp[i].h > hmax) hmax = fr.comp[i].h;
		if (fr.comp[i].v > vmax) vmax = fr.comp[i].v;
	}
	if (fr.ncomp == 1) { // a single component is not subsampled
		fr.comp[0].h = hmax = 1;
		fr.comp[0].v = vmax = 1;
	}
	int mcus_x = (fr.width + 8 * hmax - 1) / (8 * hmax);
	int mcus_y = (fr.height + 8 * vmax - 1) / (8 * vmax);

	for (int i = 0; i < fr.ncomp; i++) {
		Comp *c = &fr.comp[i];
		c->plane_w = mcus_x * c->h * n;
		c->plane_h = c->v * n;
		c->plane = malloc((size_t)c->plane_w * (size_t)c->plane_h);
		if (!c->plane) { result = DEC_MEMORY; goto done; }
	}
	rgb = malloc((size_t)ow * 3);
	if (!rgb) { result = DEC_MEMORY; goto done; }
	// The column of each component that belongs to each output column (the chroma of a subsampled picture is
	// shared), worked out once instead of for every pixel.
	for (int i = 0; i < fr.ncomp && fr.ncomp == 3; i++) {
		xmap[i] = malloc((size_t)ow * sizeof *xmap[i]);
		if (!xmap[i]) { result = DEC_MEMORY; goto done; }
		for (int x = 0; x < ow; x++) xmap[i][x] = (uint16_t)(x * fr.comp[i].h / hmax);
	}
	if (fr.ncomp == 3) init_ycc_tables();

	if (!sink->begin(sink->user, ow, oh)) { result = DEC_STOPPED; goto done; }

	int restart_count = 0, next_rst = 0;
	int out_y = 0;
	for (int my = 0; my < mcus_y; my++) {
		for (int mx = 0; mx < mcus_x; mx++) {
			if (d->restart_interval && restart_count == d->restart_interval) {
				// Byte align, find the RSTn marker, start the predictions again.
				d->bitbuf = 0;
				d->bitcnt = 0;
				if (!d->marker) d->marker = next_marker(d);
				if (d->marker < 0xD0 || d->marker > 0xD7) goto done;
				d->marker = 0;
				next_rst = (next_rst + 1) & 7;
				for (int i = 0; i < fr.ncomp; i++) fr.comp[i].pred = 0;
				restart_count = 0;
			}
			restart_count++;
			for (int i = 0; i < fr.ncomp; i++) {
				Comp *c = &fr.comp[i];
				for (int by = 0; by < c->v; by++) {
					for (int bx = 0; bx < c->h; bx++) {
						int rows;
						bool only_dc;
						if (!decode_block(d, c, n, blk, &rows, &only_dc)) goto done;
						idct(blk, n, c->plane + (by * n) * c->plane_w + (mx * c->h + bx) * n, c->plane_w, rows, only_dc);
					}
				}
			}
		}
		(void)next_rst;

		// Colour convert the rows of this MCU row.
		for (int ry = 0; ry < vmax * n && out_y < oh; ry++, out_y++) {
			const uint8_t *src[3];
			for (int i = 0; i < fr.ncomp; i++) {
				const Comp *c = &fr.comp[i];
				src[i] = c->plane + (ry * c->v / vmax) * c->plane_w;
			}
			if (fr.ncomp == 1) {
				for (int x = 0; x < ow; x++) rgb[x * 3] = rgb[x * 3 + 1] = rgb[x * 3 + 2] = src[0][x];
			} else if (d->adobe_transform == 0) {
				for (int x = 0; x < ow; x++) {
					uint8_t *o = rgb + x * 3;
					o[0] = src[0][xmap[0][x]];
					o[1] = src[1][xmap[1][x]];
					o[2] = src[2][xmap[2][x]];
				}
			} else {
				for (int x = 0; x < ow; x++) {
					int Y = src[0][xmap[0][x]], cb = src[1][xmap[1][x]], cr = src[2][xmap[2][x]];
					uint8_t *o = rgb + x * 3;
					o[0] = clamp8(Y + cr_to_r[cr]);
					o[1] = clamp8(Y - ((cb_to_g[cb] + cr_to_g[cr]) >> 16));
					o[2] = clamp8(Y + cb_to_b[cb]);
				}
			}
			if (!sink->row(sink->user, out_y, rgb)) { result = DEC_STOPPED; goto done; }
		}
	}
	result = DEC_OK;

done:
	for (int i = 0; i < 4; i++) free(fr.comp[i].plane);
	free(rgb);
	for (int i = 0; i < 3; i++) free(xmap[i]);
	free(d);
	return result;
}
