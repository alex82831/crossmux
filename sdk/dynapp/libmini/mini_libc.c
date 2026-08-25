// Freestanding helpers linked into every dynamic app. GCC emits calls to
// mem* even at -Os with -nostdlib, so these must exist; the string/format
// helpers are for app code. Everything here is position-independent C with
// no external references.

#include <stddef.h>
#include <stdint.h>

void* memset(void* dst, int value, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  while (n--) *d++ = (uint8_t)value;
  return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  while (n--) *d++ = *s++;
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  if (d < s) {
    while (n--) *d++ = *s++;
  } else if (d > s) {
    d += n;
    s += n;
    while (n--) *--d = *--s;
  }
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const uint8_t* pa = (const uint8_t*)a;
  const uint8_t* pb = (const uint8_t*)b;
  for (; n--; ++pa, ++pb) {
    if (*pa != *pb) return (int)*pa - (int)*pb;
  }
  return 0;
}

size_t strlen(const char* s) {
  const char* p = s;
  while (*p) ++p;
  return (size_t)(p - s);
}

// Minimal snprintf: %d %u %x %s %c %% with optional zero-pad width (e.g.
// %03d). Always NUL-terminates when cap > 0; returns chars that would have
// been written (snprintf semantics, truncated output stays valid).
int cp_snprintf(char* out, unsigned cap, const char* fmt, ...);

typedef __builtin_va_list cp_va_list;
#define cp_va_start(v, l) __builtin_va_start(v, l)
#define cp_va_arg(v, t) __builtin_va_arg(v, t)
#define cp_va_end(v) __builtin_va_end(v)

static unsigned emit_char(char* out, unsigned cap, unsigned pos, char c) {
  if (pos + 1 < cap) out[pos] = c;
  return pos + 1;
}

static unsigned emit_num(char* out, unsigned cap, unsigned pos, uint32_t value, unsigned base, int negative,
                         int width, char pad) {
  char tmp[12];
  int n = 0;
  do {
    const unsigned digit = value % base;
    tmp[n++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    value /= base;
  } while (value != 0 && n < (int)sizeof(tmp));
  if (negative) tmp[n++] = '-';
  for (int i = n; i < width && i < (int)sizeof(tmp); ++i) tmp[n++] = pad;
  while (n > 0) pos = emit_char(out, cap, pos, tmp[--n]);
  return pos;
}

int cp_snprintf(char* out, unsigned cap, const char* fmt, ...) {
  cp_va_list ap;
  cp_va_start(ap, fmt);
  unsigned pos = 0;
  for (const char* p = fmt; *p; ++p) {
    if (*p != '%') {
      pos = emit_char(out, cap, pos, *p);
      continue;
    }
    ++p;
    char pad = ' ';
    int width = 0;
    if (*p == '0') {
      pad = '0';
      ++p;
    }
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      ++p;
    }
    switch (*p) {
      case 'd': {
        const int32_t v = cp_va_arg(ap, int32_t);
        pos = emit_num(out, cap, pos, v < 0 ? (uint32_t)(-v) : (uint32_t)v, 10, v < 0, width, pad);
        break;
      }
      case 'u':
        pos = emit_num(out, cap, pos, cp_va_arg(ap, uint32_t), 10, 0, width, pad);
        break;
      case 'x':
        pos = emit_num(out, cap, pos, cp_va_arg(ap, uint32_t), 16, 0, width, pad);
        break;
      case 's': {
        const char* s = cp_va_arg(ap, const char*);
        if (!s) s = "(null)";
        while (*s) pos = emit_char(out, cap, pos, *s++);
        break;
      }
      case 'c':
        pos = emit_char(out, cap, pos, (char)cp_va_arg(ap, int));
        break;
      case '%':
        pos = emit_char(out, cap, pos, '%');
        break;
      default:
        pos = emit_char(out, cap, pos, '%');
        if (*p) pos = emit_char(out, cap, pos, *p);
        break;
    }
    if (!*p) break;
  }
  cp_va_end(ap);
  if (cap > 0) out[pos < cap ? pos : cap - 1] = '\0';
  return (int)pos;
}
