// PNG decoder: all colour types and bit depths except 16 bit palette-less oddities are
// handled; 16 bit samples keep their high byte. Adam7 interlaced files are decoded too.
// Transparency is blended onto white. Checksums are not checked.
#include <stdlib.h>
#include <string.h>

#include "imagedec.h"
#include "inflate.h"

typedef struct {
	ImageReadFn read;
	void *user;
	uint8_t chunk_hdr[8];
	uint32_t chunk_left;   // data bytes left in the current IDAT chunk
	bool idat_done;
	bool failed;
	uint8_t buf[512];
	size_t buf_pos, buf_len;
	bool eof;
	bool zlib_header_done;
} Source;

static int src_byte(Source *s) {
	if (s->buf_pos == s->buf_len) {
		if (s->eof) return -1;
		s->buf_len = s->read(s->user, s->buf, sizeof s->buf);
		s->buf_pos = 0;
		if (s->buf_len == 0) {
			s->eof = true;
			return -1;
		}
	}
	return s->buf[s->buf_pos++];
}

static bool src_skip(Source *s, uint32_t n) {
	while (n--) if (src_byte(s) < 0) return false;
	return true;
}

static bool src_u32(Source *s, uint32_t *out) {
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		int b = src_byte(s);
		if (b < 0) return false;
		v = (v << 8) | (uint32_t)b;
	}
	*out = v;
	return true;
}

// Moves to the data of the next IDAT chunk, skipping other chunks. False at IEND or the end of the file.
static bool next_idat(Source *s) {
	for (;;) {
		uint32_t len;
		if (!src_u32(s, &len)) return false;
		uint8_t type[4];
		for (int i = 0; i < 4; i++) {
			int b = src_byte(s);
			if (b < 0) return false;
			type[i] = (uint8_t)b;
		}
		if (memcmp(type, "IDAT", 4) == 0) {
			s->chunk_left = len;
			return true;
		}
		if (memcmp(type, "IEND", 4) == 0) return false;
		if (!src_skip(s, len + 4)) return false; // data and CRC
		(void)0;
	}
}

// The compressed data: the IDAT chunks one after the other, without the two byte zlib header.
static size_t idat_read(void *user, uint8_t *out, size_t cap) {
	Source *s = user;
	size_t n = 0;
	while (n < cap && !s->idat_done) {
		if (s->chunk_left == 0) {
			if (!s->zlib_header_done) { // first chunk already positioned by the caller
				s->idat_done = true;
				break;
			}
			if (!src_skip(s, 4) || !next_idat(s)) { // the CRC of the chunk that ended
				s->idat_done = true;
				break;
			}
			continue;
		}
		int b = src_byte(s);
		if (b < 0) {
			s->idat_done = true;
			s->failed = true;
			break;
		}
		s->chunk_left--;
		out[n++] = (uint8_t)b;
	}
	return n;
}

static int paeth(int a, int b, int c) {
	int p = a + b - c;
	int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	if (pa <= pb && pa <= pc) return a;
	if (pb <= pc) return b;
	return c;
}

typedef struct {
	int width, height, depth, ctype;
	int channels, bpp; // bytes per pixel for the filters (at least 1)
	uint8_t palette[256][3];
	uint8_t trans[256];  // alpha of palette entries
	int palette_count;
	bool have_trns_grey;
	int trns_grey;
} Header;

// Turns one unfiltered row of `w` pixels into RGB.
static void to_rgb(const Header *h, const uint8_t *raw, int w, uint8_t *rgb) {
	int depth = h->depth;
	for (int x = 0; x < w; x++) {
		int r, g, b, a = 255;
		switch (h->ctype) {
		case 0: { // grey
			int v;
			if (depth == 16) v = raw[x * 2];
			else if (depth == 8) v = raw[x];
			else {
				int per = 8 / depth, shift = (per - 1 - (x % per)) * depth;
				int mask = (1 << depth) - 1;
				v = (raw[x / per] >> shift) & mask;
				if (h->have_trns_grey && v == h->trns_grey) a = 0;
				v = v * 255 / mask;
				r = g = b = v;
				break;
			}
			r = g = b = v;
			break;
		}
		case 2:
			if (depth == 16) { r = raw[x * 6]; g = raw[x * 6 + 2]; b = raw[x * 6 + 4]; }
			else { r = raw[x * 3]; g = raw[x * 3 + 1]; b = raw[x * 3 + 2]; }
			break;
		case 3: {
			int per = 8 / depth, shift = (per - 1 - (x % per)) * depth;
			int idx = depth == 8 ? raw[x] : (raw[x / per] >> shift) & ((1 << depth) - 1);
			if (idx >= h->palette_count) idx = 0;
			r = h->palette[idx][0]; g = h->palette[idx][1]; b = h->palette[idx][2];
			a = h->trans[idx];
			break;
		}
		case 4: {
			int step = depth == 16 ? 4 : 2;
			r = g = b = raw[x * step];
			a = raw[x * step + (depth == 16 ? 2 : 1)];
			break;
		}
		default: { // 6
			int step = depth == 16 ? 8 : 4;
			int o = depth == 16 ? 2 : 1;
			r = raw[x * step]; g = raw[x * step + o]; b = raw[x * step + 2 * o];
			a = raw[x * step + 3 * o];
			break;
		}
		}
		if (a != 255) { // onto white
			r = (r * a + 255 * (255 - a)) / 255;
			g = (g * a + 255 * (255 - a)) / 255;
			b = (b * a + 255 * (255 - a)) / 255;
		}
		rgb[x * 3] = (uint8_t)r;
		rgb[x * 3 + 1] = (uint8_t)g;
		rgb[x * 3 + 2] = (uint8_t)b;
	}
}

