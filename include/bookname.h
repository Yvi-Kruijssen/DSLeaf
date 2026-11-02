// Which files are books (docs: formats.md, decision 074): .epub and .txt, in any case.
#ifndef DSLEAF_BOOKNAME_H
#define DSLEAF_BOOKNAME_H

// The length of the ending (".epub" is 5, ".txt" is 4), or 0 if the name does not end like a book that has
// something before the ending.
int book_ending_length(const char *name);

#endif
