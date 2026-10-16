/* SPDX-License-Identifier: MIT */
/* graphics.library: 32-bit multiply and divide for the 68000 build (as layers_arith.c): the os32-gcc16
 * stove's 68000 libgcc.a does not define them under -nostdlib. Shift and
 * add, shift and subtract. Copyright (c) 2026 AmigaChrome Team. MIT. */
unsigned long __mulsi3(unsigned long a, unsigned long b);
unsigned long __udivsi3(unsigned long a, unsigned long b);
unsigned long __umodsi3(unsigned long a, unsigned long b);
long __divsi3(long a, long b);
long __modsi3(long a, long b);

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
    unsigned long r = 0;
    while (b) {
        if (b & 1) r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}

static unsigned long udiv(unsigned long a, unsigned long b, unsigned long *rem)
{
    unsigned long q = 0, r = 0;
    int i;
    for (i = 31; i >= 0; i--) {
        r = r << 1 | (a >> i & 1);
        if (r >= b) { r -= b; q |= 1ul << i; }
    }
    *rem = r;
    return q;
}

unsigned long __udivsi3(unsigned long a, unsigned long b) { unsigned long r; return udiv(a, b, &r); }
unsigned long __umodsi3(unsigned long a, unsigned long b) { unsigned long r; udiv(a, b, &r); return r; }

long __divsi3(long a, long b)
{
    unsigned long q = __udivsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b);
    return (a < 0) != (b < 0) ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
    unsigned long r = __umodsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b);
    return a < 0 ? -(long)r : (long)r;
}
