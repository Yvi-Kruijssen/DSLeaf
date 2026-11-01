#include "zip.h"

#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define EOCD_SIG 0x06054b50u
#define CDIR_SIG 0x02014b50u
#define LOCAL_SIG 0x04034b50u
#define EOCD_SIZE 22
#define MAX_COMMENT 65535

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_at(FILE *f, uint32_t pos, void *buf, size_t len) {
	return fseek(f, (long)pos, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

bool zip_open(Zip *zip, const char *path) {
	memset(zip, 0, sizeof *zip);
	zip->file = storage_fopen(path, "rb");
	if (!zip->file) return false;

	uint8_t *tail = NULL;
	if (fseek(zip->file, 0, SEEK_END) != 0) goto fail;
	long size = ftell(zip->file);
	if (size < EOCD_SIZE) goto fail;

	// The end-of-directory record is at the very end, followed only by an
	// optional comment of up to 64 KB. Read that last part once and search it.
	long scan = EOCD_SIZE + MAX_COMMENT;
	if (scan > size) scan = size;
	tail = malloc((size_t)scan);
	if (!tail || !read_at(zip->file, (uint32_t)(size - scan), tail, (size_t)scan)) goto fail;

	for (long i = scan - EOCD_SIZE; i >= 0; i--) {
		if (le32(tail + i) != EOCD_SIG) continue;

		const uint8_t *e = tail + i;
		uint16_t disk = le16(e + 4), dir_disk = le16(e + 6);
		uint16_t count = le16(e + 10);
		uint32_t dir_size = le32(e + 12), dir_offset = le32(e + 16);
		if (disk != 0 || dir_disk != 0) goto fail; // split archive
		if (count == 0xFFFF || dir_size == 0xFFFFFFFFu || dir_offset == 0xFFFFFFFFu) goto fail; // zip64
		if ((long long)dir_offset + dir_size > size) goto fail;

		zip->entry_count = count;
		zip->dir_offset = dir_offset;
		zip->dir_size = dir_size;
		free(tail);
		return true;
	}

fail:
	free(tail);
	fclose(zip->file);
	zip->file = NULL;
	return false;
}

void zip_close(Zip *zip) {
	if (zip->file) fclose(zip->file);
	zip->file = NULL;
}

void zip_iter_start(const Zip *zip, ZipIter *iter) {
	iter->pos = zip->dir_offset;
	iter->index = 0;
}

bool zip_iter_next(Zip *zip, ZipIter *iter, ZipEntry *entry) {
	while (iter->index < zip->entry_count) {
		uint8_t h[46];
		if (iter->pos + sizeof h > zip->dir_offset + zip->dir_size) return false;
		if (!read_at(zip->file, iter->pos, h, sizeof h) || le32(h) != CDIR_SIG) return false;

		uint16_t name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
		uint32_t next = iter->pos + (uint32_t)sizeof h + name_len + extra_len + comment_len;
		if (next > zip->dir_offset + zip->dir_size) return false;

		bool fits = name_len < ZIP_NAME_MAX;
		if (fits) {
			if (fread(entry->name, 1, name_len, zip->file) != name_len) return false;
			entry->name[name_len] = '\0';
			entry->flags = le16(h + 8);
			entry->method = le16(h + 10);
			entry->crc32 = le32(h + 16);
			entry->compressed_size = le32(h + 20);
			entry->size = le32(h + 24);
			entry->header_offset = le32(h + 42);
		}
		iter->pos = next;
		iter->index++;
		if (fits) return true;
	}
	return false;
}

bool zip_find(Zip *zip, const char *name, ZipEntry *entry) {
	ZipIter it;
	zip_iter_start(zip, &it);
	while (zip_iter_next(zip, &it, entry)) {
		if (strcmp(entry->name, name) == 0) return true;
	}
	return false;
}

static size_t source_read(void *user, uint8_t *buf, size_t cap) {
	ZipStream *s = user;
	if (s->in_left == 0) return 0;
	if (cap > s->in_left) cap = s->in_left;
	if (fseek(s->file, (long)s->pos, SEEK_SET) != 0) return 0;
	size_t n = fread(buf, 1, cap, s->file);
	s->pos += (uint32_t)n;
	s->in_left -= (uint32_t)n;
	return n;
}

bool zip_stream_open(Zip *zip, const ZipEntry *entry, ZipStream *stream) {
	memset(stream, 0, sizeof *stream);
	if (entry->flags & 1) return false;                      // encrypted
	if (entry->method != 0 && entry->method != 8) return false;

	uint8_t h[30];
	if (!read_at(zip->file, entry->header_offset, h, sizeof h) || le32(h) != LOCAL_SIG) return false;
	uint32_t data = entry->header_offset + (uint32_t)sizeof h + le16(h + 26) + le16(h + 28);

	stream->file = zip->file;
	stream->pos = data;
	stream->in_left = entry->compressed_size;
	stream->out_left = entry->size;
	stream->deflated = entry->method == 8;

	if (stream->deflated) {
		stream->inflate = malloc(sizeof *stream->inflate);
		if (!stream->inflate) return false;
		inflate_init(stream->inflate, source_read, stream);
	}
	return true;
}

int zip_stream_read(ZipStream *s, uint8_t *buf, size_t cap) {
	if (s->out_left == 0 || cap == 0) return 0;
	if (cap > s->out_left) cap = s->out_left;

	int n;
	if (s->deflated) {
		n = inflate_read(s->inflate, buf, cap);
	} else {
		n = (int)source_read(s, buf, cap);
		if (n == 0) n = -1; // the archive ended early
	}
	if (n > 0) s->out_left -= (uint32_t)n;
	if (n == 0) return -1; // the data ended before the size the zip promised
	return n;
}

void zip_stream_close(ZipStream *s) {
	free(s->inflate);
	s->inflate = NULL;
}

uint32_t zip_crc32(uint32_t crc, const uint8_t *data, size_t len) {
	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}
