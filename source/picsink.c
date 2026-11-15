#include "picsink.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t bayer[4][4] = {
	{ 0, 8, 2, 10 },
	{ 12, 4, 14, 6 },
	{ 3, 11, 1, 9 },
	{ 15, 7, 13, 5 },
};

#define RGB15_(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

void picsink_palette(uint16_t out[PIC_PAL_COUNT]) {
	for (int i = 0; i < PIC_GREYS; i++) out[i] = RGB15_(i, i, i);
	for (int r = 0; r < 5; r++) {
		for (int g = 0; g < 5; g++) {
			for (int b = 0; b < 5; b++) {
				out[PIC_GREYS + (r * 5 + g) * 5 + b] = RGB15_(r * 31 / 4, g * 31 / 4, b * 31 / 4);
			}
		}
	}
}

int picsink_index(int r, int g, int b, int x, int y) {
	int t = bayer[y & 3][x & 3];
	int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
	int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
	if (hi - lo <= 12) {
		int v = (r + g + b) / 3;
		int idx = (v * 31 * 16 / 255 + t) >> 4;
		return idx > 31 ? 31 : idx;
	}
	int qr = (r * 4 * 16 / 255 + t) >> 4;
	int qg = (g * 4 * 16 / 255 + t) >> 4;
	int qb = (b * 4 * 16 / 255 + t) >> 4;
	if (qr > 4) qr = 4;
	if (qg > 4) qg = 4;
	if (qb > 4) qb = 4;
	return PIC_GREYS + (qr * 5 + qg) * 5 + qb;
}

// a / n by multiplying with inv = ceil(2^32 / n); the product can be one too big, which the check puts right. That
// holds while a * n < 2^32, and a is at most 255 n: so for n up to 4000 (more than that is not met in a picture
// that is shrunk to a screen, and is divided).
static uint32_t div_by(uint32_t a, uint32_t n, uint64_t inv) {
	if (n > 4000) return a / n;
	uint32_t q = (uint32_t)(((uint64_t)a * inv) >> 32);
	if ((uint64_t)q * n > a) q--;
	return q;
}

// 2^32 / n rounded up, for div_by. A division of 64 bits is slow on the ARM9, and the number of pixels in a sum is
// one of four at most (two numbers of columns times two numbers of rows) that take turns from one pixel to the
// next when the picture is not shrunk by a whole number: the last four are kept (decision 114).
static uint64_t reciprocal(PicSink *ps, uint32_t n) {
	for (int i = 0; i < 4; i++) {
		if (ps->inv_n[i] == n) return ps->inv[i];
	}
	int slot = ps->inv_next;
	ps->inv_next = (slot + 1) & 3;
	ps->inv_n[slot] = n;
	ps->inv[slot] = 0xFFFFFFFFull / n + 1;
	return ps->inv[slot];
}

static bool flush(PicSink *ps) {
	if (ps->cur < 0) return true;
	// The averages are sums divided by the number of pixels summed: the division is a multiplication with the
	// reciprocal of that number (the ARM9 has no divide instruction, decision 112). The result is exactly the quotient.
	uint32_t last_n = 0;
	uint64_t inv = 0;
	const uint32_t *acc = ps->acc;
	for (int x = 0; x < ps->dst_w; x++, acc += 3) {
		uint32_t n = (uint32_t)ps->col_count[x] * (uint32_t)ps->rows_in;
		if (n == 0) n = 1;
		if (n != last_n) {
			last_n = n;
			inv = reciprocal(ps, n);
		}
		int r = (int)div_by(acc[0], n, inv), g = (int)div_by(acc[1], n, inv), b = (int)div_by(acc[2], n, inv);
		ps->line[x] = (uint8_t)(PIC_PAL_BASE + picsink_index(r, g, b, x, ps->cur));
	}
	int y = ps->cur;
	ps->cur = -1;
	ps->rows_in = 0;
	memset(ps->acc, 0, (size_t)ps->dst_w * 3 * sizeof *ps->acc);
	return ps->put_row(ps->user, y, ps->dst_w, ps->line);
}

