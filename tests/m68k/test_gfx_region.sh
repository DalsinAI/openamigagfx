#!/bin/sh
# OpenGfx's regions (library/core/gfx_region.c) built with an m68k cross GCC
# for the 68000, 68020 and 68040 at -O0 and -O2, each run on that CPU under
# qemu-m68k, against a plain model: a grid of cells, one a pixel. Random
# rectangles (some empty, some negative) and every Rect and Region call on
# three regions; after each call the region's cells, its bands, its bounds
# and its list links are checked, and some calls are made to run out of
# memory part way, which must leave the region as it was. At the end every
# byte the regions took has been given back.
#   sh tests/m68k/test_gfx_region.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
core=$here/../../library/core
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

cat > "$tmp/t.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "gfx_region.h"
#define X0 (-24)
#define Y0 (-16)
#define W 96
#define H 64
static uint32_t seed = 0x4E71;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static unsigned long checks, fails;

/* memory: rectangles from one pool, work space from a few big slots */
#define NR 3000
#define NW 4
#define WS 32768
static struct gfx_rrect rpool[NR];
static struct gfx_rrect *rfree[NR];
static int nrfree;
static uint8_t wpool[NW][WS];
static uint32_t wsize[NW];
static long live, fail_after = -1;
static void *mem_alloc(void *ctx, uint32_t size)
{
    int i;
    (void)ctx;
    if (fail_after == 0) return 0;
    if (fail_after > 0) fail_after--;
    if (size == sizeof(struct gfx_rrect)) {
        if (!nrfree) { printf("FAIL out of test rectangles\n"); fails++; return 0; }
        live++;
        return rfree[--nrfree];
    }
    for (i = 0; i < NW; i++)
        if (!wsize[i]) {
            if (size > WS) { printf("FAIL work space %lu\n", (unsigned long)size); fails++; return 0; }
            wsize[i] = size;
            live++;
            return wpool[i];
        }
    printf("FAIL out of work slots\n");
    fails++;
    return 0;
}
static void mem_free(void *ctx, void *p, uint32_t size)
{
    int i;
    (void)ctx;
    live--;
    if (size == sizeof(struct gfx_rrect)) { rfree[nrfree++] = (struct gfx_rrect *)p; return; }
    for (i = 0; i < NW; i++)
        if (p == wpool[i]) {
            if (wsize[i] != size) { printf("FAIL free size\n"); fails++; }
            wsize[i] = 0;
            return;
        }
    printf("FAIL free of unknown memory\n");
    fails++;
}
static const struct gfx_rgn_mem mem = { mem_alloc, mem_free, 0 };

static struct gfx_region reg[3];
static uint8_t model[3][H][W], cells[H][W], before[H][W];

