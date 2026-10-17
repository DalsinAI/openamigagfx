#!/bin/sh
# OpenGfx's core (library/core/gfx_core.c) built with an m68k cross GCC for
# the 68040 at -O0 and -O2, each run on that CPU under
# qemu-m68k, against a plain model: an array of pens drawn one pixel at a
# time. Random bitmaps
# (depth 1 to 8, some planes 0 or -1, odd sizes), pens, masks and
# rectangles; every pixel of every plane is compared after every call.
#   sh tests/m68k/test_gfx_core.sh
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
#include "gfx_core.h"
#define W 208
#define H 40
static uint32_t seed = 0x1200;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static uint8_t mem[8][32 * H], chunk[W * H], model[H][W], old[H][W], buf[W * H];
static unsigned long checks, fails;
static struct gfx_planar bm;
static struct gfx_chunky8 cb;
static int width;

/* what the model holds where a plane is 0 or -1 */
static uint32_t fixed(uint32_t pen)
{
    uint32_t p;
    for (p = 0; p < bm.depth; p++) {
        if (!bm.planes[p]) pen &= ~(1u << p);
        else if (bm.planes[p] == (uint8_t *)-1) pen |= 1u << p;
    }
    return pen;
}
static uint32_t live(uint32_t mask)
{
    uint32_t p, m = 0;
    for (p = 0; p < bm.depth; p++)
        if (mask >> p & 1 && bm.planes[p] && bm.planes[p] != (uint8_t *)-1) m |= 1u << p;
    return m;
}
static void mput(int x, int y, uint32_t pen, uint32_t mask, int mode)
{
    uint32_t m = live(mask);
    model[y][x] = (uint8_t)(mode == GFX_INVERT ? model[y][x] ^ m : (model[y][x] & ~m) | (pen & m));
}
static void check(const char *what)
{
    int x, y;
    checks++;
    for (y = 0; y < H; y++)
        for (x = 0; x < width; x++)
            if (gfx_planar_get(&bm, x, y) != model[y][x]) {
                if (++fails < 20) printf("FAIL %s depth %lu at %d,%d: %lu, want %u\n", what,
                                         (unsigned long)bm.depth, x, y, (unsigned long)gfx_planar_get(&bm, x, y), model[y][x]);
                return;
            }
}
static void setup(void)
{
    uint32_t p, x, y;
    bm.depth = 1 + rnd() % 8;
    bm.bytes_per_row = 2 * (1 + rnd() % 13);  /* even, as graphics.library's */
    bm.rows = H;
    width = bm.bytes_per_row * 8;
    for (p = 0; p < 8; p++) {
        uint32_t r = rnd() % 10;
        bm.planes[p] = r == 0 ? 0 : r == 1 ? (uint8_t *)-1 : mem[p];
        for (x = 0; x < sizeof mem[p]; x++) mem[p][x] = (uint8_t)rnd();
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < (uint32_t)width; x++) model[y][x] = (uint8_t)gfx_planar_get(&bm, x, y);
}
static void rect(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = rnd() % width; *x1 = *x0 + rnd() % (width - *x0);
    *y0 = rnd() % H; *y1 = *y0 + rnd() % (H - *y0);
}
int main(void)
{
    int round, k, x, y, x0, y0, x1, y1;
    for (round = 0; round < 300; round++) {
        setup();
        for (k = 0; k < 12; k++) {
            uint32_t pen = rnd() & 0xFF, mask = rnd() % 4 ? 0xFF : rnd() & 0xFF;
            int mode = rnd() % 3 == 0 ? GFX_INVERT : GFX_SET;
            switch (rnd() % 6) {
            case 5: {
                /* BltBitMap's copy within the bitmap, overlapping or not */
                uint32_t mt = rnd() & 0xF0, live = 0, p;
                int w = 1 + rnd() % width, h = 1 + rnd() % H, sx, sy, dx, dy;
                if (rnd() % 3 == 0) mt = 0xC0;
                if (w > width) w = width;
                sx = rnd() % (width - w + 1); dx = rnd() % (width - w + 1);
                sy = rnd() % (H - h + 1); dy = rnd() % (H - h + 1);
                if (rnd() % 2) {
                    dx = sx + (int)(rnd() % 9) - 4;
                    dy = sy + (int)(rnd() % 3) - 1;
                    dx = dx < 0 ? 0 : dx + w > width ? width - w : dx;
                    dy = dy < 0 ? 0 : dy + h > H ? H - h : dy;
                }
                memcpy(old, model, sizeof old);
                gfx_planar_blit(&bm, sx, sy, &bm, dx, dy, w, h, mt, mask);
                for (p = 0; p < bm.depth; p++) if (mask >> p & 1 && bm.planes[p] && bm.planes[p] != (uint8_t *)-1) live |= 1u << p;
                for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
                    uint32_t b = old[sy + y][sx + x], c = old[dy + y][dx + x], d = c;
                    for (p = 0; p < 8; p++) {
                        int bb = b >> p & 1, cc = c >> p & 1, bit = mt >> (bb ? (cc ? 7 : 6) : (cc ? 5 : 4)) & 1;
                        if (live >> p & 1) d = (d & ~(1u << p)) | ((uint32_t)bit << p);
                    }
                    model[dy + y][dx + x] = (uint8_t)d;
                }
                check("blit");
                break;
            }
            case 4: {
                uint32_t pat = rnd() % 4 ? rnd() & 0xFFFF : 0xFFFF, bg = rnd() & 0xFF;
                int sm = rnd() % 3;
                x0 = rnd() % width; x1 = x0 + rnd() % (width - x0); y = rnd() % H;
                gfx_planar_span(&bm, x0, x1, y, pat, pen, bg, mask, sm);
                for (x = x0; x <= x1; x++) {
                    int on = pat >> (15 - (x & 15)) & 1;
                    if (sm == GFX_XOR) { if (on) mput(x, y, 0, mask, GFX_INVERT); }
                    else if (on) mput(x, y, pen, mask, GFX_SET);
                    else if (sm == GFX_JAM2) mput(x, y, bg, mask, GFX_SET);
                }
                check("span");
                break;
            }
            case 0:
                x = rnd() % width; y = rnd() % H;
                gfx_planar_put(&bm, x, y, pen, mask, mode); mput(x, y, pen, mask, mode); check("put");
                break;
            case 1:
                gfx_planar_fill_all(&bm, pen, mask);
                for (y = 0; y < H; y++) for (x = 0; x < width; x++) mput(x, y, pen, mask, GFX_SET);
                check("fill_all");
                break;
            case 2: {
                uint32_t sbpr;
                rect(&x0, &y0, &x1, &y1);
                sbpr = (x1 - x0 + 1) + rnd() % 5;
                for (x = 0; x < (int)sizeof buf; x++) buf[x] = (uint8_t)rnd();
                gfx_planar_write8(&bm, x0, y0, x1, y1, buf, sbpr, mask);
                for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) mput(x, y, buf[(y - y0) * sbpr + x - x0], mask, GFX_SET);
                check("write8");
                break;
            }
            default: {
                uint32_t dbpr;
                rect(&x0, &y0, &x1, &y1);
                dbpr = (x1 - x0 + 1) + rnd() % 5;
                memset(buf, 0xA5, sizeof buf);
                gfx_planar_read8(&bm, x0, y0, x1, y1, buf, dbpr);
                checks++;
                for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++)
                    if (buf[(y - y0) * dbpr + x - x0] != fixed(model[y][x])) {
                        if (++fails < 20) printf("FAIL read8 at %d,%d\n", x, y);
                        y = y1 + 1; break;
                    }
            }
            }
        }
    }
    /* chunky */
    cb.base = chunk; cb.bytes_per_row = W; cb.rows = H; width = W;
    for (x = 0; x < W * H; x++) chunk[x] = model[x / W][x % W] = (uint8_t)rnd();
    for (k = 0; k < 3000; k++) {
        uint32_t pen = rnd() & 0xFF, mask = rnd() % 4 ? 0xFF : rnd() & 0xFF, sbpr;
        int mode = rnd() % 3 == 0 ? GFX_INVERT : GFX_SET;
        uint8_t m = (uint8_t)mask;
        switch (rnd() % 5) {
        case 4: {
            uint32_t pat = rnd() & 0xFFFF, bg = rnd() & 0xFF;
            int sm = rnd() % 3;
            x0 = rnd() % W; x1 = x0 + rnd() % (W - x0); y = rnd() % H;
            gfx_chunky8_span(&cb, x0, x1, y, pat, pen, bg, mask, sm);
            for (x = x0; x <= x1; x++) {
                int on = pat >> (15 - (x & 15)) & 1;
                if (sm == GFX_XOR) { if (on) model[y][x] ^= m; }
                else if (on) model[y][x] = (uint8_t)((model[y][x] & ~m) | (pen & m));
                else if (sm == GFX_JAM2) model[y][x] = (uint8_t)((model[y][x] & ~m) | (bg & m));
            }
            break;
        }
        case 0:
            x = rnd() % W; y = rnd() % H;
            gfx_chunky8_put(&cb, x, y, pen, mask, mode);
            model[y][x] = (uint8_t)(mode == GFX_INVERT ? model[y][x] ^ m : (model[y][x] & ~m) | (pen & m));
            break;
        case 1:
            if (rnd() % 8) break;
            gfx_chunky8_fill_all(&cb, pen, mask);
            for (y = 0; y < H; y++) for (x = 0; x < W; x++) model[y][x] = (uint8_t)((model[y][x] & ~m) | (pen & m));
            break;
        case 2:
            rect(&x0, &y0, &x1, &y1); sbpr = (x1 - x0 + 1) + rnd() % 5;
            for (x = 0; x < (int)sizeof buf; x++) buf[x] = (uint8_t)rnd();
            gfx_chunky8_write8(&cb, x0, y0, x1, y1, buf, sbpr, mask);
            for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++)
                model[y][x] = (uint8_t)((model[y][x] & ~m) | (buf[(y - y0) * sbpr + x - x0] & m));
            break;
        default:
            rect(&x0, &y0, &x1, &y1); sbpr = (x1 - x0 + 1) + rnd() % 5;
            gfx_chunky8_read8(&cb, x0, y0, x1, y1, buf, sbpr);
            for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++)
                if (buf[(y - y0) * sbpr + x - x0] != model[y][x]) { fails++; y = y1 + 1; break; }
        }
        checks++;
        for (y = 0; y < H; y++) for (x = 0; x < W; x++)
            if (gfx_chunky8_get(&cb, x, y) != model[y][x]) { if (++fails < 20) printf("FAIL chunky at %d,%d\n", x, y); y = H; break; }
    }
    {   /* an interleaved bitmap (BMF_INTERLEAVED): three planes 4 bytes a
         * row each, 12 bytes a row; fill_all keeps to each plane's part */
        static uint8_t il[12 * 5 + 4];
        struct gfx_planar ib;
        uint32_t p;
        memset(il, 0xA5, sizeof il);
        ib.depth = 3;
        ib.bytes_per_row = 12;
        ib.rows = 5;
        ib.interleaved = 0;
        for (p = 0; p < 8; p++) ib.planes[p] = p < 3 ? il + 4 * p : 0;
        gfx_planar_fill_all(&ib, 5, 0xFF);
        checks++;
        for (y = 0; y < 5; y++) for (x = 0; x < 32; x++)
            if (gfx_planar_get(&ib, x, y) != 5) { if (++fails < 20) printf("FAIL interleaved at %d,%d\n", x, y); y = 5; break; }
        checks++;
        for (x = 60; x < 64; x++)
            if (il[x] != 0xA5) { if (++fails < 20) printf("FAIL interleaved past the end\n"); break; }
        /* planes 0 and 1 constant (0, -1): plane 2 alone still keeps to
         * its part, told by the flag, and by plane 2 and 3 when there */
        for (p = 0; p < 2; p++) {
            memset(il, 0xA5, sizeof il);
            ib.depth = 3 + p;
            ib.bytes_per_row = 4 * ib.depth;
            ib.rows = (sizeof il - 4) / ib.bytes_per_row;
            ib.planes[0] = 0;
            ib.planes[1] = (uint8_t *)-1;
            ib.planes[2] = il + 8;
            ib.planes[3] = p ? il + 12 : 0;
            ib.interleaved = !p;
            gfx_planar_fill_all(&ib, 0, 0xFF);
            checks++;
            for (x = 0; x < (int)sizeof il; x++) {
                int part = x < (int)(ib.rows * ib.bytes_per_row) ? (int)(x % ib.bytes_per_row) / 4 : -1;
                if (il[x] != (part >= 2 ? 0x00 : 0xA5)) {
                    if (++fails < 20) printf("FAIL interleaved, constant planes 0 and 1 (%lu planes), byte %d\n", (unsigned long)ib.depth, x);
                    break;
                }
            }
        }
    }
    if (fails) { printf("%lu of %lu checks failed\n", fails, checks); return 1; }
    printf("%lu checks\n", checks);
    return 0;
}
C


for cpu in -m68040; do
  for opt in -O0 -O2; do
    "$cc" $cpu $opt -static -Wall -Wextra -Werror -I"$core" -o "$tmp/t" "$tmp/t.c" "$core/gfx_core.c"
    q=m${cpu#-m}
    if out=$("$qemu" -cpu $q "$tmp/t"); then echo "ok   $opt $cpu: $out"; else echo "FAIL $opt $cpu:"; echo "$out"; fail=1; fi
  done
done
exit $fail
