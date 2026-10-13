#!/bin/sh
# OpenLayers' core (library/layers/layers_core.c) built with an m68k cross
# GCC for the 68000, 68020 and 68040 at -O0 and -O2, each run on that CPU
# under qemu-m68k, against a plain model: a grid of pixels. Random
# rectangle sets, random stacks of layers (some hidden, some off the
# screen) and random moves; every pixel is checked after every call.
#   sh tests/m68k/test_layers_core.sh
# With HOST_CC=cc it also builds and runs on the host first.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
core=$here/../../library/layers
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

cat > "$tmp/t.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "layers_core.h"

/* the grid: coordinates -OFF..G-OFF-1 both ways */
#define G 96
#define OFF 16
static uint32_t seed = 0x1a7e;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static unsigned long checks, fails;

/* a bump arena, emptied between rounds; fail_after > 0 makes the n-th
 * allocation from now fail */
static uint8_t arena[1u << 18];
static uint32_t used;
static long fail_after;
static void *a_alloc(void *ctx, uint32_t size)
{
    void *p;
    (void)ctx;
    if (fail_after > 0 && --fail_after == 0) return 0;
    size = (size + 3) & ~3u;
    if (used + size > sizeof arena) return 0;
    p = arena + used;
    used += size;
    return p;
}
static void a_free(void *ctx, void *p, uint32_t size) { (void)ctx; (void)p; (void)size; }
static const struct lc_mem mem = { a_alloc, a_free, 0 };

static uint8_t ma[G][G], mb[G][G], cover[G][G];

static int32_t coord(void) { return (int32_t)(rnd() % (G - 8)) - OFF + 4; }
static struct lc_rect rrect(void)
{
    struct lc_rect r;
    r.x0 = coord(); r.x1 = r.x0 + (int32_t)(rnd() % 40); if (r.x1 >= G - OFF) r.x1 = G - OFF - 1;
    r.y0 = coord(); r.y1 = r.y0 + (int32_t)(rnd() % 40); if (r.y1 >= G - OFF) r.y1 = G - OFF - 1;
    return r;
}
static void fail(const char *what, int x, int y)
{
    if (++fails < 20) printf("FAIL %s at %d,%d\n", what, x, y);
}
static void mark(uint8_t m[G][G], const struct lc_rect *r, uint8_t v)
{
    int32_t x, y;
    for (y = r->y0; y <= r->y1; y++)
        for (x = r->x0; x <= r->x1; x++) m[y + OFF][x + OFF] = v;
}
/* s is disjoint, inside the grid, and covers exactly the pixels set in m */
static void check_set(const char *what, const struct lc_rects *s, uint8_t m[G][G])
{
    uint32_t i;
    int32_t x, y;
    checks++;
    memset(cover, 0, sizeof cover);
    for (i = 0; i < s->n; i++) {
        const struct lc_rect *r = &s->r[i];
        if (r->x0 > r->x1 || r->y0 > r->y1 || r->x0 < -OFF || r->y0 < -OFF || r->x1 >= G - OFF || r->y1 >= G - OFF) {
            fail(what, r->x0, r->y0);
            return;
        }
        for (y = r->y0; y <= r->y1; y++)
            for (x = r->x0; x <= r->x1; x++)
                if (cover[y + OFF][x + OFF]++) { fail(what, x, y); return; }
    }
    for (y = 0; y < G; y++)
        for (x = 0; x < G; x++)
            if (cover[y][x] != m[y][x]) { fail(what, x - OFF, y - OFF); return; }
}
static void random_set(struct lc_rects *s, uint8_t m[G][G])
{
    uint32_t k, n = rnd() % 7;
    lc_clear(s);
    memset(m, 0, G * G);
    for (k = 0; k < n; k++) {
        struct lc_rect r = rrect();
        lc_or_rect(s, &r);
        mark(m, &r, 1);
    }
}

