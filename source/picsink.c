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

static bool flush(PicSink *ps) {
	if (ps->cur < 0) return true;
	for (int x = 0; x < ps->dst_w; x++) {
		uint32_t n = (uint32_t)ps->col_count[x] * (uint32_t)ps->rows_in;
		if (n == 0) n = 1;
		int r = (int)(ps->acc[x * 3] / n), g = (int)(ps->acc[x * 3 + 1] / n), b = (int)(ps->acc[x * 3 + 2] / n);
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
	ps->col = malloc((size_t)w * sizeof *ps->col);
	ps->col_count = calloc((size_t)dw, sizeof *ps->col_count);
	ps->line = malloc((size_t)dw);
	if (!ps->acc || !ps->col || !ps->col_count || !ps->line) return false;
	for (int x = 0; x < w; x++) {
		int d = (int)((int64_t)x * dw / w);
		ps->col[x] = (uint16_t)d;
		ps->col_count[d]++;
	}
	ps->cur = -1;
	if (ps->put_size) ps->put_size(ps->user, dw, dh);
	return true;
}

static bool sink_row(void *user, int y, const uint8_t *rgb) {
	PicSink *ps = user;
	int dy = (int)((int64_t)y * ps->dst_h / ps->src_h);
	if (ps->cur >= 0 && dy != ps->cur) {
		if (!flush(ps)) {
			ps->stopped = 1;
			return false;
		}
	}
	ps->cur = dy;
	ps->rows_in++;
	for (int x = 0; x < ps->src_w; x++) {
		uint32_t *a = &ps->acc[ps->col[x] * 3];
		a[0] += rgb[x * 3];
		a[1] += rgb[x * 3 + 1];
		a[2] += rgb[x * 3 + 2];
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
	free(ps->col);
	free(ps->col_count);
	free(ps->line);
	ps->acc = NULL;
	ps->col = NULL;
	ps->col_count = NULL;
	ps->line = NULL;
}
