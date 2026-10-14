/* SPDX-License-Identifier: MIT */
/* OpenLayers: exec's base without a global. Copyright (c) 2026 Dalsin
 * Limited. MIT.
 *
 * Included after <proto/exec.h>, so the exec calls' inline macros read
 * SysBase through this. The hosted tests' exec calls are plain functions
 * and never read it. */
#ifndef OPENLAYERS_SYSBASE_H
#define OPENLAYERS_SYSBASE_H

struct ExecBase;

/* No writable globals, so the module can run from ROM: exec's base is read
 * from address 4 (AbsExecBase) each time, through a register, as gcc 16's
 * -Warray-bounds wants for a pointer made from a constant */
static inline struct ExecBase *t5_sysbase(void)
{ struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); return *p; }
#define SysBase (t5_sysbase())

#endif
