/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A patch's entry, 18 bytes of 68k code in opengpu.library's base:
 *     MOVE.L  A4,-(SP)
 *     MOVEA.L #state,A4
 *     JSR     entry
 *     MOVEA.L (SP)+,A4
 *     RTS
 * graphics.library calls a patch with its own base in A6, so this puts
 * OpenGfx's state in A4 for the C. A4 is the caller's to keep, so it goes
 * back; every argument register and D0's result pass through untouched.
 * The caller clears the caches before the entry runs. tests/ogfx/run.sh
 * disassembles what this writes. */
#ifndef OPENGPU_OGFX_TRAMP_H
#define OPENGPU_OGFX_TRAMP_H

#define OGFX_TRAMP_WORDS 10

static inline void tramp_write(UWORD *t, ULONG state, ULONG entry)
{
    t[0] = 0x2F0C;                                  /* MOVE.L  A4,-(SP) */
    t[1] = 0x287C; t[2] = (UWORD)(state >> 16); t[3] = (UWORD)state;   /* MOVEA.L #state,A4 */
    t[4] = 0x4EB9; t[5] = (UWORD)(entry >> 16); t[6] = (UWORD)entry;   /* JSR     entry */
    t[7] = 0x285F;                                  /* MOVEA.L (SP)+,A4 */
    t[8] = 0x4E75;                                  /* RTS */
    t[9] = 0x4E71;                                  /* NOP: padding, never reached */
}

#endif