static void sets(void)
{
    int round;
    struct lc_rects a, b;
    lc_init(&a, &mem);
    lc_init(&b, &mem);
    for (round = 0; round < 400; round++) {
        struct lc_rect r = rrect();
        int32_t x, y, dx, dy;
        used = 0;
        lc_init(&a, &mem);
        lc_init(&b, &mem);
        random_set(&a, ma);
        check_set("or_rect", &a, ma);
        random_set(&b, mb);
        switch (rnd() % 7) {
        case 0:
            lc_sub_rect(&a, &r); mark(ma, &r, 0); check_set("sub_rect", &a, ma); break;
        case 1:
            lc_and_rect(&a, &r);
            for (y = 0; y < G; y++) for (x = 0; x < G; x++)
                if (x - OFF < r.x0 || x - OFF > r.x1 || y - OFF < r.y0 || y - OFF > r.y1) ma[y][x] = 0;
            check_set("and_rect", &a, ma);
            break;
        case 2:
            lc_sub(&a, &b);
            for (y = 0; y < G; y++) for (x = 0; x < G; x++) if (mb[y][x]) ma[y][x] = 0;
            check_set("sub", &a, ma);
            break;
        case 3:
            lc_and(&a, &b);
            for (y = 0; y < G; y++) for (x = 0; x < G; x++) ma[y][x] &= mb[y][x];
            check_set("and", &a, ma);
            break;
        case 4:
            lc_or(&a, &b);
            for (y = 0; y < G; y++) for (x = 0; x < G; x++) ma[y][x] |= mb[y][x];
            check_set("or", &a, ma);
            break;
        case 5: {
            uint32_t area = 0;
            lc_and_rect(&a, &(struct lc_rect){ -OFF + 8, -OFF + 8, G - OFF - 9, G - OFF - 9 });
            for (y = 0; y < G; y++) for (x = 0; x < G; x++)
                if (x < 8 || y < 8 || x > G - 9 || y > G - 9) ma[y][x] = 0;
            dx = (int32_t)(rnd() % 17) - 8; dy = (int32_t)(rnd() % 17) - 8;
            lc_offset(&a, dx, dy);
            memset(mb, 0, sizeof mb);
            for (y = 0; y < G; y++) for (x = 0; x < G; x++)
                if (ma[y][x]) { mb[y + dy][x + dx] = 1; area++; }
            check_set("offset", &a, mb);
            checks++;
            if (lc_area(&a) != area) fail("area", (int)lc_area(&a), (int)area);
            break;
        }
        default: {
            /* memory running out part way: still a disjoint set inside the
             * old one or the answer */
            struct lc_rects c;
            uint32_t i;
            lc_init(&c, &mem);
            lc_copy(&c, &a);
            fail_after = 1 + rnd() % 3;
            if (lc_sub(&a, &b) == 0) {
                for (y = 0; y < G; y++) for (x = 0; x < G; x++) if (mb[y][x]) ma[y][x] = 0;
                check_set("sub (memory)", &a, ma);
            } else {
                memset(cover, 0, sizeof cover);
                checks++;
                for (i = 0; i < a.n; i++) {
                    const struct lc_rect *q = &a.r[i];
                    for (y = q->y0; y <= q->y1; y++) for (x = q->x0; x <= q->x1; x++)
                        if (cover[y + OFF][x + OFF]++ || !ma[y + OFF][x + OFF]) { fail("sub (no memory)", x, y); y = q->y1 + 1; break; }
                }
            }
            fail_after = 0;
        }
        }
    }
}

static struct lc_layer st[8];
static uint32_t nst;
static void random_stack(void)
{
    uint32_t k;
    nst = 1 + rnd() % 8;
    for (k = 0; k < nst; k++) {
        st[k].bounds = rrect();
        st[k].flags = rnd() % 6 == 0 ? LC_HIDDEN : 0;
    }
}
static int inside(const struct lc_rect *r, int32_t x, int32_t y) { return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1; }
/* what the piece at (x, y) of layer idx must say */
static int32_t want(uint32_t idx, const struct lc_rect *screen, int32_t x, int32_t y)
{
    uint32_t k;
    if (screen && !inside(screen, x, y)) return LC_OFFSCREEN;
    for (k = 0; k < idx; k++)
        if (!(st[k].flags & LC_HIDDEN) && inside(&st[k].bounds, x, y)) return (int32_t)k;
    return LC_VISIBLE;
}
static void check_order(const char *what, const struct lc_rect *r, uint32_t n, int32_t dx, int32_t dy)
{
    uint32_t i, j;
    checks++;
    for (i = 0; i < n; i++) {
        struct lc_rect d = r[i];
        d.x0 += dx; d.x1 += dx; d.y0 += dy; d.y1 += dy;
        for (j = i + 1; j < n; j++)
            if (lc_meet(&d, &r[j], 0)) { fail(what, (int)i, (int)j); return; }
    }
}