static void paint(const struct gfx_region *r)
{
    const struct gfx_rrect *p;
    int x, y;
    memset(cells, 0, sizeof cells);
    for (p = r->rects; p; p = p->next)
        for (y = r->bounds.min_y + p->bounds.min_y; y <= r->bounds.min_y + p->bounds.max_y; y++)
            for (x = r->bounds.min_x + p->bounds.min_x; x <= r->bounds.min_x + p->bounds.max_x; x++) {
                if (x < X0 || x >= X0 + W || y < Y0 || y >= Y0 + H) { printf("FAIL cell off the grid\n"); fails++; return; }
                if (cells[y - Y0][x - X0]) { printf("FAIL rectangles overlap\n"); fails++; }
                cells[y - Y0][x - X0] = 1;
            }
}
static void check(int i, const char *what)
{
    const struct gfx_region *r = &reg[i];
    const struct gfx_rrect *p, *q;
    int x, y, bx0 = 32767, by0 = 32767, bx1 = -32768, by1 = -32768, bad = 0;
    checks++;
    paint(r);
    if (memcmp(cells, model[i], sizeof cells)) bad = 1;
    for (p = r->rects, q = 0; p; q = p, p = p->next) {
        int ax0 = r->bounds.min_x + p->bounds.min_x, ax1 = r->bounds.min_x + p->bounds.max_x;
        int ay0 = r->bounds.min_y + p->bounds.min_y, ay1 = r->bounds.min_y + p->bounds.max_y;
        if (p->prev != q) bad = 2;
        if (ax0 < bx0) bx0 = ax0;
        if (ax1 > bx1) bx1 = ax1;
        if (ay0 < by0) by0 = ay0;
        if (ay1 > by1) by1 = ay1;
        if (q) {
            /* same band: same rows, left to right, not touching; else below */
            if (q->bounds.min_y == p->bounds.min_y) {
                if (q->bounds.max_y != p->bounds.max_y || q->bounds.max_x + 1 >= p->bounds.min_x) bad = 3;
            } else if (q->bounds.max_y >= p->bounds.min_y) bad = 4;
        }
    }
    if (!r->rects) {
        if (r->bounds.min_x || r->bounds.min_y || r->bounds.max_x || r->bounds.max_y) bad = 5;
    } else if (bx0 != r->bounds.min_x || by0 != r->bounds.min_y || bx1 != r->bounds.max_x || by1 != r->bounds.max_y)
        bad = 6;
    /* two touching bands never hold the same runs */
    for (p = r->rects; p && !bad; ) {
        const struct gfx_rrect *b = p, *n = p, *c;
        while (n && n->bounds.min_y == b->bounds.min_y) n = n->next;
        if (n && n->bounds.min_y == b->bounds.max_y + 1) {
            int same = 1;
            for (c = b, q = n; same && c != n; c = c->next) {
                same = q && q->bounds.min_y == n->bounds.min_y && q->bounds.min_x == c->bounds.min_x
                       && q->bounds.max_x == c->bounds.max_x;
                if (same) q = q->next;
            }
            if (same && (!q || q->bounds.min_y != n->bounds.min_y)) bad = 7;
        }
        p = n;
    }
    if (bad && ++fails < 20) printf("FAIL %s: region %ld (%ld)\n", what, (long)i, (long)bad);
    (void)x; (void)y;
}
static void rand_rect(struct gfx_rect *rc)
{
    rc->min_x = (int16_t)(X0 + (int)(rnd() % W));
    rc->min_y = (int16_t)(Y0 + (int)(rnd() % H));
    rc->max_x = (int16_t)(rc->min_x + (int)(rnd() % 40) - (rnd() % 16 == 0 ? 8 : 0));
    rc->max_y = (int16_t)(rc->min_y + (int)(rnd() % 28) - (rnd() % 16 == 0 ? 8 : 0));
    if (rc->max_x >= X0 + W) rc->max_x = X0 + W - 1;
    if (rc->max_y >= Y0 + H) rc->max_y = Y0 + H - 1;
}
static int in(int op, int a, int b)
{
    switch (op) {
    case GFX_RGN_OR: return a || b;
    case GFX_RGN_AND: return a && b;
    case GFX_RGN_XOR: return a != b;
    default: return a && !b;
    }
}
static const char *const opname[] = { "Or", "And", "Xor", "Clear" };
int main(void)
{
    int i, k, x, y, ok;
    for (i = 0; i < NR; i++) rfree[nrfree++] = &rpool[i];
    for (i = 0; i < 3; i++) gfx_rgn_init(&reg[i]);
    for (k = 0; k < 6000; k++) {
        int op = (int)(rnd() % 4), d = (int)(rnd() % 3), s = (int)(rnd() % 3), kind = (int)(rnd() % 8);
        struct gfx_rect rc;
        struct gfx_rrect *keep = reg[d].rects;
        struct gfx_rect kb = reg[d].bounds;
        long l0 = live;
        memcpy(before, model[d], sizeof before);
        fail_after = rnd() % 10 == 0 ? (long)(rnd() % 4) : -1;
        if (kind == 0) {
            fail_after = -1;
            gfx_rgn_clear(&reg[d], &mem);
            memset(model[d], 0, sizeof model[d]);
            check(d, "ClearRegion");
        } else if (kind < 5) {
            rand_rect(&rc);
            ok = gfx_rgn_rect(&reg[d], op, &rc, &mem);
            if (ok)
                for (y = 0; y < H; y++)
                    for (x = 0; x < W; x++)
                        model[d][y][x] = (uint8_t)in(op, model[d][y][x], x + X0 >= rc.min_x && x + X0 <= rc.max_x
                                                     && y + Y0 >= rc.min_y && y + Y0 <= rc.max_y);
            check(d, opname[op]);
        } else {
            if (op == GFX_RGN_CLEAR) op = GFX_RGN_OR;
            ok = gfx_rgn_region(&reg[d], op, &reg[s], &mem);
            if (ok)
                for (y = 0; y < H; y++)
                    for (x = 0; x < W; x++) model[d][y][x] = (uint8_t)in(op, model[s][y][x], model[d][y][x]);
            check(d, opname[op]);
        }
        if (kind && !ok) {
            if (fail_after < 0) { printf("FAIL a call failed with memory\n"); fails++; }
            if (reg[d].rects != keep || memcmp(&reg[d].bounds, &kb, sizeof kb) || memcmp(model[d], before, sizeof before)
                || live != l0) {
                if (++fails < 20) printf("FAIL a failed call changed the region\n");
            }
        }
        fail_after = -1;
    }
    for (i = 0; i < 3; i++) gfx_rgn_clear(&reg[i], &mem);
    if (live) { printf("FAIL %ld allocations kept\n", live); fails++; }
    printf("%lu checks, %lu failed\n", checks, fails);
    return fails != 0;
}
C

cat > "$tmp/rt.c" <<'C'
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
int memcmp(const void *a, const void *b, size_t n) { const unsigned char *p = a, *q = b; for (; n--; p++, q++) if (*p != *q) return *p < *q ? -1 : 1; return 0; }
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
C

for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $tmp/rt.c" ;;
      *) rt= ;;
    esac
    "$cc" $cpu $opt -static -Wall -Wextra -Werror -I"$core" -o "$tmp/t" "$tmp/t.c" "$core/gfx_region.c" $rt
    q=m${cpu#-m}
    if out=$("$qemu" -cpu $q "$tmp/t"); then echo "ok   $opt $cpu: $out"; else echo "FAIL $opt $cpu:"; echo "$out"; fail=1; fi
  done
done
exit $fail
