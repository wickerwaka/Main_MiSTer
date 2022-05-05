/*
* util.h
*
*/

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#define ARRAY_COUNT(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

int str_tokenize(char *s, const char *delim, char **tokens, int max_tokens);

// String copy with guaranteed null termination
char *strncpyz(char *dest, size_t dest_size, const char *src, size_t num);
char *strcpyz(char *dest, size_t dest_size, const char *src);

template<size_t N>
char *strncpyz(char (&dest)[N], const char *src, size_t num)
{
    return strncpyz(dest, N, src, num);
}

template<size_t N>
char *strcpyz(char (&dest)[N], const char *src)
{
    return strcpyz(dest, N, src);
}


#endif // UTIL_H