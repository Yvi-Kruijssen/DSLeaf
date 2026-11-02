// Baseline JPEG decoder (docs: decision 075): 8 bit, Huffman coded, one interleaved scan, any
// sampling up to 4x4, restart markers. Progressive, arithmetic coded and CMYK files are refused
// (DEC_UNSUPPORTED). When the screen could not show all the detail the picture is decoded at 1/2,
// 1/4 or 1/8 of its size by using only the low frequencies of each block, which is much faster.
#include <stdlib.h>
#include <string.h>

#include "imagedec.h"

// IDCT tables for the reduced sizes: table[x][u] = a(u) * sqrt(N / 8) * cos((2x + 1) u pi / (2N)) * 4096
static const int16_t idct1[1][1] = {
	{ 1448 },
};
static const int16_t idct2[2][2] = {
	{ 1448, 1448 },
	{ 1448, -1448 },
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
	uint8_t buf[512];
	size_t pos, len;
	bool eof;

	uint32_t bitbuf;
	int bitcnt;
	int marker; // a marker met inside the data, 0 for none

	uint16_t qt[4][64]; // natural order
	bool qt_present[4];
	Huff dc[4], ac[4];
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

static void fill_bits(Dec *d) {
	while (d->bitcnt <= 24) {
		int b = 0;
		if (!d->marker) {
			b = get_byte(d);
			if (b < 0) {
				b = 0;
				d->marker = 0xD9; // the file ends: feed zeros
			} else if (b == 0xFF) {
				int n = get_byte(d);
				while (n == 0xFF) n = get_byte(d);
				if (n == 0) {
					b = 0xFF;
				} else {
					d->marker = n < 0 ? 0xD9 : n;
					b = 0;
				}
			}
		}
		d->bitbuf |= (uint32_t)b << (24 - d->bitcnt);
		d->bitcnt += 8;
	}
}

static int get_bits(Dec *d, int n) { // n 1..16
	if (d->bitcnt < n) fill_bits(d);
	int v = (int)(d->bitbuf >> (32 - n));
	d->bitbuf <<= n;
	d->bitcnt -= n;
	return v;
}

static int receive_extend(Dec *d, int s) {
	if (s == 0) return 0;
	int v = get_bits(d, s);
	return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
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

static int decode_huff(Dec *d, const Huff *h) {
	if (d->bitcnt < 16) fill_bits(d);
	int look = (int)(d->bitbuf >> 23);
	int e = h->look[look];
	if (e) {
		int l = e >> 8;
		d->bitbuf <<= l;
		d->bitcnt -= l;
		return e & 0xFF;
	}
	int code = (int)(d->bitbuf >> 22); // 10 bits
	int l = 10;
	while (l <= 16 && (h->maxcode[l] < 0 || code > h->maxcode[l])) {
		l++;
		code = (int)(d->bitbuf >> (32 - l));
	}
	if (l > 16) return -1;
	d->bitbuf <<= l;
	d->bitcnt -= l;
	return h->vals[h->valptr[l] + code - h->mincode[l]];
}

// Decodes one block into `out` (natural order, dequantised, only the coefficients below n x n are kept).
// Returns false if the data is bad.
static bool decode_block(Dec *d, Comp *c, int n, int16_t out[64]) {
	memset(out, 0, 64 * sizeof(int16_t));
	const Huff *dc = &d->dc[c->dc_table], *ac = &d->ac[c->ac_table];
	const uint16_t *q = d->qt[c->tq];
	int t = decode_huff(d, dc);
	if (t < 0 || t > 15) return false;
	c->pred += receive_extend(d, t);
	int v = c->pred * q[0];
	out[0] = (int16_t)(v > 2047 ? 2047 : (v < -2047 ? -2047 : v));
	for (int k = 1; k < 64;) {
		int rs = decode_huff(d, ac);
		if (rs < 0) return false;
		int r = rs >> 4, s = rs & 15;
		if (s == 0) {
			if (r != 15) break;
			k += 16;
			continue;
		}
		k += r;
		if (k > 63) return false;
		int coef = receive_extend(d, s);
		int pos = zigzag[k];
		if ((pos & 7) < n && (pos >> 3) < n) {
			int val = coef * q[pos];
			out[pos] = (int16_t)(val > 2047 ? 2047 : (val < -2047 ? -2047 : val));
		}
		k++;
	}
	return true;
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Inverse DCT of the n x n low frequencies, written as n x n pixels at `dst` with row distance `stride`.
static void idct(const int16_t *f, int n, uint8_t *dst, int stride) {
	const int16_t *t;
	switch (n) {
	case 1: t = &idct1[0][0]; break;
	case 2: t = &idct2[0][0]; break;
	case 4: t = &idct4[0][0]; break;
	default: t = &idct8[0][0]; break;
	}
	int tmp[8][8];
	for (int v = 0; v < n; v++) { // horizontal pass over the rows of coefficients
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
			for (int v = 0; v < n; v++) sum += t[y * n + v] * tmp[v][x];
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
						if (!decode_block(d, c, n, blk)) goto done;
						idct(blk, n, c->plane + (by * n) * c->plane_w + (mx * c->h + bx) * n, c->plane_w);
					}
				}
			}
		}
		(void)next_rst;

		// Colour convert the rows of this MCU row.
		for (int ry = 0; ry < vmax * n && out_y < oh; ry++, out_y++) {
			for (int x = 0; x < ow; x++) {
				int yv[3];
				for (int i = 0; i < fr.ncomp; i++) {
					const Comp *c = &fr.comp[i];
					int sx = x * c->h / hmax, sy = ry * c->v / vmax;
					yv[i] = c->plane[sy * c->plane_w + sx];
				}
				uint8_t *o = rgb + x * 3;
				if (fr.ncomp == 1) {
					o[0] = o[1] = o[2] = (uint8_t)yv[0];
				} else if (d->adobe_transform == 0) {
					o[0] = (uint8_t)yv[0]; o[1] = (uint8_t)yv[1]; o[2] = (uint8_t)yv[2];
				} else {
					int Y = yv[0], cb = yv[1] - 128, cr = yv[2] - 128;
					o[0] = clamp8(Y + ((91881 * cr) >> 16));
					o[1] = clamp8(Y - ((22554 * cb + 46802 * cr) >> 16));
					o[2] = clamp8(Y + ((116130 * cb) >> 16));
				}
			}
			if (!sink->row(sink->user, out_y, rgb)) { result = DEC_STOPPED; goto done; }
		}
	}
	result = DEC_OK;

done:
	for (int i = 0; i < 4; i++) free(fr.comp[i].plane);
	free(rgb);
	free(d);
	return result;
}
