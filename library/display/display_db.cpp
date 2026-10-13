/* SPDX-License-Identifier: MIT */
/* OpenGfx display: extended nodes, monitors, the display database,
 * VideoControl, and the display's start. Copyright (c) 2026 Dalsin
 * Limited. MIT.
 *
 * Clean-room: from graphics.library's autodocs (GfxNew, GfxFree,
 * GfxAssociate, GfxLookUp, OpenMonitor, CloseMonitor, FindDisplayInfo,
 * NextDisplayInfo, GetDisplayInfoData, GetVPModeID, ModeNotAvailable,
 * BestModeIDA, CoerceMode, VideoControl) and the NDK 3.2 headers
 * (graphics/modeid.h, displayinfo.h, monitor.h, videocontrol.h); never
 * from Kickstart's code. Lean C++: no Amiga headers, no globals; the
 * tables are constants.
 *
 * The database is ours: three monitors (default, NTSC, PAL) times the 30
 * native keys of modeid.h, each with its NotAvailable from the chips.
 * DEVS:Monitors drivers add nothing yet (graphics.library's private
 * AddDisplayInfo calls are not done). */
#include "display_int.h"

using namespace gd;

namespace {

enum : uint8_t { K_LACE = 1, K_HAM = 2, K_EHB = 4, K_DPF = 8, K_DPF2 = 16 };
enum : uint8_t { NEED_OCS = 0, NEED_ECS = 1, NEED_AGA = 2 };
struct keydef {
    uint16_t key;
    uint8_t res;                        /* 0 lores, 1 hires, 2 superhires */
    uint8_t flags;
    uint8_t need;
};
constexpr keydef keys[NKEYS] = {
    { 0x0000, 0, 0, NEED_OCS },      { 0x0004, 0, K_LACE, NEED_OCS },
    { 0x0080, 0, K_EHB, NEED_OCS },  { 0x0084, 0, K_EHB | K_LACE, NEED_OCS },
    { 0x0400, 0, K_DPF, NEED_OCS },  { 0x0404, 0, K_DPF | K_LACE, NEED_OCS },
    { 0x0440, 0, K_DPF | K_DPF2, NEED_OCS }, { 0x0444, 0, K_DPF | K_DPF2 | K_LACE, NEED_OCS },
    { 0x0800, 0, K_HAM, NEED_OCS },  { 0x0804, 0, K_HAM | K_LACE, NEED_OCS },
    { 0x8000, 1, 0, NEED_OCS },      { 0x8004, 1, K_LACE, NEED_OCS },
    { 0x8020, 2, 0, NEED_ECS },      { 0x8024, 2, K_LACE, NEED_ECS },
    { 0x8080, 1, K_EHB, NEED_AGA },  { 0x8084, 1, K_EHB | K_LACE, NEED_AGA },
    { 0x80A0, 2, K_EHB, NEED_AGA },  { 0x80A4, 2, K_EHB | K_LACE, NEED_AGA },
    { 0x8400, 1, K_DPF, NEED_OCS },  { 0x8404, 1, K_DPF | K_LACE, NEED_OCS },
    { 0x8420, 2, K_DPF, NEED_ECS },  { 0x8424, 2, K_DPF | K_LACE, NEED_ECS },
    { 0x8440, 1, K_DPF | K_DPF2, NEED_OCS }, { 0x8444, 1, K_DPF | K_DPF2 | K_LACE, NEED_OCS },
    { 0x8460, 2, K_DPF | K_DPF2, NEED_ECS }, { 0x8464, 2, K_DPF | K_DPF2 | K_LACE, NEED_ECS },
    { 0x8800, 1, K_HAM, NEED_AGA },  { 0x8804, 1, K_HAM | K_LACE, NEED_AGA },
    { 0x8820, 2, K_HAM, NEED_AGA },  { 0x8824, 2, K_HAM | K_LACE, NEED_AGA },
};
constexpr uint32_t monids[NMONITORS] = { GD_DEFAULT_MONITOR_ID, GD_NTSC_MONITOR_ID, GD_PAL_MONITOR_ID };
const char *const monnames[NMONITORS] = { "default.monitor", "ntsc.monitor", "pal.monitor" };
const char *const resnames[3] = { "Low Res", "High Res", "Super-High Res" };

struct mode {
    int mon;                            /* 0 default, 1 ntsc, 2 pal */
    const keydef *k;
    uint32_t id;
};

bool find_mode(uint32_t id, mode &m)
{
    for (int i = 0; i < NMONITORS; i++)
        if ((id & GD_MONITOR_ID_MASK) == monids[i])
            for (int j = 0; j < NKEYS; j++)
                if ((id & ~GD_MONITOR_ID_MASK) == keys[j].key) {
                    m = { i, &keys[j], id };
                    return true;
                }
    return false;
}

bool mon_pal(gfx_env *env, int mon)
{
    if (mon == 2) return true;
    if (mon == 1) return false;
    return (base(env)->displayflags & GFX_PAL) != 0;
}

uint32_t avail(gfx_env *env, const mode &m)
{
    uint8_t c = base(env)->chiprevbits0;
    if (m.k->need == NEED_ECS && !(c & (GFX_GFXF_HR_DENISE | GFX_GFXF_AA_LISA))) return GD_DI_AVAIL_NOCHIPS;
    if (m.k->need == NEED_AGA && !(c & GFX_GFXF_AA_LISA)) return GD_DI_AVAIL_NOCHIPS;
    return 0;
}

uint32_t props(gfx_env *env, const mode &m)
{
    uint32_t p = GD_DIPF_IS_SPRITES | GD_DIPF_IS_DRAGGABLE | GD_DIPF_IS_BEAMSYNC | GD_DIPF_IS_DBUFFER
                 | GD_DIPF_IS_SPRITES_ATT;
    uint8_t f = m.k->flags;
    if (f & K_LACE) p |= GD_DIPF_IS_LACE;
    if (f & K_HAM) p |= GD_DIPF_IS_HAM;
    if (f & K_EHB) p |= GD_DIPF_IS_EXTRAHALFBRITE;
    if (f & K_DPF) p |= GD_DIPF_IS_DUALPF;
    if (f & K_DPF2) p |= GD_DIPF_IS_PF2PRI;
    if (!(f & (K_HAM | K_EHB | K_DPF))) p |= GD_DIPF_IS_WB;
    if (m.k->need == NEED_ECS) p |= GD_DIPF_IS_ECS;
    if (m.k->need == NEED_AGA) p |= GD_DIPF_IS_AA;
    if (mon_pal(env, m.mon)) p |= GD_DIPF_IS_PAL;
    if (base(env)->displayflags & GFX_GENLOC) p |= GD_DIPF_IS_GENLOCK;
    if (aga(env))
        p |= GD_DIPF_IS_SPRITES_CHNG_RES | GD_DIPF_IS_SPRITES_BORDER | GD_DIPF_IS_SPRITES_CHNG_BASE
             | GD_DIPF_IS_SPRITES_CHNG_PRI;
    return p;
}

int maxdepth(gfx_env *env, const mode &m)
{
    bool a = aga(env);
    uint8_t f = m.k->flags;
    if (m.k->res == 2) return 2;        /* 1x fetch (display_view.cpp) */
    if (m.k->res == 1) return 4;
    if (f & (K_HAM | K_EHB)) return a && (f & K_HAM) ? 8 : 6;
    return a ? 8 : (f & K_DPF) ? 6 : 5;
}

void rect(gfx_rectangle &r, int x0, int y0, int x1, int y1)
{
    r.minx = static_cast<int16_t>(x0);
    r.miny = static_cast<int16_t>(y0);
    r.maxx = static_cast<int16_t>(x1);
    r.maxy = static_cast<int16_t>(y1);
}

void nominal(gfx_env *env, const mode &m, int &w, int &h)
{
    w = 320 << m.k->res;
    h = (mon_pal(env, m.mon) ? 256 : 200) << ((m.k->flags & K_LACE) ? 1 : 0);
}

void copy_out(void *buf, const void *src, uint32_t n)
{
    uint8_t *d = static_cast<uint8_t *>(buf);
    const uint8_t *s = static_cast<const uint8_t *>(src);
    while (n--) *d++ = *s++;
}

int ncmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y) return 1;
        if (!x) return 0;
    }
}

