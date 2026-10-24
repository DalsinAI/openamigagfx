/* SPDX-License-Identifier: MIT */
/* OpenGfx display: the beam, the chip revision and the sprites.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: from graphics.library's autodocs (VBeamPos, WaitTOF,
 * WaitBOVP, SetChipRev, GetSprite, FreeSprite, ChangeSprite, MoveSprite,
 * AllocSpriteDataA, FreeSpriteData, GetExtSpriteA, ChangeExtSpriteA), the
 * NDK 3.2 layouts and the Hardware Reference Manual's sprite and beam
 * registers. Lean C++: no Amiga headers, no globals. */
#include "display_int.h"

using namespace gd;

namespace {

int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* a sprite's control words for (x, y) in vp (or the View when vp is 0):
 * x in the ViewPort's pixels, y in its lines (ABI test: OS 3.2's units
 * and the HSTART of the View's left edge) */
void place(gfx_env *env, gd_viewport *vp, gd_simplesprite *s, int x, int y)
{
    gfx_base *gb = base(env);
    uint16_t *d = s->posctldata;
    if (!d) return;
    gd_view *view = static_cast<gd_view *>(gb->actiview);
    int hx = view ? view->dxoffset : VIEW_X, vy = view ? view->dyoffset : VIEW_Y;
    if (vp) {
        int res = (vp->modes & GD_SUPERHIRES) ? 4 : (vp->modes & GD_HIRES) ? 2 : 1;
        bool lace = (vp->modes & GD_LACE) != 0;
        hx += floordiv(vp->dxoffset, res) + floordiv(x, res);
        vy += lace ? (vp->dyoffset >> 1) + (y >> 1) : vp->dyoffset + y;
    } else {
        hx += x;
        vy += y;
    }
    int hstart = hx - 1;                /* the sprite's HSTART for the bitplanes' first pixel */
    int vstart = vy, vstop = vy + s->height;
    if (hstart < 0) hstart = 0;
    if (vstart < 0) vstart = 0;
    if (vstop < vstart) vstop = vstart;
    int w = st(env)->spritewords;
    uint16_t pos = static_cast<uint16_t>((vstart & 0xFF) << 8 | ((hstart >> 1) & 0xFF));
    uint16_t ctl = static_cast<uint16_t>((vstop & 0xFF) << 8 | (d[w] & 0x80) | ((vstart >> 8) & 1) << 2
                                         | ((vstop >> 8) & 1) << 1 | (hstart & 1));
    if (aga(env)) ctl |= static_cast<uint16_t>(((vstart >> 9) & 1) << 6 | ((vstop >> 9) & 1) << 5);
    d[0] = pos;
    d[w] = ctl;
}

int16_t claim(gfx_env *env, int num, int count, gd_simplesprite *a, gd_simplesprite *b)
{
    gfx_base *gb = base(env);
    state *s = st(env);
    int got = -1;
    env->disable(env->ctx);
    if (num == -1) {
        for (int i = 0; i + count <= 8 && got < 0; i += count)
            if (!((gb->spritereserved >> i) & ((1 << count) - 1))) got = i;
    } else if (num >= 0 && num + count <= 8 && !(count == 2 && (num & 1))
               && !((gb->spritereserved >> num) & ((1 << count) - 1)))
        got = num;
    if (got >= 0) {
        gb->spritereserved = static_cast<uint8_t>(gb->spritereserved | (((1 << count) - 1) << got));
        s->sprites[got] = a;
        a->num = static_cast<int16_t>(got);
        if (count == 2) {
            s->sprites[got + 1] = b;
            b->num = static_cast<int16_t>(got + 1);
        }
    }
    env->enable(env->ctx);
    return static_cast<int16_t>(got);
}

} // namespace