static int row_bytes(const Header *h, int w) {
	return (w * h->channels * h->depth + 7) / 8;
}

// Reads exactly `n` bytes from the inflater. False if the data ends early.
static bool inflate_exact(Inflate *z, uint8_t *out, size_t n) {
	while (n > 0) {
		int got = inflate_read(z, out, n);
		if (got <= 0) return false;
		out += got;
		n -= (size_t)got;
	}
	return true;
}

// Undoes the filter of one row. `prev` is the unfiltered row before it (zeros for the first).
static bool unfilter(uint8_t *row, const uint8_t *prev, int bytes, int bpp, int filter) {
	switch (filter) {
	case 0: break;
	case 1: for (int i = bpp; i < bytes; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]); break;
	case 2: for (int i = 0; i < bytes; i++) row[i] = (uint8_t)(row[i] + prev[i]); break;
	case 3:
		for (int i = 0; i < bytes; i++) {
			int left = i >= bpp ? row[i - bpp] : 0;
			row[i] = (uint8_t)(row[i] + ((left + prev[i]) >> 1));
		}
		break;
	case 4:
		for (int i = 0; i < bytes; i++) {
			int left = i >= bpp ? row[i - bpp] : 0;
			int ul = i >= bpp ? prev[i - bpp] : 0;
			row[i] = (uint8_t)(row[i] + paeth(left, prev[i], ul));
		}
		break;
	default: return false;
	}
	return true;
}

static const uint8_t adam_x0[7] = { 0, 4, 0, 2, 0, 1, 0 };
static const uint8_t adam_y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
static const uint8_t adam_dx[7] = { 8, 8, 4, 4, 2, 2, 1 };
static const uint8_t adam_dy[7] = { 8, 8, 8, 4, 4, 2, 2 };

