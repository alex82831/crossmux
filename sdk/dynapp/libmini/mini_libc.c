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

static unsigned emit_num(char* out, unsigned cap, unsigned pos, uint32_t value, unsigned base, int negative, int width,
                         char pad) {
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

char* strchr(const char* s, int c) {
  for (; *s; ++s) {
    if (*s == (char)c) return (char*)s;
  }
  return (c == 0) ? (char*)s : (void*)0;
}

// --- 64-bit integer helpers ---------------------------------------------
// rv32imc has native 32-bit mul/div/rem but calls libgcc for 64-bit ops,
// which we cannot link under -nostdlib. Provide the few the compiler emits
// so apps can use `long long` (fixed-point math) while staying self-contained.
// Local calls to these are PC-relative and need no dynamic relocation.

typedef union {
  unsigned long long u64;
  struct {
    unsigned int lo, hi;  // little-endian
  } s;
} u64parts;

unsigned long long __ashldi3(unsigned long long v, int cnt) {
  u64parts x;
  x.u64 = v;
  if (cnt == 0) return v;
  if (cnt >= 32) {
    x.s.hi = x.s.lo << (cnt - 32);
    x.s.lo = 0;
  } else {
    x.s.hi = (x.s.hi << cnt) | (x.s.lo >> (32 - cnt));
    x.s.lo = x.s.lo << cnt;
  }
  return x.u64;
}

unsigned long long __lshrdi3(unsigned long long v, int cnt) {
  u64parts x;
  x.u64 = v;
  if (cnt == 0) return v;
  if (cnt >= 32) {
    x.s.lo = x.s.hi >> (cnt - 32);
    x.s.hi = 0;
  } else {
    x.s.lo = (x.s.lo >> cnt) | (x.s.hi << (32 - cnt));
    x.s.hi = x.s.hi >> cnt;
  }
  return x.u64;
}

long long __ashrdi3(long long v, int cnt) {
  u64parts x;
  x.u64 = (unsigned long long)v;
  if (cnt == 0) return v;
  const unsigned int sign = x.s.hi & 0x80000000u;
  if (cnt >= 32) {
    x.s.lo = (unsigned int)((int)x.s.hi >> (cnt - 32));
    x.s.hi = sign ? 0xFFFFFFFFu : 0;
  } else {
    x.s.lo = (x.s.lo >> cnt) | (x.s.hi << (32 - cnt));
    x.s.hi = (unsigned int)((int)x.s.hi >> cnt);
  }
  return (long long)x.u64;
}

unsigned long long __udivmoddi4(unsigned long long num, unsigned long long den, unsigned long long* rem_out) {
  unsigned long long quot = 0, rem = 0;
  for (int i = 63; i >= 0; --i) {
    rem = (rem << 1) | ((num >> i) & 1u);
    if (rem >= den) {
      rem -= den;
      quot |= (1ull << i);
    }
  }
  if (rem_out) *rem_out = rem;
  return quot;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b) { return __udivmoddi4(a, b, 0); }
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
  unsigned long long r;
  __udivmoddi4(a, b, &r);
  return r;
}
long long __divdi3(long long a, long long b) {
  int neg = (a < 0) ^ (b < 0);
  unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
  unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
  unsigned long long q = __udivmoddi4(ua, ub, 0);
  return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long a, long long b) {
  unsigned long long ua = a < 0 ? -(unsigned long long)a : (unsigned long long)a;
  unsigned long long ub = b < 0 ? -(unsigned long long)b : (unsigned long long)b;
  unsigned long long r;
  __udivmoddi4(ua, ub, &r);
  return a < 0 ? -(long long)r : (long long)r;
}