uint32_t hash_of(const void *key) { return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)) >> 3) % NHASH; }

uint32_t extnode_size(int type)
{
    switch (type) {
    case GD_VIEW_EXTRA_TYPE: return sizeof(gd_viewextra);
    case GD_VIEWPORT_EXTRA_TYPE: return sizeof(gd_vpextra);
    case GD_SPECIAL_MONITOR_TYPE: return sizeof(gd_specialmonitor);
    case GD_MONITOR_SPEC_TYPE: return sizeof(gd_monitorspec);
    default: return 0;
    }
}

void build_copinit(gfx_env *env, gd_copinit *ci)
{
    auto mv = [](uint16_t *w, int i, uint16_t reg, uint16_t val) {
        w[2 * i] = reg;
        w[2 * i + 1] = val;
    };
    mv(ci->vsync_hblank, 0, GD_NOOP, 0);
    for (int i = 0; i < 6; i++) mv(ci->diagstrt, i, GD_NOOP, 0);
    mv(ci->fm0, 0, GD_NOOP, 0);
    mv(ci->diwstart, 0, GD_DIWSTRT, 0x2C81);
    mv(ci->diwstart, 1, GD_DIWSTOP, 0xF4C1);
    mv(ci->diwstart, 2, GD_DDFSTRT, 0x0038);
    mv(ci->diwstart, 3, GD_DDFSTOP, 0x00D0);
    mv(ci->diwstart, 4, GD_BPLCON0, static_cast<uint16_t>(0x0200 | (base(env)->system_bplcon0 & 0x0102)));
    mv(ci->bplcon2, 0, GD_BPLCON2, 0x0024);
    for (int i = 0; i < 8; i++) mv(ci->sprfix, i, GD_NOOP, 0);
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ci->sprstop));
    for (int i = 0; i < 8; i++) {
        mv(ci->sprstrtup, 2 * i, static_cast<uint16_t>(GD_SPR0PTH + 4 * i), static_cast<uint16_t>(a >> 16));
        mv(ci->sprstrtup, 2 * i + 1, static_cast<uint16_t>(GD_SPR0PTH + 4 * i + 2), static_cast<uint16_t>(a));
    }
    mv(ci->wait14, 0, GD_NOOP, 0);
    mv(ci->norm_hblank, 0, GD_NOOP, 0);
    mv(ci->jump, 0, GD_COPJMP2, 0);
    ci->wait_forever[0] = 0xFFFF;
    ci->wait_forever[1] = 0xFFFE;
    mv(ci->wait_forever, 1, GD_NOOP, 0);
    mv(ci->wait_forever, 2, GD_NOOP, 0);
    for (int i = 0; i < 8; i++) ci->sprstop[i] = 0;
}

