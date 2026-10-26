#include <kernel/libc/string.h>

void *memset(void *ptr, int value, size_t size) {
  char *p = (char *)ptr;
  for (size_t i = 0; i < size; i++) {
    p[i] = value;
  }
  return ptr;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while ((*dest++ = *src++));
    return original_dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if ((char)c == '\0') {
        return (char *)s;
    }
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;

    do {
        if (*s == (char)c) {
            last = s;
        }
    } while (*s++);
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    size_t n = strlen(needle);

    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, n) == 0) {
            return (char *)haystack;
        }
    }
    return n == 0 ? (char *)haystack : NULL;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        while (n--) {
            d[n] = s[n];
        }
    }
    return dst;
}