static bool sink_begin(void *user, int w, int h) {
	PicSink *ps = user;
	ps->src_w = w;
	ps->src_h = h;
	// Fit into the area without enlarging.
	int dw = w, dh = h;
	if (dw > ps->area_w || dh > ps->area_h) {
		if ((int64_t)w * ps->area_h >= (int64_t)h * ps->area_w) {
			dh = (int)((int64_t)h * ps->area_w / w);
			dw = ps->area_w;
		} else {
			dw = (int)((int64_t)w * ps->area_h / h);
			dh = ps->area_h;
		}
	}
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	ps->dst_w = dw;
	ps->dst_h = dh;

	ps->acc = calloc((size_t)dw * 3, sizeof *ps->acc);
	ps->col_count = calloc((size_t)dw, sizeof *ps->col_count);
	ps->line = malloc((size_t)dw);
	if (!ps->acc || !ps->col_count || !ps->line) return false;
	// Picture column x goes to output column x * dw / w (rounded down): the columns of one output column follow each
	// other, so their number is all that is kept. Counted without a division for each column.
	for (int x = 0, d = 0, rem = 0; x < w; x++) {
		ps->col_count[d]++;
		rem += dw;
		while (rem >= w) {
			rem -= w;
			d++;
		}
	}
	ps->cur = -1;
	ps->last_y = -2;
	if (ps->put_size) ps->put_size(ps->user, dw, dh);
	return true;
}

static bool sink_row(void *user, int y, const uint8_t *rgb) {
	PicSink *ps = user;
	// The output row is y * dst_h / src_h. The rows come one after the other, so it follows from the row before.
	int dy;
	if (y == ps->last_y + 1 && ps->cur >= 0) {
		dy = ps->cur;
		ps->last_rem += ps->dst_h;
		while (ps->last_rem >= ps->src_h) {
			ps->last_rem -= ps->src_h;
			dy++;
		}
	} else {
		int64_t at = (int64_t)y * ps->dst_h;
		dy = (int)(at / ps->src_h);
		ps->last_rem = (int)(at % ps->src_h);
	}
	ps->last_y = y;
	if (ps->cur >= 0 && dy != ps->cur) {
		if (!flush(ps)) {
			ps->stopped = 1;
			return false;
		}
	}
	ps->cur = dy;
	ps->rows_in++;
	// The picture columns of each output column are summed in one go, and stored once (decision 114).
	uint32_t *a = ps->acc;
	const uint16_t *count = ps->col_count;
	for (int d = 0; d < ps->dst_w; d++, a += 3) {
		uint32_t r = a[0], g = a[1], b = a[2];
		for (int c = count[d]; c > 0; c--, rgb += 3) {
			r += rgb[0];
			g += rgb[1];
			b += rgb[2];
		}
		a[0] = r;
		a[1] = g;
		a[2] = b;
	}
	return true;
}

void picsink_init(PicSink *ps, int area_w, int area_h, PicRowFn put_row, PicSizeFn put_size, void *user,
                  ImageSink *out) {
	memset(ps, 0, sizeof *ps);
	ps->area_w = area_w;
	ps->area_h = area_h;
	ps->put_row = put_row;
	ps->put_size = put_size;
	ps->user = user;
	ps->cur = -1;
	out->user = ps;
	out->begin = sink_begin;
	out->row = sink_row;
}

bool picsink_finish(PicSink *ps) {
	if (ps->stopped || !ps->acc) return false;
	return flush(ps);
}

void picsink_close(PicSink *ps) {
	free(ps->acc);
	free(ps->col_count);
	free(ps->line);
	ps->acc = NULL;
	ps->col_count = NULL;
	ps->line = NULL;
}
