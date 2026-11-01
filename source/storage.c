#include "storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ARM9
#include <fat.h>
#endif

#define PATH_CAP 320

static char root[PATH_CAP];
static bool available;

void storage_set_root(const char *new_root) {
	snprintf(root, sizeof root, "%s", new_root);
}

static bool full_path(char *out, const char *path, const char *suffix) {
	int n = snprintf(out, PATH_CAP, "%s%s%s", root, path, suffix);
	return n > 0 && n < PATH_CAP;
}

static bool make_dir(const char *path) {
	char full[PATH_CAP];
	if (!full_path(full, path, "")) return false;
	if (mkdir(full, 0777) == 0 || errno == EEXIST) return true;
	return false;
}

bool storage_init(void) {
#ifdef ARM9
	if (!fatInitDefault()) {
		available = false;
		return false;
	}
#endif
	available = make_dir(STORAGE_APP_DIR) && make_dir(STORAGE_BOOKS_DIR);
	return available;
}

bool storage_available(void) { return available; }

static int read_file(const char *full, char *buf, size_t cap) {
	FILE *f = fopen(full, "rb");
	if (!f) return -1;
	size_t n = fread(buf, 1, cap - 1, f);
	bool failed = ferror(f);
	fclose(f);
	if (failed) return -1;
	buf[n] = '\0';
	return (int)n;
}

int storage_read_text(const char *path, char *buf, size_t cap) {
	char full[PATH_CAP];
	if (cap == 0 || !full_path(full, path, "")) return -1;

	int n = read_file(full, buf, cap);
	if (n >= 0) return n;

	if (!full_path(full, path, ".tmp")) return -1;
	return read_file(full, buf, cap);
}

bool storage_write_text(const char *path, const char *text) {
	char full[PATH_CAP], tmp[PATH_CAP];
	if (!full_path(full, path, "") || !full_path(tmp, path, ".tmp")) return false;

	FILE *f = fopen(tmp, "wb");
	if (!f) return false;
	size_t len = strlen(text);
	bool ok = fwrite(text, 1, len, f) == len;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		unlink(tmp);
		return false;
	}

	// FAT cannot rename over an existing file, so remove the old one first.
	if (unlink(full) != 0 && errno != ENOENT) return false; // the .tmp stays as a backup
	return rename(tmp, full) == 0;
}

bool storage_list_dir(const char *path, StorageDirFn visit, void *user) {
	char full[PATH_CAP];
	if (!full_path(full, path, "")) return false;

	DIR *dir = opendir(full);
	if (!dir) return false;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		const char *name = entry->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		bool is_dir = entry->d_type == DT_DIR;
		if (entry->d_type == DT_UNKNOWN) {
			char child[PATH_CAP];
			struct stat st;
			int n = snprintf(child, sizeof child, "%s/%s", full, name);
			if (n > 0 && n < (int)sizeof child && stat(child, &st) == 0) is_dir = S_ISDIR(st.st_mode);
		}
		if (!visit(name, is_dir, user)) break;
	}
	closedir(dir);
	return true;
}

FILE *storage_fopen(const char *path, const char *mode) {
	char full[PATH_CAP];
	if (!full_path(full, path, "")) return NULL;
	return fopen(full, mode);
}
