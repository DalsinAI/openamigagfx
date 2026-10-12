/* SPDX-License-Identifier: MIT */
/*
 * OpenGfx premultiplied ARGB reference compositing.
 * Copyright (c) 2026 Dalsin Limited.
 *
 * No tables, SIMD or host assumptions here: this is the portable 68k
 * correctness path. Accelerated providers are checked against these bytes.
 */
#include "ogfx_composite.h"

static uint8_t div255(uint32_t x)
{
    x += 128u;
    return (uint8_t)((x + (x >> 8)) >> 8);
}

static uint8_t mul255(uint8_t a, uint8_t b)
{
    return div255((uint32_t)a * (uint32_t)b);
}

static uint8_t add_sat(uint8_t a, uint8_t b)
{
    uint32_t x = (uint32_t)a + (uint32_t)b;
    return (uint8_t)(x > 255u ? 255u : x);
}

static void over_pixel(uint8_t *d, const uint8_t *s)
{
    uint8_t inv = (uint8_t)(255u - s[0]);
    d[0] = add_sat(s[0], mul255(d[0], inv));
    d[1] = add_sat(s[1], mul255(d[1], inv));
    d[2] = add_sat(s[2], mul255(d[2], inv));
    d[3] = add_sat(s[3], mul255(d[3], inv));
}

static int request_ok(const uint8_t *dst, uint32_t dst_stride,
                      const uint8_t *src, uint32_t src_stride,
                      const uint8_t *mask, uint32_t mask_stride,
                      uint32_t width, uint32_t height)
{
    uint32_t rowbytes;
    if (!dst || !src || !width || !height || width > UINT32_MAX / 4u)
        return 0;
    rowbytes = width * 4u;
    if (dst_stride < rowbytes || src_stride < rowbytes)
        return 0;
    if (mask && mask_stride < width)
        return 0;
    return 1;
}

uint32_t ogfx_argb32_over(uint8_t *dst, uint32_t dst_stride,
                          const uint8_t *src, uint32_t src_stride,
                          uint32_t width, uint32_t height)
{
    uint32_t y, x;
    if (!request_ok(dst, dst_stride, src, src_stride, 0, 0, width, height))
        return 0;

    for (y = 0; y < height; ++y) {
        uint8_t *d = dst + y * dst_stride;
        const uint8_t *s = src + y * src_stride;
        for (x = 0; x < width; ++x, d += 4, s += 4)
            over_pixel(d, s);
    }
    return width * height;
}

uint32_t ogfx_argb32_over_a8(uint8_t *dst, uint32_t dst_stride,
                             const uint8_t *src, uint32_t src_stride,
                             const uint8_t *mask, uint32_t mask_stride,
                             uint32_t width, uint32_t height)
{
    uint32_t y, x;
    if (!mask ||
        !request_ok(dst, dst_stride, src, src_stride, mask, mask_stride,
                    width, height))
        return 0;

    for (y = 0; y < height; ++y) {
        uint8_t *d = dst + y * dst_stride;
        const uint8_t *s = src + y * src_stride;
        const uint8_t *m = mask + y * mask_stride;
        for (x = 0; x < width; ++x, d += 4, s += 4) {
            uint8_t q[4], a = m[x];
            if (!a)
                continue;
            q[0] = mul255(s[0], a);
            q[1] = mul255(s[1], a);
            q[2] = mul255(s[2], a);
            q[3] = mul255(s[3], a);
            over_pixel(d, q);
        }
    }
    return width * height;
}

uint32_t ogfx_argb32_solid_over_a8(uint8_t *dst, uint32_t dst_stride,
                                   uint32_t premul_argb,
                                   const uint8_t *mask, uint32_t mask_stride,
                                   uint32_t width, uint32_t height)
{
    uint32_t y, x;
    uint8_t colour[4];
    if (!dst || !mask || !width || !height || width > UINT32_MAX / 4u ||
        dst_stride < width * 4u || mask_stride < width)
        return 0;

    colour[0] = (uint8_t)(premul_argb >> 24);
    colour[1] = (uint8_t)(premul_argb >> 16);
    colour[2] = (uint8_t)(premul_argb >> 8);
    colour[3] = (uint8_t)premul_argb;

    for (y = 0; y < height; ++y) {
        uint8_t *d = dst + y * dst_stride;
        const uint8_t *m = mask + y * mask_stride;
        for (x = 0; x < width; ++x, d += 4) {
            uint8_t q[4], a = m[x];
            if (!a)
                continue;
            q[0] = mul255(colour[0], a);
            q[1] = mul255(colour[1], a);
            q[2] = mul255(colour[2], a);
            q[3] = mul255(colour[3], a);
            over_pixel(d, q);
        }
    }
    return width * height;
}