static void layers(void)
{
    int round;
    static struct lc_rect order[256];
    for (round = 0; round < 600; round++) {
        struct lc_pieces p;
        struct lc_rects v;
        struct lc_rect scr, *screen = 0;
        uint32_t idx, i;
        int32_t x, y;
        used = 0;
        random_stack();
        if (rnd() % 4) { scr = rrect(); screen = &scr; }
        lc_pieces_init(&p, &mem);
        lc_init(&v, &mem);
        for (idx = 0; idx < nst; idx++) {
            const struct lc_rect *b = &st[idx].bounds;
            int bad = 0;
            if (lc_layer_pieces(st, nst, idx, screen, &p)) { fail("pieces memory", 0, 0); continue; }
            checks++;
            memset(cover, 0, sizeof cover);
            for (i = 0; i < p.n && !bad; i++) {
                const struct lc_rect *r = &p.p[i].r;
                if (r->x0 > r->x1 || r->y0 > r->y1 || r->x0 < b->x0 || r->y0 < b->y0 || r->x1 > b->x1 || r->y1 > b->y1) {
                    fail("piece outside its layer", r->x0, r->y0); bad = 1; break;
                }
                for (y = r->y0; y <= r->y1 && !bad; y++)
                    for (x = r->x0; x <= r->x1; x++) {
                        if (cover[y + OFF][x + OFF]++) { fail("pieces overlap", x, y); bad = 1; break; }
                        if (want(idx, screen, x, y) != p.p[i].obscurer) { fail("piece says the wrong layer", x, y); bad = 1; break; }
                    }
            }
            for (y = b->y0; y <= b->y1 && !bad; y++)
                for (x = b->x0; x <= b->x1; x++)
                    if (!cover[y + OFF][x + OFF]) { fail("pieces leave a hole", x, y); bad = 1; break; }
            /* the visible part */
            lc_layer_visible(st, nst, idx, screen, &v);
            memset(ma, 0, sizeof ma);
            for (y = b->y0; y <= b->y1; y++)
                for (x = b->x0; x <= b->x1; x++) ma[y + OFF][x + OFF] = want(idx, screen, x, y) == LC_VISIBLE;
            check_set("visible", &v, ma);
            /* copy order for its pieces moved by a random offset */
            if (p.n <= 256) {
                int32_t dx = (int32_t)(rnd() % 41) - 20, dy = (int32_t)(rnd() % 41) - 20;
                if (rnd() % 3 == 0) dy = 0; else if (rnd() % 3 == 0) dx = 0;
                for (i = 0; i < p.n; i++) order[i] = p.p[i].r;
                checks++;
                if (lc_sort_for_move(order, p.n, dx, dy)) fail("sort stuck", dx, dy);
                check_order("sort order", order, p.n, dx, dy);
            }
        }
        /* behind every layer */
        if (screen) {
            lc_uncovered(st, nst, screen, &v);
            memset(ma, 0, sizeof ma);
            for (y = scr.y0; y <= scr.y1; y++)
                for (x = scr.x0; x <= scr.x1; x++) {
                    uint32_t k;
                    for (k = 0; k < nst; k++) if (!(st[k].flags & LC_HIDDEN) && inside(&st[k].bounds, x, y)) break;
                    ma[y + OFF][x + OFF] = k == nst;
                }
            check_set("uncovered", &v, ma);
        }
        /* WhichLayer */
        for (i = 0; i < 20; i++) {
            int32_t w;
            uint32_t k;
            x = coord(); y = coord();
            for (k = 0; k < nst; k++) if (!(st[k].flags & LC_HIDDEN) && inside(&st[k].bounds, x, y)) break;
            w = lc_which(st, nst, x, y);
            checks++;
            if (w != (k == nst ? -1 : (int32_t)k)) fail("which", x, y);
        }
    }
}

/* the copy order for sets of many small rectangles */
static void orders(void)
{
    int round;
    static struct lc_rect r[256];
    for (round = 0; round < 300; round++) {
        struct lc_rects a;
        uint32_t i, n;
        int32_t dx = (int32_t)(rnd() % 61) - 30, dy = (int32_t)(rnd() % 61) - 30;
        used = 0;
        lc_init(&a, &mem);
        random_set(&a, ma);
        { struct lc_rects b; lc_init(&b, &mem); random_set(&b, mb); lc_sub(&a, &b); }
        n = a.n < 256 ? a.n : 256;
        for (i = 0; i < n; i++) r[i] = a.r[i];
        checks++;
        if (lc_sort_for_move(r, n, dx, dy)) fail("sort stuck (set)", dx, dy);
        check_order("sort order (set)", r, n, dx, dy);
    }
}

int main(void)
{
    sets();
    layers();
    orders();
    if (fails) { printf("%lu of %lu checks failed\n", fails, checks); return 1; }
    printf("%lu checks\n", checks);
    return 0;
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

if [ -n "${HOST_CC:-}" ]; then
  "$HOST_CC" -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$core" -o "$tmp/h" "$tmp/t.c" "$core/layers_core.c"
  if out=$("$tmp/h"); then echo "ok   host: $out"; else echo "FAIL host:"; echo "$out"; fail=1; fi
fi
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit $fail; }
for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $tmp/rt.c" ;;
      *) rt= ;;
    esac
    "$cc" $cpu $opt -static -Wall -Wextra -Werror -I"$core" -o "$tmp/t" "$tmp/t.c" "$core/layers_core.c" $rt
    q=m${cpu#-m}
    if out=$("$qemu" -cpu $q "$tmp/t"); then echo "ok   $opt $cpu: $out"; else echo "FAIL $opt $cpu:"; echo "$out"; fail=1; fi
  done
done
exit $fail
