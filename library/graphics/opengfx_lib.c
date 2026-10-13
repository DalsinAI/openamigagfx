/* SPDX-License-Identifier: MIT */
/* OpenGfx: an open graphics.library for AmigaOS 3.2, API and ABI
 * compatible. Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Clean-room: written from graphics.library's autodocs, the NDK 3.2
 * headers' published layouts and OS 3.2's answers measured by the ABI
 * tests, never from Kickstart's code or bytes. This file is the library
 * around the four areas (OWNERS.md): the RomTag, the library base (the
 * public GfxBase, then the env and each area's state: the library has no
 * .data or .bss), the env made from exec, utility.library and the custom
 * chips, and the vector table in graphics.library's LVO order, which
 * gfx_vectors.h (made by gen_stubs.py from graphics.lvo) lists.
 *
 * LIBNAME is "graphics.library" for the replacement and
 * "opengfx.library" for the side-by-side build the ABI tests open next to
 * OS 3.2's own. graphics.library is in ROM, so the replacement goes in at
 * boot as a resident module, never from LIBS:. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "gfx_env.h"
#include "gfx_vectors.h"

#define REG(r, decl) register decl __asm(#r)

#ifndef LIBNAME
#define LIBNAME "graphics.library"
#endif
#define LIB_VERSION  47
#define LIB_REVISION 100                /* 47.100 and up: OpenGfx */

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = LIBNAME;
static const char lib_id[] = "graphics.library 47.100 (13.10.2026) OpenGfx, Dalsin Limited\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: graphics.library 47.100 (13.10.2026) OpenGfx, Dalsin Limited";
static const char util_name[] = "utility.library";

/* ---- the env: ctx is the library base -------------------------------- */

#define GB(ctx) ((struct gfx_base *)(ctx))
#define SYS(ctx) struct ExecBase *SysBase = (struct ExecBase *)GB(ctx)->execbase
#define UTIL(ctx) struct Library *UtilityBase = (struct Library *)GB(ctx)->utilbase

static void *e_alloc(void *ctx, uint32_t size, uint32_t flags) { SYS(ctx); return AllocMem(size, flags); }
static void e_free(void *ctx, void *p, uint32_t size) { SYS(ctx); FreeMem(p, size); }
static uint32_t e_avail(void *ctx, uint32_t flags) { SYS(ctx); return AvailMem(flags); }
static void e_forbid(void *ctx) { SYS(ctx); Forbid(); }
static void e_permit(void *ctx) { SYS(ctx); Permit(); }
static void e_disable(void *ctx) { SYS(ctx); Disable(); }
static void e_enable(void *ctx) { SYS(ctx); Enable(); }
static void e_sem_init(void *ctx, struct gfx_semaphore *s) { SYS(ctx); InitSemaphore((struct SignalSemaphore *)s); }
static void e_sem_obtain(void *ctx, struct gfx_semaphore *s) { SYS(ctx); ObtainSemaphore((struct SignalSemaphore *)s); }
static void e_sem_obtain_shared(void *ctx, struct gfx_semaphore *s)
{
    SYS(ctx);
    ObtainSemaphoreShared((struct SignalSemaphore *)s);
}
static int32_t e_sem_attempt(void *ctx, struct gfx_semaphore *s)
{
    SYS(ctx);
    return AttemptSemaphore((struct SignalSemaphore *)s) ? 1 : 0;
}
static void e_sem_release(void *ctx, struct gfx_semaphore *s) { SYS(ctx); ReleaseSemaphore((struct SignalSemaphore *)s); }
static void *e_find_task(void *ctx) { SYS(ctx); return FindTask(NULL); }
static uint32_t e_wait(void *ctx, uint32_t sigs) { SYS(ctx); return Wait(sigs); }
static void e_signal(void *ctx, void *task, uint32_t sigs) { SYS(ctx); Signal((struct Task *)task, sigs); }
static int32_t e_alloc_signal(void *ctx, int32_t n) { SYS(ctx); return AllocSignal(n); }
static void e_free_signal(void *ctx, int32_t n) { SYS(ctx); FreeSignal(n); }
static void e_reply_msg(void *ctx, void *msg) { SYS(ctx); ReplyMsg((struct Message *)msg); }
static void e_add_int(void *ctx, int32_t n, struct gfx_interrupt *is) { SYS(ctx); AddIntServer(n, (struct Interrupt *)is); }
static void e_rem_int(void *ctx, int32_t n, struct gfx_interrupt *is) { SYS(ctx); RemIntServer(n, (struct Interrupt *)is); }
static struct gfx_interrupt *e_set_int_vector(void *ctx, int32_t n, struct gfx_interrupt *is)
{
    SYS(ctx);
    return (struct gfx_interrupt *)SetIntVector(n, (struct Interrupt *)is);
}
static uint32_t e_type_of_mem(void *ctx, const void *p) { SYS(ctx); return TypeOfMem((APTR)p); }

static uint16_t e_chip_read(void *ctx, uint32_t off)
{
    (void)ctx;
    return *(volatile UWORD *)(0xDFF000UL + off);
}
static void e_chip_write(void *ctx, uint32_t off, uint16_t v)
{
    (void)ctx;
    *(volatile UWORD *)(0xDFF000UL + off) = v;
}