DecResult png_decode(ImageReadFn read, void *read_user, const ImageInfo *info, const ImageSink *sink) {
	if (info->kind != IMAGE_PNG) return DEC_BAD;
	Header h;
	memset(&h, 0, sizeof h);
	h.width = (int)info->width;
	h.height = (int)info->height;
	h.depth = info->bit_depth;
	h.ctype = info->color_type;
	if (h.width <= 0 || h.height <= 0 || h.width > 16384 || h.height > 16384) return DEC_UNSUPPORTED;
	switch (h.ctype) {
	case 0: h.channels = 1; break;
	case 2: h.channels = 3; break;
	case 3: h.channels = 1; break;
	case 4: h.channels = 2; break;
	case 6: h.channels = 4; break;
	default: return DEC_UNSUPPORTED;
	}
	if (h.depth != 1 && h.depth != 2 && h.depth != 4 && h.depth != 8 && h.depth != 16) return DEC_UNSUPPORTED;
	h.bpp = (h.channels * h.depth + 7) / 8;
	memset(h.trans, 255, sizeof h.trans);

	Source *s = calloc(1, sizeof *s);
	if (!s) return DEC_MEMORY;
	s->read = read;
	s->user = read_user;

	// Skip the signature and IHDR, and find the palette and transparency before the data.
	DecResult result = DEC_BAD;
	Inflate *z = NULL;
	uint8_t *raw = NULL, *prev = NULL, *rgb = NULL, *full = NULL;
	if (!src_skip(s, 8)) goto done;
	for (;;) {
		uint32_t len;
		if (!src_u32(s, &len)) goto done;
		uint8_t type[4];
		for (int i = 0; i < 4; i++) {
			int b = src_byte(s);
			if (b < 0) goto done;
			type[i] = (uint8_t)b;
		}
		if (memcmp(type, "IDAT", 4) == 0) {
			s->chunk_left = len;
			break;
		}
		if (memcmp(type, "IEND", 4) == 0) goto done;
		if (memcmp(type, "PLTE", 4) == 0 && len % 3 == 0 && len <= 768) {
			h.palette_count = (int)(len / 3);
			for (uint32_t i = 0; i < len; i++) {
				int b = src_byte(s);
				if (b < 0) goto done;
				h.palette[i / 3][i % 3] = (uint8_t)b;
			}
			if (!src_skip(s, 4)) goto done;
		} else if (memcmp(type, "tRNS", 4) == 0 && h.ctype == 3 && len <= 256) {
			for (uint32_t i = 0; i < len; i++) {
				int b = src_byte(s);
				if (b < 0) goto done;
				h.trans[i] = (uint8_t)b;
			}
			if (!src_skip(s, 4)) goto done;
		} else if (memcmp(type, "tRNS", 4) == 0 && h.ctype == 0 && len == 2) {
			int hi = src_byte(s), lo = src_byte(s);
			if (hi < 0 || lo < 0) goto done;
			h.have_trns_grey = h.depth < 16;
			h.trns_grey = (hi << 8) | lo;
			if (!src_skip(s, 4)) goto done;
		} else if (!src_skip(s, len + 4)) {
			goto done;
		}
	}
	if (h.ctype == 3 && h.palette_count == 0) goto done;

	// The two byte zlib header comes first in the compressed data.
	if (s->chunk_left < 2) goto done;
	if (src_byte(s) < 0 || src_byte(s) < 0) goto done;
	s->chunk_left -= 2;
	s->zlib_header_done = true;

	z = malloc(sizeof *z);
	int max_row = row_bytes(&h, h.width) + 1;
	raw = malloc((size_t)max_row);
	prev = calloc(1, (size_t)max_row);
	rgb = malloc((size_t)h.width * 3);
	if (!z || !raw || !prev || !rgb) {
		result = DEC_MEMORY;
		goto done;
	}
	inflate_init(z, idat_read, s);

	bool interlaced = info->progressive;
	if (!sink->begin(sink->user, h.width, h.height)) {
		result = DEC_STOPPED;
		goto done;
	}

	if (!interlaced) {
		int bytes = row_bytes(&h, h.width);
		for (int y = 0; y < h.height; y++) {
			if (!inflate_exact(z, raw, (size_t)bytes + 1)) goto done;
			if (!unfilter(raw + 1, prev + 1, bytes, h.bpp, raw[0])) goto done;
			to_rgb(&h, raw + 1, h.width, rgb);
			if (!sink->row(sink->user, y, rgb)) {
				result = DEC_STOPPED;
				goto done;
			}
			uint8_t *t = prev; prev = raw; raw = t;
		}
		result = DEC_OK;
		goto done;
	}

	// Adam7: the passes are decoded into a whole RGB picture, then delivered. Only for
	// pictures small enough to hold.
	if ((size_t)h.width * (size_t)h.height * 3 > 600 * 1024) {
		result = DEC_UNSUPPORTED;
		goto done;
	}
	full = malloc((size_t)h.width * (size_t)h.height * 3);
	if (!full) {
		result = DEC_MEMORY;
		goto done;
	}
	for (int pass = 0; pass < 7; pass++) {
		int pw = (h.width - adam_x0[pass] + adam_dx[pass] - 1) / adam_dx[pass];
		int ph = (h.height - adam_y0[pass] + adam_dy[pass] - 1) / adam_dy[pass];
		if (pw <= 0 || ph <= 0) continue;
		int bytes = row_bytes(&h, pw);
		memset(prev, 0, (size_t)max_row);
		for (int py = 0; py < ph; py++) {
			if (!inflate_exact(z, raw, (size_t)bytes + 1)) goto done;
			if (!unfilter(raw + 1, prev + 1, bytes, h.bpp, raw[0])) goto done;
			to_rgb(&h, raw + 1, pw, rgb);
			int y = adam_y0[pass] + py * adam_dy[pass];
			for (int px = 0; px < pw; px++) {
				int x = adam_x0[pass] + px * adam_dx[pass];
				memcpy(full + ((size_t)y * (size_t)h.width + (size_t)x) * 3, rgb + px * 3, 3);
			}
			uint8_t *t = prev; prev = raw; raw = t;
		}
	}
	for (int y = 0; y < h.height; y++) {
		if (!sink->row(sink->user, y, full + (size_t)y * (size_t)h.width * 3)) {
			result = DEC_STOPPED;
			goto done;
		}
	}
	result = DEC_OK;

done:
	free(full);
	free(rgb);
	free(raw);
	free(prev);
	free(z);
	free(s);
	return result;
}
