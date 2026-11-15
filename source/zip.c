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
	// The size comes from the directory of the card when it can: a seek to the end of the file makes the card's file
	// system walk the whole chain of the file's clusters, and the seek back to the last 64 KB walks it again
	// (a tenth of the time of opening a 25 MB book on the emulated DS, decision 116).
	long size = storage_file_size(path);
	if (size < 0) {
		if (fseek(zip->file, 0, SEEK_END) != 0) goto fail;
		size = ftell(zip->file);
	}
	if (size < EOCD_SIZE) goto fail;
	zip->size = (uint32_t)size;

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
		// Looking up an entry walks the directory, and the program does that many times when it opens a book: reading it
		// entry by entry from the SD card took seconds (decision 107). It is usually inside the tail read above already.
		if (dir_size > 0 && dir_size <= ZIP_DIR_CACHE_MAX) {
			zip->dir = malloc(dir_size);
			if (zip->dir) {
				if (dir_offset >= (uint32_t)(size - scan)) memcpy(zip->dir, tail + (dir_offset - (uint32_t)(size - scan)), dir_size);
				else if (!read_at(zip->file, dir_offset, zip->dir, dir_size)) {
					free(zip->dir);
					zip->dir = NULL; // the entries are read from the file one by one, as before
				}
			}
		}
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
	free(zip->dir);
	zip->dir = NULL;
	if (zip->file) fclose(zip->file);
	zip->file = NULL;
}

void zip_iter_start(const Zip *zip, ZipIter *iter) {
	iter->pos = zip->dir_offset;
	iter->index = 0;
}

// The name of the entry at the iterator equals `name`: looked at in the directory in memory without copying the entry.
// -1 when that cannot be told this way (no directory in memory, or the place is not an entry).
static int name_matches(const Zip *zip, const ZipIter *iter, const char *name, size_t name_len) {
	if (!zip->dir || iter->pos < zip->dir_offset || iter->pos + 46 > zip->dir_offset + zip->dir_size) return -1;
	const uint8_t *h = zip->dir + (iter->pos - zip->dir_offset);
	if (le32(h) != CDIR_SIG) return -1;
	uint16_t len = le16(h + 28);
	if (iter->pos + 46 + len > zip->dir_offset + zip->dir_size) return -1;
	return len == name_len && memcmp(h + 46, name, name_len) == 0;
}

bool zip_iter_next(Zip *zip, ZipIter *iter, ZipEntry *entry) {
	while (iter->index < zip->entry_count) {
		uint8_t hbuf[46];
		const uint8_t *h = hbuf;
		if (iter->pos + sizeof hbuf > zip->dir_offset + zip->dir_size) return false;
		if (zip->dir) h = zip->dir + (iter->pos - zip->dir_offset);
		else if (!read_at(zip->file, iter->pos, hbuf, sizeof hbuf)) return false;
		if (le32(h) != CDIR_SIG) return false;

		uint16_t name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
		uint32_t next = iter->pos + (uint32_t)sizeof hbuf + name_len + extra_len + comment_len;
		if (next > zip->dir_offset + zip->dir_size) return false;

		bool fits = name_len < ZIP_NAME_MAX;
		if (fits) {
			if (zip->dir) memcpy(entry->name, zip->dir + (iter->pos - zip->dir_offset) + sizeof hbuf, name_len);
			else if (fread(entry->name, 1, name_len, zip->file) != name_len) return false;
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

// Looks for `name` among the entries from the iterator up to entry number `stop`. With the directory in memory the
// names are compared in place and only the entry that is wanted is copied.
static bool find_in(Zip *zip, ZipIter *it, uint16_t stop, const char *name, size_t name_len, ZipEntry *entry) {
	while (it->index < stop) {
		if (name_matches(zip, it, name, name_len) == 0) { // another name: step over the entry
			const uint8_t *h = zip->dir + (it->pos - zip->dir_offset);
			it->pos += 46u + le16(h + 28) + le16(h + 30) + le16(h + 32);
			it->index++;
			continue;
		}
		if (!zip_iter_next(zip, it, entry)) return false; // (it checks the entry, and skips names that are too long)
		if (strcmp(entry->name, name) == 0) return true;
	}
	return false;
}

bool zip_find(Zip *zip, const char *name, ZipEntry *entry) {
	// From the entry after the one found last to the end, then from the start up to there. Opening a book looks up
	// every chapter, and they are nearly always in the order of the archive: each was a walk from the start before,
	// which grows with the square of the number of files (decision 116). (Of two entries with the same name, which no
	// proper archive has, the one that is met first this way is found.)
	size_t name_len = strlen(name);
	ZipIter it;
	uint16_t from = zip->find_index;
	bool found = false;
	if (from > 0 && from < zip->entry_count) {
		it.pos = zip->find_pos;
		it.index = from;
		found = find_in(zip, &it, zip->entry_count, name, name_len, entry);
	} else {
		from = zip->entry_count;
	}
	if (!found) {
		zip_iter_start(zip, &it);
		found = find_in(zip, &it, from, name, name_len, entry);
	}
	if (found) {
		zip->find_pos = it.pos;
		zip->find_index = it.index;
	}
	return found;
}

static size_t source_read(void *user, uint8_t *buf, size_t cap) {
	ZipStream *s = user;
	if (s->in_left == 0) return 0;
	if (cap > s->in_left) cap = s->in_left;
	if (ftell(s->file) != (long)s->pos && fseek(s->file, (long)s->pos, SEEK_SET) != 0) return 0; // no seek while reading on
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
