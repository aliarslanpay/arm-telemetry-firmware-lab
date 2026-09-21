/* SPDX-License-Identifier: MIT
 * Minimal freestanding libc functions used by generated code and the kernel.
 * No allocator or full C library is linked.
 */
#include <stddef.h>
void *memset(void *dst, int value, size_t n) {
    unsigned char *p = dst; while (n--) *p++ = (unsigned char)value; return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src; while (n--) *d++ = *s++; return dst;
}
int memcmp(const void *left, const void *right, size_t n) {
    const unsigned char *a = left, *b = right;
    while (n--) { if (*a != *b) return *a - *b; ++a; ++b; } return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
