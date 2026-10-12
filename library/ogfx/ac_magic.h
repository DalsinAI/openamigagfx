/* SPDX-License-Identifier: MIT */
/* A copy of amigachrome-guest common/amiga/ac_magic.h (at 90aa66f), the
 * source of truth for the tag and the published ids: OpenGfx's leaves need
 * it, and openamigartg builds on its own. Change the ids there first. */
#ifndef AC_MAGIC_H
#define AC_MAGIC_H
/* Magic functions: a function of ours that runs as host code on AmigaChrome
 * and as its own 68k code everywhere else (amigachrome's
 * docs/architecture/MAGIC_FUNCTIONS_AND_BRIDGE.md, part 1, and
 * machines/cpus/m68k-ac090/native/jit_magic.c).
 *
 * The function's entry is a 12-byte tag, then a jump to its C body:
 *     BRA.S   *+12            ; $600A: a real Amiga branches over the tag
 *     DC.L    'ACMF'
 *     DC.W    $0001           ; the tag's version
 *     DC.L    id              ; who (Dalsin is 1) in the top half, which below
 *     JMP     name_acbody
 * When AC090 translates the entry and knows the id, it runs its native
 * version instead, after checking the call (plain RAM only, no overlap it
 * cannot copy, nothing over the call's own stack frame, and so on); when it
 * declines, or does not know the id, the C body runs. A real Amiga, a
 * PiStorm or another emulator always runs the C body, for the cost of two
 * branches.
 *
 *   #include "ac_magic.h"
 *   AC_MAGIC(ac_memcpy, AC_MAGIC_MEMCPY)
 *   AC_MAGIC_KEEP void *AC_MAGIC_BODY(ac_memcpy)(void *dst, const void *src, size_t n) { ... }
 *
 * Callers call ac_memcpy as declared in their header. The rules:
 * - The body must do what the id's function does below, byte for byte, for
 *   every call: AC090_MAGIC=verify in the runtime runs both and compares.
 * - GCC's m68k-amigaos stack convention: the arguments as 32-bit values on
 *   the stack above the return address, the result in D0, D0/D1/A0/A1
 *   scratch. Not for a -mregparm build or a library's register entry (a
 *   LVO); tag a stack-convention function those call.
 * - Give the body external linkage (not static), so its name is the one
 *   the jump names. AC_MAGIC_KEEP keeps it out of the inliner's hands.
 * - GCC expands small memcpy, memset and strlen calls inline, and those
 *   are not magic. Name the tagged function something of its own (as
 *   ac_memcpy above), or build with -fno-builtin-memcpy and the like.
 * - The tag sits inside a small unused function (its name, name_actag), so
 *   that it goes into the code section whatever section GCC was in; the
 *   jump never returns into it.
 * - The tagged name is defined in assembly, which an LTO object's symbol
 *   table does not list. Linking such an object directly is fine, but in a
 *   static library its index would not offer the name, and a caller's link
 *   fails with an undefined reference. Build a file with tags in it with
 *   -fno-lto when it goes into a static library.
 *
 * The ids are a published list: a PiStorm's Emu68 or an FPGA accelerator
 * may honour the same tags. Dalsin's (1 in the top half) are below; a new
 * one is added here and in jit_magic.c's magic_defs together. */

/* void *memcpy(void *dst, const void *src, size_t n): dst. AC090 declines
 * a call whose destination overlaps the source above it (dst > src and
 * dst - src < n), whose result would depend on the copy's direction. */
#define AC_MAGIC_MEMCPY  0x00010001
/* void *memmove(void *dst, const void *src, size_t n): dst, as if through
 * a buffer. */
#define AC_MAGIC_MEMMOVE 0x00010002
/* void *memset(void *dst, int c, size_t n): dst; the low byte of c. */
#define AC_MAGIC_MEMSET  0x00010003
/* int memcmp(const void *a, const void *b, size_t n): the first differing
 * bytes' difference, as unsigned bytes (a[i] - b[i], -255 to 255), else 0.
 * C only promises the sign; the native version gives this exact value. */
#define AC_MAGIC_MEMCMP  0x00010004
/* size_t strlen(const char *s) */
#define AC_MAGIC_STRLEN  0x00010005
/* unsigned long crc32(unsigned long crc, const unsigned char *buf,
 * unsigned len): zlib's (the reflected polynomial $EDB88320, inverted on
 * the way in and out); 0 when buf is NULL. */
#define AC_MAGIC_CRC32   0x00010006
/* unsigned long adler32(unsigned long adler, const unsigned char *buf,
 * unsigned len): zlib's; 1 when buf is NULL. AC090 leaves a call with
 * either half 65521 or more to the body (zlib never makes one), and one
 * with a NULL buf and len 1, which zlib reads anyway. */
#define AC_MAGIC_ADLER32 0x00010007
/* OpenGfx's leaves (openamigartg library/ogfx/ogfx_leaves.h, SPD-11): each takes
 * one parameter block and returns how much it drew. AC090 leaves a block
 * that reaches anything but plain memory, or whose arithmetic would wrap
 * past 4 GB, to the body. */
/* uint32_t ogfx_planar_rect(const struct ogfx_planar_rect *): RectFill on
 * planar bitmaps, set from the pen or inverted; the planes drawn. */
#define AC_MAGIC_OGFX_PLANAR_RECT 0x00010100
/* uint32_t ogfx_chunky_rect(const struct ogfx_chunky_rect *): RectFill on
 * 1 to 4 bytes a pixel, stored or XORed; the pixels drawn. */
#define AC_MAGIC_OGFX_CHUNKY_RECT 0x00010101
/* uint32_t ogfx_chunky_copy(const struct ogfx_chunky_copy *): a rectangle
 * of 1 to 4 bytes a pixel, each row as memmove, rows from the last up when
 * the destination starts above the source; the pixels copied. */
#define AC_MAGIC_OGFX_CHUNKY_COPY 0x00010102
/* uint32_t ogfx_planar_blit(const struct ogfx_planar_blit *): BltBitMap
 * on standard planar bitmaps, source/destination minterm and plane mask;
 * the destination planes involved. */
#define AC_MAGIC_OGFX_PLANAR_BLIT 0x00010103

#define AC_MAGIC_STR_(x) #x
#define AC_MAGIC_STR(x) AC_MAGIC_STR_(x)
/* the assembler's name for a C name: "_memcpy" for m68k-amigaos */
#ifdef __USER_LABEL_PREFIX__
#define AC_MAGIC_SYM(name) AC_MAGIC_STR(__USER_LABEL_PREFIX__) #name
#else
#define AC_MAGIC_SYM(name) #name
#endif

#define AC_MAGIC_BODY(name) name##_acbody
#define AC_MAGIC_KEEP __attribute__((used, noinline))

#define AC_MAGIC(name, id)                                                   \
    __attribute__((used, noinline, noclone)) static void name##_actag(void) \
    {                                                                        \
        __asm__ volatile("\t.globl\t" AC_MAGIC_SYM(name) "\n"                \
                         AC_MAGIC_SYM(name) ":\n"                            \
                         "\t.short\t0x600a\n"                                \
                         "\t.long\t0x41434d46\n"                             \
                         "\t.short\t0x0001\n"                                \
                         "\t.long\t" AC_MAGIC_STR(id) "\n"                   \
                         "\tjmp\t" AC_MAGIC_SYM(name##_acbody) "\n");        \
    }

#endif
