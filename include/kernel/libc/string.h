#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include <kernel/types.h>

void *memset(void *ptr, int value, size_t size);
int strcmp(const char *s1, const char *s2);
char* strcpy(char* dest, const char* src);
void *memcpy(void *dest, const void *src, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);

#endif
