/* From tests/m68k/test_gfx_core.sh (its 68000 runtime, written out at test time),
 * kept when the builds went 68040 only. */
#include <stdarg.h>
#include <stddef.h>
int main(void);
static long sys3(long n, long a, long b, long c)
{
    register long d0 __asm__("d0") = n, d1 __asm__("d1") = a, d2 __asm__("d2") = b, d3 __asm__("d3") = c;
    __asm__ volatile ("trap #0" : "+d"(d0) : "d"(d1), "d"(d2), "d"(d3) : "memory");
    return d0;
}
void _start(void) { for (;;) sys3(1, main(), 0, 0); }
void *memset(void *d, int v, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)v; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
static unsigned long udiv(unsigned long a, unsigned long b, unsigned long *rem)
{
    unsigned long q = 0, r = 0;
    int i;
    for (i = 31; i >= 0; i--) { r = r << 1 | (a >> i & 1); if (r >= b) { r -= b; q |= 1ul << i; } }
    *rem = r;
    return q;
}
unsigned long __udivsi3(unsigned long a, unsigned long b) { unsigned long r; return udiv(a, b, &r); }
unsigned long __umodsi3(unsigned long a, unsigned long b) { unsigned long r; udiv(a, b, &r); return r; }
long __divsi3(long a, long b) { unsigned long q = __udivsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b); return (a < 0) != (b < 0) ? -(long)q : (long)q; }
long __modsi3(long a, long b) { unsigned long r = __umodsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b); return a < 0 ? -(long)r : (long)r; }
unsigned long __mulsi3(unsigned long a, unsigned long b) { unsigned long r = 0; while (b) { if (b & 1) r += a; a <<= 1; b >>= 1; } return r; }
static char out[256];
static int len;
static void put(char c) { out[len++] = c; if (len == sizeof out) { sys3(4, 1, (long)out, len); len = 0; } }
static void num(unsigned long v, int neg) { char t[12]; int i = 0; if (neg) put('-'); do t[i++] = '0' + v % 10; while (v /= 10); while (i) put(t[--i]); }
int printf(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    for (; *f; f++) {
        if (*f != '%') { put(*f); continue; }
        if (*++f == 'l') f++;
        if (*f == 's') { const char *s = va_arg(ap, const char *); while (*s) put(*s++); }
        else if (*f == 'd') { long v = va_arg(ap, long); num(v < 0 ? -(unsigned long)v : (unsigned long)v, v < 0); }
        else num(va_arg(ap, unsigned long), 0);
    }
    va_end(ap);
    sys3(4, 1, (long)out, len); len = 0;
    return 0;
}
int puts(const char *s) { return printf("%s\n", s); }
int putchar(int c) { put((char)c); sys3(4, 1, (long)out, len); len = 0; return c; }