namespace gd {

void set_sprite_ptr(gfx_env *env, int n, const uint16_t *data)
{
    state *s = st(env);
    if (n < 0 || n > 7) return;
    if (!data) data = s->copinit->sprstop;
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    env->disable(env->ctx);
    s->copinit->sprstrtup[4 * n + 1] = static_cast<uint16_t>(a >> 16);
    s->copinit->sprstrtup[4 * n + 3] = static_cast<uint16_t>(a);
    env->enable(env->ctx);
}

void set_fmode(gfx_env *env)
{
    state *s = st(env);
    s->fmode = s->spritewords == 4 ? 0x000C : s->spritewords == 2 ? 0x0004 : 0;
    s->copinit->fm0[0] = aga(env) ? GD_FMODE : GD_NOOP;
    s->copinit->fm0[1] = s->fmode;
    base(env)->spritefmode = s->fmode;
    base(env)->spritewidth = static_cast<uint16_t>(16 * s->spritewords);
    /* each ViewPort's FMODE carries the sprite width too: the shown ones anew */
    if (gd_view *v = static_cast<gd_view *>(base(env)->actiview))
        for (gd_viewport *vp = v->viewport; vp; vp = vp->next) patch_vp(env, vp, false);
}

} // namespace gd

extern "C" {

/* ---- the beam ------------------------------------------------------------ */

int32_t gd_VBeamPos(gfx_env *env)
{
    for (;;) {
        uint16_t a = env->chip_read(env->ctx, GD_VPOSR);
        uint16_t b = env->chip_read(env->ctx, GD_VHPOSR);
        uint16_t c = env->chip_read(env->ctx, GD_VPOSR);
        if ((a & 7) == (c & 7)) return (a & 7) << 8 | b >> 8;   /* V10..V8 (ECS), V7..V0 */
    }
}

void gd_WaitTOF(gfx_env *env)
{
    gfx_base *gb = base(env);
    tof_waiter w;
    w.node.type = 0;
    w.node.pri = 0;
    w.node.name = nullptr;
    w.task = env->find_task(env->ctx);
    env->disable(env->ctx);
    add_tail(&gb->tof_waitq, &w.node);
    env->enable(env->ctx);
    /* the blank always signals a waiter it takes, so Wait at least once:
     * a blank that came before the first look leaves no SIGF_SINGLE behind */
    do
        env->wait(env->ctx, SIGF_SINGLE);
    while (w.task);
}

void gd_WaitBOVP(gfx_env *env, gd_viewport *vp)
{
    gfx_base *gb = base(env);
    gd_view *view = static_cast<gd_view *>(gb->actiview);
    if (!vp) return;
    geom g = view_geom(env, view);
    bool lace = (vp->modes & GD_LACE) != 0;
    int top = (view ? view->dyoffset : VIEW_Y) + (lace ? vp->dyoffset >> 1 : vp->dyoffset);
    int bottom = top + (lace ? (vp->dheight + 1) >> 1 : vp->dheight);
    if (bottom > g.total_rows - 1) bottom = g.total_rows - 1;
    int prev = -1;
    for (;;) {
        int b = gd_VBeamPos(env);
        if (b >= bottom || b < prev) return;    /* past it, or a new frame began */
        prev = b;
    }
}

uint32_t gd_SetChipRev(gfx_env *env, uint32_t want)
{
    gfx_base *gb = base(env);
    unsigned agnus = (env->chip_read(env->ctx, GD_VPOSR) >> 8) & 0x7F;
    unsigned d1 = env->chip_read(env->ctx, GD_DENISEID) & 0xFF, d2 = env->chip_read(env->ctx, GD_DENISEID) & 0xFF;
    uint32_t have = 0;
    if (agnus & 0x20) have |= GFX_GFXF_HR_AGNUS;
    if ((agnus & 0x20) && (agnus & 0x0F) >= 2) have |= GFX_GFXF_AA_ALICE;
    if (d1 == d2) {                     /* OCS Denise has no ID register: it floats */
        if (d1 == 0xFC) have |= GFX_GFXF_HR_DENISE;
        if (d1 == 0xF8) have |= GFX_GFXF_HR_DENISE | GFX_GFXF_AA_LISA;
    }
    uint32_t got = want & have;
    /* AA needs both of its chips (ours) */
    if ((got & (GFX_GFXF_AA_ALICE | GFX_GFXF_AA_LISA)) != (GFX_GFXF_AA_ALICE | GFX_GFXF_AA_LISA))
        got &= ~static_cast<uint32_t>(GFX_GFXF_AA_ALICE | GFX_GFXF_AA_LISA | GFX_GFXF_AA_MLISA);
    gb->chiprevbits0 = static_cast<uint8_t>(got);
    if (st(env) && st(env)->copinit) set_fmode(env);
    return got;
}

/* ---- sprites ------------------------------------------------------------- */

int16_t gd_GetSprite(gfx_env *env, gd_simplesprite *s, int32_t num)
{
    if (!s) return -1;
    int16_t n = claim(env, num, 1, s, nullptr);
    if (n >= 0) set_sprite_ptr(env, n, s->posctldata);
    return n;
}

void gd_FreeSprite(gfx_env *env, int32_t num)
{
    gfx_base *gb = base(env);
    if (num < 0 || num > 7) return;
    env->disable(env->ctx);
    set_sprite_ptr(env, num, nullptr);  /* before the sprite is free to claim again */
    gb->spritereserved = static_cast<uint8_t>(gb->spritereserved & ~(1 << num));
    st(env)->sprites[num] = nullptr;
    env->enable(env->ctx);
}

void gd_ChangeSprite(gfx_env *env, gd_viewport *vp, gd_simplesprite *s, uint16_t *data)
{
    if (!s || !data) return;
    s->posctldata = data;
    place(env, vp, s, s->x, s->y);
    if (s->num >= 0 && s->num < 8) set_sprite_ptr(env, s->num, data);
}

void gd_MoveSprite(gfx_env *env, gd_viewport *vp, gd_simplesprite *s, int32_t x, int32_t y)
{
    if (!s) return;
    s->x = static_cast<uint16_t>(x);
    s->y = static_cast<uint16_t>(y);
    place(env, vp, s, static_cast<int16_t>(x), static_cast<int16_t>(y));
}

/* One chip block: [size][pad][ExtSprite][posctl, lines, end]; the data is
 * 8-byte aligned for the 64-bit fetch. */
gd_extsprite *gd_AllocSpriteDataA(gfx_env *env, const gfx_bitmap *bm, const gfx_tagitem *tags)
{
    if (!bm) return nullptr;
    const bool old = tag_data(tags, GD_SPRITEA_OldDataFormat, 0) != 0;
    const int width = static_cast<int>(tag_data(tags, GD_SPRITEA_Width, 16));
    const int xrep = static_cast<int32_t>(tag_data(tags, GD_SPRITEA_XReplication, 0));
    const int yrep = static_cast<int32_t>(tag_data(tags, GD_SPRITEA_YReplication, 0));
    const bool attached = tag_data(tags, GD_SPRITEA_Attached, 0) != 0;
    bool hasout;
    int outh = static_cast<int>(tag_data(tags, GD_SPRITEA_OutputHeight, 0, &hasout));
    if (width <= 0 || width > 64 || xrep < -2 || xrep > 2 || yrep < -2 || yrep > 2) return nullptr;
    const int w = width <= 16 ? 1 : width <= 32 ? 2 : 4;
    const uint16_t *olddata = reinterpret_cast<const uint16_t *>(bm);
    int srch, srcw;
    if (old) {
        if (!hasout) return nullptr;    /* old data says nothing of its height (ABI test) */
        srcw = 16;
        srch = yrep >= 0 ? outh >> yrep : outh << -yrep;
    } else {
        srcw = bm->bytesperrow * 8;
        srch = bm->rows;
    }
    int h = hasout ? outh : (yrep >= 0 ? srch << yrep : srch >> -yrep);
    if (h <= 0 || h > 1023) return nullptr;
    uint32_t datasize = static_cast<uint32_t>((h + 2) * 2 * w * 2);
    uint32_t size = 24 + datasize;
    uint8_t *blk = static_cast<uint8_t *>(alloc(env, size, GFX_MEMF_CHIP | GFX_MEMF_CLEAR));
    if (!blk) return nullptr;
    *reinterpret_cast<uint32_t *>(blk) = size;
    gd_extsprite *es = reinterpret_cast<gd_extsprite *>(blk + 8);
    uint16_t *d = reinterpret_cast<uint16_t *>(blk + 24);
    es->es.posctldata = d;
    es->es.height = static_cast<uint16_t>(h);
    es->es.num = -1;                    /* ABI test */
    es->wordwidth = static_cast<uint16_t>(w);
    es->flags = attached ? GD_SPRITE_ATTACHED : 0;
    if (attached) d[w] = 0x80;
    for (int oy = 0; oy < h; oy++) {
        int sy = yrep >= 0 ? oy >> yrep : oy << -yrep;
        uint16_t *line = d + 2 * w * (oy + 1);
        if (sy >= srch) continue;
        for (int ox = 0; ox < 16 * w && ox < width; ox++) {
            int sx = xrep >= 0 ? ox >> xrep : ox << -xrep;
            if (sx >= srcw) continue;
            int b0, b1;
            if (old) {
                b0 = olddata[2 * sy] >> (15 - sx) & 1;
                b1 = olddata[2 * sy + 1] >> (15 - sx) & 1;
            } else {
                const uint8_t *p0 = bm->planes[0] + sy * bm->bytesperrow;
                b0 = bm->depth > 0 && bm->planes[0] ? p0[sx >> 3] >> (7 - (sx & 7)) & 1 : 0;
                const uint8_t *p1 = bm->depth > 1 && bm->planes[1] ? bm->planes[1] + sy * bm->bytesperrow : nullptr;
                b1 = p1 ? p1[sx >> 3] >> (7 - (sx & 7)) & 1 : 0;
            }
            uint16_t bit = static_cast<uint16_t>(0x8000 >> (ox & 15));
            if (b0) line[ox >> 4] |= bit;
            if (b1) line[w + (ox >> 4)] |= bit;
        }
    }
    return es;
}

void gd_FreeSpriteData(gfx_env *env, gd_extsprite *es)
{
    if (!es) return;
    uint8_t *blk = reinterpret_cast<uint8_t *>(es) - 8;
    release(env, blk, *reinterpret_cast<uint32_t *>(blk));
}

int32_t gd_GetExtSpriteA(gfx_env *env, gd_extsprite *es, const gfx_tagitem *tags)
{
    if (!es) return -1;
    int num = static_cast<int32_t>(tag_data(tags, GD_GSTAG_SPRITE_NUM, static_cast<uint32_t>(-1)));
    gd_extsprite *pair = reinterpret_cast<gd_extsprite *>(static_cast<uintptr_t>(tag_data(tags, GD_GSTAG_ATTACHED, 0)));
    if (tag_data(tags, GD_GSTAG_SOFTSPRITE, 0)) return -1;     /* no soft sprites: not yet */
    int16_t n = claim(env, num, pair ? 2 : 1, &es->es, pair ? &pair->es : nullptr);
    if (n < 0) return -1;
    set_sprite_ptr(env, n, es->es.posctldata);
    if (pair) {
        pair->flags |= GD_SPRITE_ATTACHED;
        if (pair->es.posctldata) pair->es.posctldata[st(env)->spritewords] |= 0x80;
        set_sprite_ptr(env, n + 1, pair->es.posctldata);
    }
    return n;
}

int32_t gd_ChangeExtSpriteA(gfx_env *env, gd_viewport *vp, gd_extsprite *oldsp, gd_extsprite *newsp,
                            const gfx_tagitem *tags)
{
    (void)tags;
    state *s = st(env);
    if (!newsp) return 0;
    int num = oldsp ? oldsp->es.num : newsp->es.num;
    if (num < 0 || num > 7) return 0;
    int w = newsp->wordwidth ? newsp->wordwidth : 1;
    if (w != s->spritewords) {
        if (w != 1 && !aga(env)) return 0;
        s->spritewords = static_cast<uint16_t>(w);
        set_fmode(env);
    }
    newsp->es.num = static_cast<int16_t>(num);
    s->sprites[num] = &newsp->es;
    if (newsp->flags & GD_SPRITE_ATTACHED) newsp->es.posctldata[w] |= 0x80;
    place(env, vp, &newsp->es, static_cast<int16_t>(newsp->es.x), static_cast<int16_t>(newsp->es.y));
    set_sprite_ptr(env, num, newsp->es.posctldata);
    return 1;
}

} // extern "C"
