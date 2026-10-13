/* SPDX-License-Identifier: MIT */
/* What library/core's gfx_rp.c needs on the 68000 beyond rt68000.c, for
 * tests/m68k/test_gfx_text.sh: a 64-bit multiply (its ellipses), shift and
 * add on 32-bit halves. Copyright (c) 2026 Dalsin Limited. MIT. */
unsigned long long __muldi3(unsigned long long a, unsigned long long b);
unsigned long long __muldi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r = 0;
    while (b) {
        if (b & 1) r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}
