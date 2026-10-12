/* SPDX-License-Identifier: MIT */
#ifndef OGFX_COMPOSITE_H
#define OGFX_COMPOSITE_H

#include <stdint.h>

/*
 * OpenGfx premultiplied compositing reference.
 *
 * ARGB32 pixels are four bytes in memory in A,R,G,B order. RGB channels are
 * premultiplied by A. These plain-C functions define the byte-exact result
 * future AC090/OpenGPU implementations must reproduce.
 *
 * Source and destination rectangles must not partially overlap. Exact
 * src==dst is valid. Strides are bytes per row and must cover width pixels.
 * Returns the number of destination pixels processed, or 0 for an invalid
 * request.
 */
uint32_t ogfx_argb32_over(uint8_t *dst, uint32_t dst_stride,
                          const uint8_t *src, uint32_t src_stride,
                          uint32_t width, uint32_t height);

uint32_t ogfx_argb32_over_a8(uint8_t *dst, uint32_t dst_stride,
                             const uint8_t *src, uint32_t src_stride,
                             const uint8_t *mask, uint32_t mask_stride,
                             uint32_t width, uint32_t height);

uint32_t ogfx_argb32_solid_over_a8(uint8_t *dst, uint32_t dst_stride,
                                   uint32_t premul_argb,
                                   const uint8_t *mask, uint32_t mask_stride,
                                   uint32_t width, uint32_t height);

#endif