gd_monitorspec *new_monitor(gfx_env *env, int i, bool pal)
{
    gd_monitorspec *ms = reinterpret_cast<gd_monitorspec *>(gd_GfxNew(env, GD_MONITOR_SPEC_TYPE));
    if (!ms) return nullptr;
    ms->n.node.name = const_cast<char *>(monnames[i]);
    ms->ratioh = ms->ratiov = 16;       /* RATIO_UNITY */
    ms->total_rows = pal ? 312 : 262;   /* STANDARD_PAL_ROWS, STANDARD_NTSC_ROWS */
    ms->total_colorclocks = 226;        /* STANDARD_COLORCLOCKS */
    ms->denisemaxdisplaycolumn = 455;   /* STANDARD_DENISE_MAX */
    ms->deniseminDisplaycolumn = 93;    /* STANDARD_DENISE_MIN */
    ms->beamcon0 = pal ? 0x0020 : 0;    /* DISPLAYPAL */
    ms->min_row = pal ? 29 : 21;        /* MIN_PAL_ROW, MIN_NTSC_ROW */
    ms->xoffset = 9;                    /* STANDARD_XOFFSET */
    ms->yoffset = 0;
    rect(ms->legalview, LEGAL_X0, ms->min_row, LEGAL_X1 - 1, ms->total_rows - 1);
    ms->displaycompatible = 0;          /* MCOMPAT_MIXED */
    return ms;
}

const mode *mode_by_index(int i, mode &m)
{
    m = { i / NKEYS, &keys[i % NKEYS], monids[i / NKEYS] | keys[i % NKEYS].key };
    return &m;
}

} // namespace

