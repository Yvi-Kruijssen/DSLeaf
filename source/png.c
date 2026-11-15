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
	uint8_t buf[2048]; // (512 before: a stored picture is read from the card in pieces of this size)
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
		if (s->buf_pos == s->buf_len) { // fill the buffer (or find the end of the file)
			int b = src_byte(s);
			if (b < 0) {
				s->idat_done = true;
				s->failed = true;
				break;
			}
			s->buf_pos--;
		}
		// As much of the chunk as is in the buffer, in one go (a byte at a time before, decision 114).
		size_t take = s->buf_len - s->buf_pos;
		if (take > s->chunk_left) take = s->chunk_left;
		if (take > cap - n) take = cap - n;
		memcpy(out + n, s->buf + s->buf_pos, take);
		s->buf_pos += take;
		s->chunk_left -= (uint32_t)take;
		n += take;
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

// A pixel with transparency `a` over white.
static inline uint8_t on_white(int v, int a) {
	return (uint8_t)((v * a + 255 * (255 - a)) / 255);
}

// Turns one unfiltered row of `w` pixels into RGB. One loop for each kind of picture, so that nothing about the kind is
// worked out again for every pixel (a palette picture of 1 to 4 bits took three divisions a pixel, decision 114).
static void to_rgb(const Header *h, const uint8_t *raw, int w, uint8_t *rgb) {
	int depth = h->depth;
	// For samples smaller than a byte: 8 / depth of them in a byte, the first one in the high bits.
	int per_shift = depth == 1 ? 3 : (depth == 2 ? 2 : (depth == 4 ? 1 : 0));
	int per_mask = (1 << per_shift) - 1, mask = (1 << depth) - 1;

	switch (h->ctype) {
	case 0: // grey
		if (depth >= 8) {
			int step = depth == 16 ? 2 : 1;
			for (int x = 0; x < w; x++, rgb += 3) rgb[0] = rgb[1] = rgb[2] = raw[x * step];
		} else {
			int scale = 255 / mask; // 255, 85 or 17: exact
			for (int x = 0; x < w; x++, rgb += 3) {
				int v = (raw[x >> per_shift] >> ((per_mask - (x & per_mask)) * depth)) & mask;
				bool clear = h->have_trns_grey && v == h->trns_grey;
				rgb[0] = rgb[1] = rgb[2] = clear ? 255 : (uint8_t)(v * scale);
			}
		}
		break;
	case 2: // colour
		if (depth == 16) {
			for (int x = 0; x < w; x++, rgb += 3, raw += 6) {
				rgb[0] = raw[0];
				rgb[1] = raw[2];
				rgb[2] = raw[4];
			}
		} else {
			memcpy(rgb, raw, (size_t)w * 3);
		}
		break;
	case 3: // palette
		for (int x = 0; x < w; x++, rgb += 3) {
			int idx = depth == 8 ? raw[x] : (raw[x >> per_shift] >> ((per_mask - (x & per_mask)) * depth)) & mask;
			if (idx >= h->palette_count) idx = 0;
			const uint8_t *c = h->palette[idx];
			int a = h->trans[idx];
			if (a == 255) {
				rgb[0] = c[0];
				rgb[1] = c[1];
				rgb[2] = c[2];
			} else {
				rgb[0] = on_white(c[0], a);
				rgb[1] = on_white(c[1], a);
				rgb[2] = on_white(c[2], a);
			}
		}
		break;
	case 4: { // grey with transparency
		int step = depth == 16 ? 4 : 2, ao = depth == 16 ? 2 : 1;
		for (int x = 0; x < w; x++, rgb += 3, raw += step) {
			int a = raw[ao];
			rgb[0] = rgb[1] = rgb[2] = a == 255 ? raw[0] : on_white(raw[0], a);
		}
		break;
	}
	default: { // 6: colour with transparency
		int step = depth == 16 ? 8 : 4, o = depth == 16 ? 2 : 1;
		for (int x = 0; x < w; x++, rgb += 3, raw += step) {
			int a = raw[3 * o];
			if (a == 255) {
				rgb[0] = raw[0];
				rgb[1] = raw[o];
				rgb[2] = raw[2 * o];
			} else {
				rgb[0] = on_white(raw[0], a);
				rgb[1] = on_white(raw[o], a);
				rgb[2] = on_white(raw[2 * o], a);
			}
		}
		break;
	}
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
		for (int i = 0; i < bpp && i < bytes; i++) row[i] = (uint8_t)(row[i] + (prev[i] >> 1)); // nothing to the left
		for (int i = bpp; i < bytes; i++) row[i] = (uint8_t)(row[i] + ((row[i - bpp] + prev[i]) >> 1));
		break;
	case 4:
		for (int i = 0; i < bpp && i < bytes; i++) row[i] = (uint8_t)(row[i] + prev[i]); // paeth(0, b, 0) is b
		for (int i = bpp; i < bytes; i++) row[i] = (uint8_t)(row[i] + paeth(row[i - bpp], prev[i], prev[i - bpp]));
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
	// (Read like the data, so that it may be spread over two chunks: found by tpn.c, decision 114.)
	uint8_t zlib_header[2];
	s->zlib_header_done = true;
	if (idat_read(s, zlib_header, 2) != 2) goto done;

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
