#include "imageinfo.h"

#include <string.h>

static uint32_t be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static ProbeResult probe_png(const uint8_t *d, size_t len, ImageInfo *info) {
	if (len < 26) return PROBE_NEED_MORE;
	if (memcmp(d + 12, "IHDR", 4) != 0) return PROBE_BAD;
	info->kind = IMAGE_PNG;
	info->width = be32(d + 16);
	info->height = be32(d + 20);
	info->bit_depth = d[24];
	info->color_type = d[25];
	info->progressive = len > 28 && d[28] != 0;
	return (info->width && info->height) ? PROBE_OK : PROBE_BAD;
}

static ProbeResult probe_jpeg(const uint8_t *d, size_t len, ImageInfo *info) {
	size_t i = 2;
	for (;;) {
		while (i < len && d[i] != 0xFF) i++;
		while (i < len && d[i] == 0xFF) i++; // fill bytes
		if (i >= len) return PROBE_NEED_MORE;
		uint8_t m = d[i++];
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue; // no length
		if (m == 0xD9 || m == 0xDA) return PROBE_BAD;                      // end or data before any frame
		if (i + 2 > len) return PROBE_NEED_MORE;
		size_t seg = ((size_t)d[i] << 8) | d[i + 1];
		if (seg < 2) return PROBE_BAD;
		bool sof = m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
		if (sof) {
			if (i + 8 > len) return PROBE_NEED_MORE;
			info->kind = IMAGE_JPEG;
			info->height = ((uint32_t)d[i + 3] << 8) | d[i + 4];
			info->width = ((uint32_t)d[i + 5] << 8) | d[i + 6];
			info->components = d[i + 7];
			// Only baseline (C0) and extended sequential (C1, 8 bit) are decoded.
			info->progressive = m != 0xC0 && m != 0xC1;
			return (info->width && info->height) ? PROBE_OK : PROBE_BAD;
		}
		i += seg;
	}
}

ProbeResult image_probe(const uint8_t *d, size_t len, ImageInfo *info) {
	memset(info, 0, sizeof *info);
	if (len < 4) return PROBE_NEED_MORE;
	if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return probe_png(d, len, info);
	if (d[0] == 0xFF && d[1] == 0xD8) return probe_jpeg(d, len, info);
	return PROBE_BAD;
}

void image_fit(uint32_t width, uint32_t height, int sw, int sh, int *out_w, int *out_h) {
	uint64_t w = width, h = height;
	if (w > (uint64_t)sw || h > (uint64_t)sh) {
		// scale = min(sw / w, sh / h)
		if (w * (uint64_t)sh >= h * (uint64_t)sw) {
			h = (h * (uint64_t)sw) / w;
			w = (uint64_t)sw;
		} else {
			w = (w * (uint64_t)sh) / h;
			h = (uint64_t)sh;
		}
	}
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	*out_w = (int)w;
	*out_h = (int)h;
}

bool image_wanted(uint32_t width, uint32_t height) {
	// Small ones are ornaments, icons or the thumbnails next to a larger picture.
	if (width < 48 || height < 48 || (width < 120 && height < 120)) return false;
	int w, h;
	image_fit(width, height, 256, 192, &w, &h);
	return w >= 32 && h >= 32;
}