extern "C" {

/* ---- extended nodes -------------------------------------------------------- */

gd_extnode *gd_GfxNew(gfx_env *env, uint32_t type)
{
    uint32_t size = extnode_size(static_cast<int>(type));
    if (!size) return nullptr;
    gd_extnode *n = static_cast<gd_extnode *>(alloc(env, size, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!n) return nullptr;
    n->node.type = GD_NT_GRAPHICS;
    n->subsystem = GD_SS_GRAPHICS;
    n->subtype = static_cast<uint8_t>(type);
    n->library = static_cast<int32_t>(reinterpret_cast<uintptr_t>(env->base));
    n->init = nullptr;                  /* ABI test: OS 3.2 puts a routine here */
    if (type == GD_MONITOR_SPEC_TYPE) {
        gd_monitorspec *ms = reinterpret_cast<gd_monitorspec *>(n);
        list_init(&ms->displayinfodatabase);
        if (env->sem_init) env->sem_init(env->ctx, &ms->displayinfodatabasesemaphore);
    }
    return n;
}

void gd_GfxFree(gfx_env *env, gd_extnode *n)
{
    if (!n) return;
    uint32_t size = extnode_size(n->subtype);
    if (!size) return;                  /* not one of GfxNew's: left alone (ABI test) */
    state *s = st(env);
    env->forbid(env->ctx);
    for (int i = 0; i < NHASH; i++)
        for (assoc **pp = &s->hash[i]; *pp;) {
            if ((*pp)->node == n) {
                assoc *a = *pp;
                *pp = a->next;
                release(env, a, sizeof(assoc));
            } else
                pp = &(*pp)->next;
        }
    env->permit(env->ctx);
    release(env, n, size);
}

void gd_GfxAssociate(gfx_env *env, const void *key, gd_extnode *node)
{
    state *s = st(env);
    if (!key) return;
    env->forbid(env->ctx);
    assoc **pp = &s->hash[hash_of(key)];
    for (assoc *a = *pp; a; a = a->next)
        if (a->key == key) {
            a->node = node;
            env->permit(env->ctx);
            return;
        }
    assoc *a = static_cast<assoc *>(alloc(env, sizeof(assoc), GFX_MEMF_PUBLIC));
    if (a) {
        a->key = key;
        a->node = node;
        a->next = *pp;
        *pp = a;
    }
    env->permit(env->ctx);
}

gd_extnode *gd_GfxLookUp(gfx_env *env, const void *key)
{
    state *s = st(env);
    if (!key || !s) return nullptr;
    for (assoc *a = s->hash[hash_of(key)]; a; a = a->next)
        if (a->key == key) return a->node;
    return nullptr;
}

/* ---- monitors -------------------------------------------------------------- */

gd_monitorspec *gd_OpenMonitor(gfx_env *env, const char *name, uint32_t id)
{
    gfx_base *gb = base(env);
    gd_monitorspec *ms = nullptr;
    env->forbid(env->ctx);
    if (name) {
        for (gfx_node *n = gb->monitorlist.head; n->succ; n = n->succ)
            if (n->name && !ncmp(n->name, name)) {
                ms = reinterpret_cast<gd_monitorspec *>(n);
                break;
            }
    } else {
        uint32_t mon = id == GD_INVALID_ID ? 0 : id & GD_MONITOR_ID_MASK;
        for (int i = 0; i < NMONITORS; i++)
            if (monids[i] == mon) ms = st(env)->monitors[i];
    }
    if (ms) ms->opencount++;
    env->permit(env->ctx);
    return ms;
}

int32_t gd_CloseMonitor(gfx_env *env, gd_monitorspec *ms)
{
    if (!ms) return 0;
    env->forbid(env->ctx);
    int32_t err = ms->opencount ? 0 : 1;    /* FALSE when closed (autodoc) */
    if (ms->opencount) ms->opencount--;
    env->permit(env->ctx);
    return err;
}

/* ---- the display database --------------------------------------------------- */

void *gd_FindDisplayInfo(gfx_env *env, uint32_t id)
{
    state *s = st(env);
    for (int i = 0; i < NMODES; i++)
        if (s->modeids[i] == id) return &s->modeids[i];
    return nullptr;
}

uint32_t gd_NextDisplayInfo(gfx_env *env, uint32_t id)
{
    state *s = st(env);
    if (id == GD_INVALID_ID) return s->modeids[0];
    for (int i = 0; i + 1 < NMODES; i++)
        if (s->modeids[i] == id) return s->modeids[i + 1];
    /* an ID not in the database: the next one above it (ABI test) */
    for (int i = 0; i < NMODES; i++)
        if (s->modeids[i] > id) return s->modeids[i];
    return GD_INVALID_ID;
}

uint32_t gd_GetDisplayInfoData(gfx_env *env, void *handle, void *buf, uint32_t size, uint32_t tagid, uint32_t id)
{
    state *s = st(env);
    if (handle) {
        uint32_t *h = static_cast<uint32_t *>(handle);
        if (h < s->modeids || h >= s->modeids + NMODES) return 0;
        id = *h;
    }
    mode m;
    if (!buf || !find_mode(id, m)) return 0;
    const bool pal = mon_pal(env, m.mon), lace = (m.k->flags & K_LACE) != 0;
    const int xm = 1 << m.k->res, ym = lace ? 2 : 1;
    gd_monitorspec *ms = s->monitors[m.mon];
    union {
        gd_displayinfo di;
        gd_dimensioninfo dims;
        gd_monitorinfo mi;
        gd_nameinfo ni;
    } u;
    zero(&u, sizeof u);
    uint32_t len;
    switch (tagid) {
    case GD_DTAG_DISP:
        len = sizeof u.di;
        u.di.notavailable = static_cast<uint16_t>(avail(env, m));
        u.di.propertyflags = props(env, m);
        u.di.resolution = { static_cast<int16_t>(44 / xm), static_cast<int16_t>(44 / ym) };
        u.di.pixelspeed = static_cast<uint16_t>(140 / xm);
        u.di.numstdsprites = 8;
        u.di.paletterange = aga(env) ? 0xFFFF : 4096;      /* ABI test */
        u.di.spriteresolution = { 44, static_cast<int16_t>(44 / ym) };
        u.di.redbits = u.di.greenbits = u.di.bluebits = aga(env) ? 8 : 4;
        break;
    case GD_DTAG_DIMS: {
        len = sizeof u.dims;
        int w, h;
        nominal(env, m, w, h);
        int rows = ms ? ms->total_rows : (pal ? 312 : 262), minrow = ms ? ms->min_row : (pal ? 29 : 21);
        bool big = (base(env)->chiprevbits0 & GFX_GFXF_HR_AGNUS) != 0;
        u.dims.maxdepth = static_cast<uint16_t>(maxdepth(env, m));
        u.dims.minrasterwidth = 16;
        u.dims.minrasterheight = 1;
        u.dims.maxrasterwidth = big ? 16368 : 1008;
        u.dims.maxrasterheight = big ? 16384 : 1008;
        rect(u.dims.nominal, 0, 0, w - 1, h - 1);
        rect(u.dims.maxoscan, (LEGAL_X0 - VIEW_X) * xm, (minrow - VIEW_Y) * ym, (LEGAL_X1 - VIEW_X) * xm - 1,
             (rows - VIEW_Y) * ym - 1);
        u.dims.videooscan = u.dims.maxoscan;
        u.dims.txtoscan = u.dims.nominal;
        rect(u.dims.stdoscan, -16 * xm, 0, w - 1 + 16 * xm, h - 1);
        break;
    }
    case GD_DTAG_MNTR:
        len = sizeof u.mi;
        u.mi.mspc = ms;
        u.mi.viewposition = { VIEW_X, VIEW_Y };
        u.mi.viewresolution = { 44, 44 };                   /* ABI test */
        rect(u.mi.viewpositionrange, LEGAL_X0, ms ? ms->min_row : 21, LEGAL_X1 - 1, ms ? ms->total_rows - 1 : 261);
        u.mi.totalrows = ms ? ms->total_rows : 262;
        u.mi.totalcolorclocks = ms ? ms->total_colorclocks : 226;
        u.mi.minrow = ms ? ms->min_row : 21;
        u.mi.compatibility = 0;                             /* MCOMPAT_MIXED */
        u.mi.mouseticks = { static_cast<int16_t>(44 / xm), static_cast<int16_t>(44 / ym) };
        u.mi.defaultviewposition = { VIEW_X, VIEW_Y };
        u.mi.preferredmodeid = monids[m.mon] | GD_HIRES_KEY;
        break;
    case GD_DTAG_NAME: {
        if (m.mon == 0) return 0;       /* the default monitor's modes have no names (ABI test) */
        len = sizeof u.ni;
        char *p = reinterpret_cast<char *>(u.ni.name), *e = p + sizeof u.ni.name - 1;
        auto add = [&](const char *t) { while (*t && p < e) *p++ = *t++; };
        add(m.mon == 1 ? "NTSC:" : "PAL:");
        uint8_t f = m.k->flags;
        if ((f & K_HAM) && m.k->res == 0) add("HAM");
        else if ((f & K_EHB) && m.k->res == 0) add("Extra-HalfBrite");
        else {
            add(resnames[m.k->res]);
            if (f & K_HAM) add(" HAM");
            if (f & K_EHB) add(" EHB");
        }
        if (f & K_LACE) add(" Laced");
        if (f & K_DPF) add((f & K_DPF2) ? " DualPF2" : " DualPF");
        break;
    }
    default:
        return 0;                       /* DTAG_VEC and others: none (ABI test) */
    }
    u.di.header.structid = tagid;
    u.di.header.displayid = id;
    u.di.header.skipid = GFX_TAG_SKIP;
    u.di.header.length = (len + 7) / 8;
    if (size > len) size = len;
    copy_out(buf, &u, size);
    return size;
}

uint32_t gd_GetVPModeID(gfx_env *env, const gd_viewport *vp)
{
    (void)env;
    if (!vp) return GD_INVALID_ID;
    const gfx_colormap *cm = vp->colormap;
    if (cm && cm->type >= GFX_COLORMAP_TYPE_V36 && cm->vpmodeid != GD_INVALID_ID) return cm->vpmodeid;
    /* an old ViewPort: its Modes on the default monitor (ABI test) */
    return vp->modes & (GD_LACE | GD_SUPERHIRES | GD_PFBA | GD_EXTRA_HALFBRITE | GD_DUALPF | GD_HAM | GD_HIRES);
}

uint32_t gd_ModeNotAvailable(gfx_env *env, uint32_t id)
{
    mode m;
    if (find_mode(id, m)) return avail(env, m);
    for (int i = 0; i < NMONITORS; i++)
        if ((id & GD_MONITOR_ID_MASK) == monids[i]) return GD_DI_AVAIL_NOCHIPS;
    return GD_DI_AVAIL_NOMONITOR;       /* ABI test */
}

uint32_t gd_BestModeIDA(gfx_env *env, const gfx_tagitem *tags)
{
    const uint32_t special = GD_DIPF_IS_DUALPF | GD_DIPF_IS_PF2PRI | GD_DIPF_IS_HAM | GD_DIPF_IS_EXTRAHALFBRITE;
    uint32_t must = 0, mustnot = special, source = GD_INVALID_ID, monitor = GD_INVALID_ID;
    int nomw = 640, nomh = 200, desw = 0, desh = 0, depth = 1, rb = 0, gbits = 0, bb = 0;
    bool f;
    /* a ViewPort and a source mode first, then the explicit tags over them */
    gd_viewport *vp = reinterpret_cast<gd_viewport *>(static_cast<uintptr_t>(tag_data(tags, GD_BIDTAG_ViewPort, 0)));
    if (vp) {
        source = gd_GetVPModeID(env, vp);
        nomw = vp->dwidth;
        nomh = vp->dheight;
        if (vp->rasinfo && vp->rasinfo->bitmap) depth = vp->rasinfo->bitmap->depth;
    }
    source = tag_data(tags, GD_BIDTAG_SourceID, source);
    mode sm;
    bool hassrc = source != GD_INVALID_ID && find_mode(source, sm);
    if (hassrc) {
        uint32_t sp = props(env, sm) & special;
        must |= sp;
        mustnot &= ~sp;
        if (!vp) nominal(env, sm, nomw, nomh);
    }
    must = tag_data(tags, GD_BIDTAG_DIPFMustHave, must);
    mustnot = tag_data(tags, GD_BIDTAG_DIPFMustNotHave, mustnot & ~must);   /* what is a must is not barred */
    nomw = static_cast<int>(tag_data(tags, GD_BIDTAG_NominalWidth, static_cast<uint32_t>(nomw)));
    nomh = static_cast<int>(tag_data(tags, GD_BIDTAG_NominalHeight, static_cast<uint32_t>(nomh)));
    desw = static_cast<int>(tag_data(tags, GD_BIDTAG_DesiredWidth, static_cast<uint32_t>(nomw)));
    desh = static_cast<int>(tag_data(tags, GD_BIDTAG_DesiredHeight, static_cast<uint32_t>(nomh)));
    depth = static_cast<int>(tag_data(tags, GD_BIDTAG_Depth, static_cast<uint32_t>(depth)));
    monitor = tag_data(tags, GD_BIDTAG_MonitorID, monitor, &f);
    rb = static_cast<int>(tag_data(tags, GD_BIDTAG_RedBits, 0));
    gbits = static_cast<int>(tag_data(tags, GD_BIDTAG_GreenBits, 0));
    bb = static_cast<int>(tag_data(tags, GD_BIDTAG_BlueBits, 0));
    if (nomw <= 0) nomw = 640;
    if (nomh <= 0) nomh = 200;
    const int bits = aga(env) ? 8 : 4;
    if (rb > bits || gbits > bits || bb > bits) return GD_INVALID_ID;
    uint32_t best = GD_INVALID_ID;
    uint32_t bestscore[4] = { ~0u, ~0u, ~0u, ~0u };
    for (int i = 0; i < NMODES; i++) {
        mode m;
        mode_by_index(i, m);
        if (monitor != GD_INVALID_ID && (m.id & GD_MONITOR_ID_MASK) != (monitor & GD_MONITOR_ID_MASK)) continue;
        if (avail(env, m)) continue;
        uint32_t p = props(env, m);
        if ((p & must) != must || (p & mustnot)) continue;
        if (maxdepth(env, m) < depth) continue;
        int w, h;
        nominal(env, m, w, h);
        /* fits the desired size, the nominal aspect, the size, the source's monitor */
        uint32_t fits = (w >= desw && h >= desh) ? 0 : 1;
        int cross = w * nomh - h * nomw;
        uint32_t aspect = static_cast<uint32_t>(cross < 0 ? -cross : cross) / static_cast<uint32_t>((h * nomh) / 64 + 1);
        int dw = w - desw, dh = h - desh;
        uint32_t dist = static_cast<uint32_t>((dw < 0 ? -dw : dw) + (dh < 0 ? -dh : dh));
        uint32_t mon = hassrc && (m.id & GD_MONITOR_ID_MASK) != (sm.id & GD_MONITOR_ID_MASK) ? 1 : 0;
        uint32_t score[4] = { fits, aspect, dist, mon };
        bool better = false;
        for (int k = 0; k < 4; k++)
            if (score[k] != bestscore[k]) {
                better = score[k] < bestscore[k];
                break;
            }
        if (better) {
            best = m.id;
            for (int k = 0; k < 4; k++) bestscore[k] = score[k];
        }
    }
    return best;
}

uint32_t gd_CoerceMode(gfx_env *env, gd_viewport *vp, uint32_t monitorid, uint32_t flags)
{
    uint32_t src = gd_GetVPModeID(env, vp);
    mode m;
    if (src == GD_INVALID_ID) return GD_INVALID_ID;
    uint32_t key = src & ~GD_MONITOR_ID_MASK;
    uint32_t mon = monitorid & GD_MONITOR_ID_MASK;
    uint32_t id = mon | key;
    if (find_mode(id, m) && !avail(env, m)) return id;
    if (flags & GD_PRESERVE_COLORS) return GD_INVALID_ID;
    /* the same resolution without HAM, EHB or dual playfield (ours) */
    id = mon | (key & (GD_HIRES | GD_SUPERHIRES | GD_LACE));
    if (find_mode(id, m) && !avail(env, m)) return id;
    return GD_INVALID_ID;
}

/* ---- VideoControl ----------------------------------------------------------- */

uint32_t gd_VideoControl(gfx_env *env, gfx_colormap *cm, gfx_tagitem *tags)
{
    if (!cm) return 1;
    uint32_t err = 0;
    bool immediate = false;
    gd_viewport *vp = static_cast<gd_viewport *>(cm->vp);
    const gfx_tagitem *s = tags;
    auto flag = [&](uint8_t &field, uint8_t bit, bool on) {
        field = static_cast<uint8_t>(on ? field | bit : field & ~bit);
    };
    auto getflag = [](gfx_tagitem *t, bool on, uint32_t set, uint32_t clr) { t->tag = on ? set : clr; };
    while (gfx_tagitem *t = next_tag(&s)) {
        uint32_t d = t->data;
        switch (t->tag) {
        case 0x80000000u: flag(cm->flags, GD_COLORMAP_TRANSPARENCY, false); break;   /* CHROMAKEY_CLR */
        case 0x80000001u: flag(cm->flags, GD_COLORMAP_TRANSPARENCY, true); break;    /* CHROMAKEY_SET */
        case 0x80000002u: flag(cm->flags, GD_COLORPLANE_TRANSPARENCY, false); break; /* BITPLANEKEY_CLR */
        case 0x80000003u: flag(cm->flags, GD_COLORPLANE_TRANSPARENCY, true); break;
        case 0x80000004u: flag(cm->flags, GD_BORDER_BLANKING, false); break;         /* BORDERBLANK_CLR */
        case 0x80000005u: flag(cm->flags, GD_BORDER_BLANKING, true); break;
        case 0x80000006u: flag(cm->flags, GD_BORDER_NOTRANSPARENCY, false); break;   /* BORDERNOTRANS_CLR */
        case 0x80000007u: flag(cm->flags, GD_BORDER_NOTRANSPARENCY, true); break;
        case 0x80000008u:                                                            /* CHROMA_PEN_CLR */
        case 0x80000009u:                                                            /* CHROMA_PEN_SET */
            if (d < cm->count && cm->colortable) {
                if (t->tag & 1) cm->colortable[d] |= 0x8000;
                else cm->colortable[d] &= 0x7FFF;
            }
            break;
        case 0x8000000Au: cm->transparencyplane = static_cast<uint8_t>(d); break;    /* CHROMA_PLANE_SET */
        case 0x8000000Bu: cm->vp = vp = reinterpret_cast<gd_viewport *>(static_cast<uintptr_t>(d)); break; /* ATTACH_CM_SET */
        case 0x8000000Cu:                                                            /* NEXTBUF_CM */
            s = reinterpret_cast<const gfx_tagitem *>(static_cast<uintptr_t>(d));
            break;
        case 0x8000000Du: flag(cm->flags, GD_VIDEOCONTROL_BATCH, false); break;      /* BATCH_CM_CLR */
        case 0x8000000Eu: flag(cm->flags, GD_VIDEOCONTROL_BATCH, true); break;
        case 0x8000000Fu: t->data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cm->normaldisplayinfo)); break;
        case 0x80000010u: cm->normaldisplayinfo = reinterpret_cast<void *>(static_cast<uintptr_t>(d)); break;
        case 0x80000011u: t->data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cm->coercedisplayinfo)); break;
        case 0x80000012u: cm->coercedisplayinfo = reinterpret_cast<void *>(static_cast<uintptr_t>(d)); break;
        case 0x80000013u: t->data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cm->vpe)); break;
        case 0x80000014u: cm->vpe = reinterpret_cast<void *>(static_cast<uintptr_t>(d)); break;
        case 0x80000015u: getflag(t, cm->flags & GD_COLORMAP_TRANSPARENCY, 0x80000001u, 0x80000000u); break;
        case 0x80000016u: getflag(t, cm->flags & GD_COLORPLANE_TRANSPARENCY, 0x80000003u, 0x80000002u); break;
        case 0x80000017u: getflag(t, cm->flags & GD_BORDER_BLANKING, 0x80000005u, 0x80000004u); break;
        case 0x80000018u: getflag(t, cm->flags & GD_BORDER_NOTRANSPARENCY, 0x80000007u, 0x80000006u); break;
        case 0x80000019u: t->data = cm->transparencyplane; break;                    /* CHROMA_PLANE_GET */
        case 0x8000001Au: t->data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cm->vp)); break;
        case 0x8000001Bu: getflag(t, cm->flags & GD_VIDEOCONTROL_BATCH, 0x8000000Eu, 0x8000000Du); break;
        case 0x8000001Cu: t->data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cm->batch_items)); break;
        case 0x8000001Du:                                                            /* BATCH_ITEMS_SET */
        case 0x8000001Eu:                                                            /* BATCH_ITEMS_ADD: chained by the caller */
            cm->batch_items = reinterpret_cast<gfx_tagitem *>(static_cast<uintptr_t>(d));
            break;
        case 0x8000001Fu: t->data = cm->vpmodeid; break;                             /* VPMODEID_GET */
        case 0x80000020u: cm->vpmodeid = d; break;                                   /* VPMODEID_SET */
        case 0x80000021u: cm->vpmodeid = GD_INVALID_ID; break;                       /* VPMODEID_CLR */
        case 0x80000022u: getflag(t, cm->flags & GD_USER_COPPER_CLIP, 0x80000023u, 0x80000024u); break;
        case 0x80000023u: flag(cm->flags, GD_USER_COPPER_CLIP, true); break;
        case 0x80000024u: flag(cm->flags, GD_USER_COPPER_CLIP, false); break;
        case 0x80000025u: t->data = cm->bp_0_base; break;                            /* PF1_BASE_GET */
        case 0x80000026u: t->data = cm->bp_1_base; break;
        case 0x80000027u: t->data = cm->spritebase_even; break;
        case 0x80000028u: t->data = cm->spritebase_odd; break;
        case 0x80000029u: cm->bp_0_base = static_cast<uint16_t>(d); break;           /* PF1_BASE_SET */
        case 0x8000002Au: cm->bp_1_base = static_cast<uint16_t>(d); break;
        case 0x8000002Bu: cm->spritebase_even = static_cast<uint16_t>(d); break;
        case 0x8000002Cu: cm->spritebase_odd = static_cast<uint16_t>(d); break;
        case 0x8000002Du: getflag(t, cm->flags & GD_BORDERSPRITES, 0x8000002Eu, 0x8000002Fu); break;
        case 0x8000002Eu: flag(cm->flags, GD_BORDERSPRITES, true); break;
        case 0x8000002Fu: flag(cm->flags, GD_BORDERSPRITES, false); break;
        case 0x80000030u: cm->spriteresolution = static_cast<uint8_t>(d); break;    /* SPRITERESN_SET */
        case 0x80000031u: t->data = static_cast<uint32_t>(static_cast<int8_t>(cm->spriteresolution)); break;
        case 0x80000032u:                                                            /* PF1_TO_SPRITEPRI_SET */
            if (vp) vp->spritepriorities = static_cast<uint8_t>((vp->spritepriorities & ~7) | (d & 7));
            break;
        case 0x80000033u: t->data = vp ? vp->spritepriorities & 7u : 0; break;
        case 0x80000034u:
            if (vp) vp->spritepriorities = static_cast<uint8_t>((vp->spritepriorities & ~0x38) | ((d & 7) << 3));
            break;
        case 0x80000035u: t->data = vp ? (vp->spritepriorities >> 3) & 7u : 0; break;
        case 0x80000036u: immediate = true; break;                                   /* IMMEDIATE */
        case 0x80000037u: cm->auxflags |= GD_CMAF_FULLPALETTE; break;
        case 0x80000038u: getflag(t, cm->auxflags & GD_CMAF_FULLPALETTE, 0x80000037u, 0x80000039u); break;
        case 0x80000039u: cm->auxflags &= static_cast<uint8_t>(~GD_CMAF_FULLPALETTE); break;
        case 0x8000003Au: cm->spriteresdefault = static_cast<uint8_t>(d); break;     /* DEFSPRITERESN_SET */
        case 0x8000003Bu: t->data = static_cast<uint32_t>(static_cast<int8_t>(cm->spriteresdefault)); break;
        case 0x80000080u: flag(cm->auxflags, GD_CMAF_NO_INTERMED_UPDATE, !d); break; /* VC_IntermediateCLUpdate */
        case 0x80000081u: t->data = !(cm->auxflags & GD_CMAF_NO_INTERMED_UPDATE); break;
        case 0x80000082u: flag(cm->auxflags, GD_CMAF_NO_COLOR_LOAD, d != 0); break;  /* VC_NoColorPaletteLoad */
        case 0x80000083u: t->data = (cm->auxflags & GD_CMAF_NO_COLOR_LOAD) != 0; break;
        case 0x80000084u: flag(cm->auxflags, GD_CMAF_DUALPF_DISABLE, d != 0); break; /* VC_DUALPF_Disable */
        case 0x80000085u: t->data = (cm->auxflags & GD_CMAF_DUALPF_DISABLE) != 0; break;
        default: err = 1; break;        /* an unknown tag (ABI test) */
        }
    }
    if (immediate && vp && vp->dspins) patch_vp(env, vp, false);
    return err;
}