static struct gfx_tagitem *e_next_tag(void *ctx, const struct gfx_tagitem **state)
{
    UTIL(ctx);
    return (struct gfx_tagitem *)NextTagItem((struct TagItem **)state);
}
static struct gfx_tagitem *e_find_tag(void *ctx, uint32_t tag, const struct gfx_tagitem *tags)
{
    UTIL(ctx);
    return (struct gfx_tagitem *)FindTagItem(tag, (struct TagItem *)tags);
}
static uint32_t e_get_tag_data(void *ctx, uint32_t tag, uint32_t def, const struct gfx_tagitem *tags)
{
    UTIL(ctx);
    return GetTagData(tag, def, (struct TagItem *)tags);
}
static struct gfx_tagitem *e_clone_tags(void *ctx, const struct gfx_tagitem *tags)
{
    UTIL(ctx);
    return (struct gfx_tagitem *)CloneTagItems((struct TagItem *)tags);
}
static void e_free_tags(void *ctx, struct gfx_tagitem *tags) { UTIL(ctx); FreeTagItems((struct TagItem *)tags); }
static uint32_t e_call_hook(void *ctx, void *hook, void *obj, void *msg)
{
    UTIL(ctx);
    return CallHookPkt((struct Hook *)hook, obj, msg);
}

static void new_list(struct gfx_list *l, UBYTE type)
{
    l->head = (struct gfx_node *)&l->tail;
    l->tail = 0;
    l->tailpred = (struct gfx_node *)&l->head;
    l->type = type;
}

/* ---- Library housekeeping -------------------------------------------- */

static void areas_exit(struct gfx_env *env, int upto)
{
    if (upto > 3) gfx_exit_gels(env);
    if (upto > 2) gfx_exit_text(env);
    if (upto > 1) gfx_exit_display(env);
    if (upto > 0) gfx_exit_face(env);
}

static struct Library *lib_init(REG(d0, struct gfx_base *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    struct ExecBase *SysBase = sys;
    struct gfx_private *pv = gfx_private_of(base);
    struct gfx_env *env = &pv->env;
    struct Library *lib = (struct Library *)base;
    int n = 0;

    lib->lib_Node.ln_Type = NT_LIBRARY;
    lib->lib_Node.ln_Name = (char *)lib_name;
    lib->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    lib->lib_Version = LIB_VERSION;
    lib->lib_Revision = LIB_REVISION;
    lib->lib_IdString = (APTR)lib_id;
    base->execbase = (uint32_t *)sys;
    pv->reserved[0] = (uint32_t)seglist;
    if (!(base->utilbase = (uint32_t *)OpenLibrary((CONST_STRPTR)util_name, 39))) goto fail;
    new_list(&base->textfonts, NT_FONT);
    new_list(&base->blitwaitq, 0);
    new_list(&base->tof_waitq, 0);
    new_list(&base->monitorlist, 0);
    base->memtype = 0;          /* ABI test: OS 3.2's MemType */

    env->ctx = base;
    env->base = base;
    env->alloc = e_alloc; env->free = e_free; env->avail = e_avail;
    env->forbid = e_forbid; env->permit = e_permit; env->disable = e_disable; env->enable = e_enable;
    env->sem_init = e_sem_init; env->sem_obtain = e_sem_obtain; env->sem_obtain_shared = e_sem_obtain_shared;
    env->sem_attempt = e_sem_attempt; env->sem_release = e_sem_release;
    env->find_task = e_find_task; env->wait = e_wait; env->signal = e_signal;
    env->alloc_signal = e_alloc_signal; env->free_signal = e_free_signal; env->reply_msg = e_reply_msg;
    env->add_int_server = e_add_int; env->rem_int_server = e_rem_int;
    env->chip_read = e_chip_read; env->chip_write = e_chip_write;
    env->next_tag = e_next_tag; env->find_tag = e_find_tag; env->get_tag_data = e_get_tag_data;
    env->clone_tags = e_clone_tags; env->free_tags = e_free_tags;
    env->log = 0;
    env->call_hook = e_call_hook;
    env->set_int_vector = e_set_int_vector;
    env->type_of_mem = e_type_of_mem;

    if (!gfx_init_face(env)) goto fail;
    n = 1;
    if (!gfx_init_display(env)) goto fail;
    n = 2;
    if (!gfx_init_text(env)) goto fail;
    n = 3;
    if (!gfx_init_gels(env)) goto fail;
    return lib;

fail:
    areas_exit(env, n);
    if (base->utilbase) CloseLibrary((struct Library *)base->utilbase);
    FreeMem((UBYTE *)base - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
    return 0;
}

static struct Library *lib_open(REG(a6, struct Library *lib))
{
    lib->lib_OpenCnt++;
    lib->lib_Flags &= ~LIBF_DELEXP;
    return lib;
}

static BPTR lib_expunge(REG(a6, struct Library *lib));

static BPTR lib_close(REG(a6, struct Library *lib))
{
    if (lib->lib_OpenCnt) lib->lib_OpenCnt--;
    if (lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP)) return lib_expunge(lib);
    return 0;
}

/* The replacement is the system's and stays; the side-by-side build goes
 * once nobody has it open. */
static BPTR lib_expunge(REG(a6, struct Library *lib))
{
    struct gfx_base *base = (struct gfx_base *)lib;
    struct gfx_private *pv = gfx_private_of(base);
    struct ExecBase *SysBase = (struct ExecBase *)base->execbase;
    BPTR seglist = (BPTR)pv->reserved[0];

    if (lib->lib_OpenCnt || !seglist || lib_name[0] == 'g') {
        lib->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    Remove(&lib->lib_Node);
    areas_exit(&pv->env, 4);
    CloseLibrary((struct Library *)base->utilbase);
    FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* graphics.library's LVO order; gfx_vectors.h follows graphics.lvo line
 * for line. */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    GFX_VECTORS
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    GFX_BASE_POSSIZE, lib_vectors, NULL, (APTR)lib_init
};

/* graphics.library's own priority, after exec, expansion and utility */
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT | RTF_COLDSTART, LIB_VERSION, NT_LIBRARY, 65,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};
