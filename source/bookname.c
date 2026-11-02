#include "bookname.h"

#include <string.h>
#include <strings.h>

int book_ending_length(const char *name) {
	size_t len = strlen(name);
	if (len > 5 && strcasecmp(name + len - 5, ".epub") == 0) return 5;
	if (len > 4 && strcasecmp(name + len - 4, ".txt") == 0) return 4;
	return 0;
}