/* ---- the display's start ---------------------------------------------------- */

int gd_init(gfx_env *env, int own_hw)
{
    gfx_base *gb = base(env);
    state *s = static_cast<state *>(alloc(env, sizeof(state), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!s) return 0;
    gfx_private_of(gb)->area[GFX_AREA_DISPLAY] = s;
    s->copinit = static_cast<gd_copinit *>(alloc(env, sizeof(gd_copinit), GFX_MEMF_CHIP | GFX_MEMF_CLEAR));
    if (!s->copinit) {
        gd_exit(env);
        return 0;
    }
    s->spritewords = 1;
    s->hw = own_hw != 0;
    gb->copinit = s->copinit;
    gb->simplesprites = reinterpret_cast<void **>(s->sprites);
    gb->hash_table = reinterpret_cast<int16_t *>(s->hash);
    gb->displayinfodatabase = s->modeids;
    for (int i = 0; i < NMODES; i++) s->modeids[i] = monids[i / NKEYS] | keys[i % NKEYS].key;

    /* the chips: Agnus says PAL or NTSC */
    unsigned agnus = (env->chip_read(env->ctx, GD_VPOSR) >> 8) & 0x7F;
    bool pal = (agnus & 0x10) || agnus == 0x23;     /* ABI test: Alice's PAL ID */
    gb->displayflags = static_cast<uint16_t>((gb->displayflags & GFX_GENLOC) | (pal ? GFX_PAL | GFX_REALLY_PAL : GFX_NTSC));
    build_copinit(env, s->copinit);
    gd_SetChipRev(env, 0xFFFFFFFFu);    /* SETCHIPREV_BEST */

    for (int i = 0; i < NMONITORS; i++) {
        s->monitors[i] = new_monitor(env, i, i == 2 || (i == 0 && pal));
        if (!s->monitors[i]) {
            gd_exit(env);
            return 0;
        }
        add_tail(&gb->monitorlist, &s->monitors[i]->n.node);
    }
    gb->default_monitor = s->monitors[0];
    gb->natural_monitor = s->monitors[pal ? 2 : 1];
    gb->current_monitor = s->monitors[0];
    gb->monitor_id = 0;
    gb->current_tot_rows = s->monitors[0]->total_rows;
    gb->current_tot_cclks = s->monitors[0]->total_colorclocks;
    /* ABI test: OS 3.2's values for these */
    gb->normaldisplayrows = pal ? 256 : 200;
    gb->normaldisplaycolumns = 640;
    gb->maxdisplayrow = static_cast<uint16_t>(s->monitors[0]->total_rows - 1);
    gb->maxdisplaycolumn = 455;
    gb->mindisplaycolumn = 93;
    gb->normaldpmx = 1280;
    gb->normaldpmy = pal ? 1280 : 1000;
    gb->microsperline = pal ? 64 * 256 : 63 * 256 + 128;
    gb->defaultspritewidth = 16;
    set_fmode(env);
    for (int i = 0; i < 8; i++) set_sprite_ptr(env, i, nullptr);

    /* the copper on copinit, then DMA: copper, bitplanes, sprites */
    gd_LoadView(env, nullptr);
    put(env, GD_COPJMP1, 0);
    put(env, 0x096, 0x83A0);   /* DMACON: SET, DMAEN, BPLEN, COPEN, SPREN */
    return 1;
}

void gd_exit(gfx_env *env)
{
    gfx_base *gb = base(env);
    state *s = st(env);
    if (!s) return;
    for (int i = 0; i < NMONITORS; i++)
        if (s->monitors[i]) {
            remove(&s->monitors[i]->n.node);
            gd_GfxFree(env, &s->monitors[i]->n);
        }
    for (int i = 0; i < NHASH; i++)
        while (assoc *a = s->hash[i]) {
            s->hash[i] = a->next;
            release(env, a, sizeof(assoc));
        }
    if (gb->copinit == s->copinit) gb->copinit = nullptr;
    gb->simplesprites = nullptr;
    gb->hash_table = nullptr;
    gb->displayinfodatabase = nullptr;
    gb->default_monitor = gb->natural_monitor = gb->current_monitor = nullptr;
    release(env, s->copinit, sizeof(gd_copinit));
    gfx_private_of(gb)->area[GFX_AREA_DISPLAY] = nullptr;
    release(env, s, sizeof(state));
}

} // extern "C"
