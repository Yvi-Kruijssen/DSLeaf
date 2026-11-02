#include "recent.h"

#include "bookname.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "storage.h"

#define FILE_CAP 1024

static bool valid_path(const char *path) {
	size_t len = strlen(path);
	return len > 1 && len < RECENT_PATH_MAX && path[0] == '/';
}

static int find(const RecentList *list, const char *path) {
	for (int i = 0; i < list->count; i++) {
		if (strcmp(list->path[i], path) == 0) return i;
	}
	return -1;
}

void recent_remove(RecentList *list, int index) {
	if (index < 0 || index >= list->count) return;
	for (int i = index; i < list->count - 1; i++) {
		memcpy(list->path[i], list->path[i + 1], RECENT_PATH_MAX);
	}
	list->count--;
	list->path[list->count][0] = '\0';
}

bool recent_touch(RecentList *list, const char *path) {
	if (!valid_path(path)) return false;

	int existing = find(list, path);
	if (existing >= 0) recent_remove(list, existing);
	if (list->count == RECENT_MAX) recent_remove(list, RECENT_MAX - 1);

	for (int i = list->count; i > 0; i--) {
		memcpy(list->path[i], list->path[i - 1], RECENT_PATH_MAX);
	}
	snprintf(list->path[0], RECENT_PATH_MAX, "%s", path);
	list->count++;
	return true;
}

void recent_parse(RecentList *list, const char *text) {
	memset(list, 0, sizeof *list);

	char line[RECENT_PATH_MAX + 8];
	while (*text && list->count < RECENT_MAX) {
		size_t n = strcspn(text, "\n");
		size_t take = n < sizeof line - 1 ? n : sizeof line - 1;
		memcpy(line, text, take);
		line[take] = '\0';
		text += n;
		if (*text == '\n') text++;

		// Trim, including the \r of Windows line endings.
		char *s = line;
		while (isspace((unsigned char)*s)) s++;
		char *end = s + strlen(s);
		while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';

		if (*s == '#' || !valid_path(s) || find(list, s) >= 0) continue;
		memcpy(list->path[list->count++], s, strlen(s) + 1); // valid_path checked the length
	}
}

int recent_format(const RecentList *list, char *buf, size_t cap) {
	size_t used = 0;
	if (cap > 0) buf[0] = '\0';
	for (int i = 0; i < list->count; i++) {
		int n = snprintf(buf + used, cap - used, "%s\n", list->path[i]);
		if (n < 0 || (size_t)n >= cap - used) return -1;
		used += (size_t)n;
	}
	return (int)used;
}

void recent_title_from_path(const char *path, char *buf, size_t cap) {
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;

	size_t len = strlen(name);
	len -= (size_t)book_ending_length(name);
	if (len >= cap) len = cap - 1;
	memcpy(buf, name, len);
	buf[len] = '\0';
}

bool recent_load(RecentList *list) {
	char text[FILE_CAP];
	if (storage_read_text(RECENT_PATH, text, sizeof text) < 0) {
		memset(list, 0, sizeof *list);
		return false;
	}
	recent_parse(list, text);
	return true;
}

bool recent_save(const RecentList *list) {
	char text[FILE_CAP];
	if (recent_format(list, text, sizeof text) < 0) return false;
	return storage_write_text(RECENT_PATH, text);
}
