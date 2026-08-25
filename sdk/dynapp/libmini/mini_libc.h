#pragma once

// Freestanding helpers available to every dynamic app (see mini_libc.c).

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* dst, int value, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);

// Minimal snprintf: %d %u %x %s %c %% with optional zero-pad width.
int cp_snprintf(char* out, unsigned cap, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
