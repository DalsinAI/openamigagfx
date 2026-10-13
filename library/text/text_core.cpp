// SPDX-License-Identifier: MIT
// OpenGfx text: graphics.library's font and text calls (text_core.h).
// Copyright (c) 2026 Dalsin Limited. MIT.
//
// Clean-room, from the autodocs, the NDK 3.2 layouts and OS 3.2's answers
// measured by tests/GfxCoreABITest (TEXTPROBE) and tests/TextABITest
// (PROBE). Where neither settles a corner the comment says "ABI test" and
// TextABITest has a PROBE line recording OS 3.2's answer; README.md's
// "Edge cases" table lists them.
//
// Lean C++: no exceptions, RTTI, statics, globals or allocation of its own;
// it calls nothing but libgcc and the env.
#include "text_types.h"
#include "text_core.h"
#include "topaz_data.h"

namespace {

// ---- exec lists ---------------------------------------------------------

void add_head(List *l, Node *n)
{
    Node *h = l->lh_Head;
    n->ln_Succ = h;
    n->ln_Pred = reinterpret_cast<Node *>(&l->lh_Head);
    h->ln_Pred = n;
    l->lh_Head = n;
}

void remove(Node *n)
{
    n->ln_Pred->ln_Succ = n->ln_Succ;
    n->ln_Succ->ln_Pred = n->ln_Pred;
}

bool in_list(const List *l, const Node *n)
{
    for (const Node *p = l->lh_Head; p->ln_Succ; p = p->ln_Succ)
        if (p == n) return true;
    return false;
}

// ---- tags ----------------------------------------------------------------

// The item with tag, through TAG_MORE, TAG_SKIP and TAG_IGNORE; 0 if none.
const TagItem *find_tag(ULONG tag, const TagItem *t)
{
    while (t) {
        switch (t->ti_Tag) {
        case TAG_DONE: return nullptr;
        case TAG_MORE: t = reinterpret_cast<const TagItem *>(t->ti_Data); continue;
        case TAG_SKIP: t += 1 + t->ti_Data; continue;
        case TAG_IGNORE: break;
        default:
            if (t->ti_Tag == tag) return t;
        }
        t++;
    }
    return nullptr;
}

// ---- the font's extension ------------------------------------------------

// tf_Extension (tf_Message.mn_ReplyPort) when it is a TextFontExtension
// made for this font: the match word and the back pointer agree.
TextFontExtension *extension_of(const TextFont *tf)
{
    auto *e = reinterpret_cast<TextFontExtension *>(tf->tf_Message.mn_ReplyPort);
    if (e && e->tfe_MatchWord == TE_MATCHWORD && e->tfe_BackPtr == tf) return e;
    return nullptr;
}

// ---- glyphs ---------------------------------------------------------------
// Character c is glyph c - tf_LoChar, or outside tf_LoChar..tf_HiChar the
// glyph after tf_HiChar's (measured, GfxCoreABITest TEXTPROBE).

LONG glyph_of(const TextFont *tf, UBYTE c)
{
    if (c < tf->tf_LoChar || c > tf->tf_HiChar) return LONG(tf->tf_HiChar) - tf->tf_LoChar + 1;
    return LONG(c) - tf->tf_LoChar;
}
LONG glyph_off(const TextFont *tf, LONG g) { return static_cast<const UWORD *>(tf->tf_CharLoc)[2 * g]; }
LONG glyph_width(const TextFont *tf, LONG g) { return static_cast<const UWORD *>(tf->tf_CharLoc)[2 * g + 1]; }
LONG glyph_kern(const TextFont *tf, LONG g)
{
    return tf->tf_CharKern ? static_cast<const WORD *>(tf->tf_CharKern)[g] : 0;
}
LONG glyph_space(const TextFont *tf, LONG g)
{
    return tf->tf_CharSpace ? static_cast<const WORD *>(tf->tf_CharSpace)[g] : LONG(tf->tf_XSize);
}

// Bit x of row r of a font plane (tf_Modulo bytes a row).
int plane_bit(const void *data, const TextFont *tf, LONG r, LONG x)
{
    if (!data) return 0;
    const UBYTE *row = static_cast<const UBYTE *>(data) + ULONG(r) * tf->tf_Modulo;
    if (ULONG(x >> 3) >= tf->tf_Modulo) return 0;
    return row[x >> 3] >> (7 - (x & 7)) & 1;
}

bool is_color(const TextFont *tf) { return tf->tf_Style & FSF_COLORFONT; }
bool is_rev(const TextFont *tf) { return tf->tf_Flags & FPF_REVPATH; }

// ---- a string as drawn ----------------------------------------------------
// The characters in the order they land left to right: s[0], s[step], ...
// A font drawn right to left (FPF_REVPATH) lands its string last
// character first (ours, ABI test).
struct Run {
    const UBYTE *s;
    LONG step, n;
    UBYTE at(LONG i) const { return s[i * step]; }
};

struct Style {
    const TextFont *tf;
    LONG spacing, smear;
    UBYTE algo;                 // FSF_UNDERLINED | FSF_BOLD | FSF_ITALIC
};

Style style_of(const RastPort *rp)
{
    Style st;
    st.tf = rp->Font;
    st.spacing = rp->TxSpacing;
    st.algo = rp->AlgoStyle & (FSF_UNDERLINED | FSF_BOLD | FSF_ITALIC);
    st.smear = st.algo & FSF_BOLD ? LONG(st.tf->tf_BoldSmear) : 0;
    return st;
}

// How far glyph g moves the pen: CharSpace's width (tf_XSize without one),
// the kerning and the RastPort's TxSpacing.
LONG advance(const Style &st, LONG g) { return glyph_space(st.tf, g) + glyph_kern(st.tf, g) + st.spacing; }

// What a run of characters measures, kept as it grows at either end.
struct Measure {
    LONG pos = 0;               // where the pen ends, from where it began
    LONG lo = 0, hi = 0;        // the image pixels' columns, when any
    bool any = false;
    LONG n = 0;

    void image(LONG left, LONG w)
    {
        if (!w) return;
        if (!any || left < lo) lo = left;
        if (!any || left + w - 1 > hi) hi = left + w - 1;
        any = true;
    }
    void append(const Style &st, LONG g)
    {
        image(pos + glyph_kern(st.tf, g), glyph_width(st.tf, g));
        pos += advance(st, g);
        n++;
    }
    void prepend(const Style &st, LONG g)
    {
        LONG a = advance(st, g);
        lo += a;
        hi += a;
        pos += a;
        image(glyph_kern(st.tf, g), glyph_width(st.tf, g));
        n++;
    }
};

// The text's box from the pen, x0..x1 inclusive (0..-1 for no characters):
// from the pen, or the leftmost image pixel if that is further left, to the
// end of the width, or the rightmost image pixel if that is further right;
// bold adds tf_BoldSmear on the right, italic tf_Baseline / 2 on the right
// and (tf_YSize - tf_Baseline + 1) / 2 on the left (measured, TEXTPROBE).
void box(const Style &st, const Measure &m, LONG *x0, LONG *x1)
{
    if (!m.n) {
        *x0 = 0;
        *x1 = -1;
        return;
    }
    LONG lo = m.any && m.lo < 0 ? m.lo : 0;
    LONG hi = m.any ? m.hi : -0x7FFFFFFF;
    if (m.pos - 1 > hi) hi = m.pos - 1;
    hi += st.smear;
    if (st.algo & FSF_ITALIC) {
        hi += st.tf->tf_Baseline / 2;
        lo -= (LONG(st.tf->tf_YSize) - st.tf->tf_Baseline + 1) / 2;
    }
    *x0 = lo;
    *x1 = hi;
}

Measure measure(const Style &st, const Run &run)
{
    Measure m;
    for (LONG i = 0; i < run.n; i++) m.append(st, glyph_of(st.tf, run.at(i)));
    return m;
}

// The run as drawn for string s, n characters, in the font's direction.
Run run_of(const TextFont *tf, const UBYTE *s, LONG n)
{
    if (is_rev(tf)) return Run{s + n - 1, -1, n};
    return Run{s, 1, n};
}

void extent_of(const Style &st, const Measure &m, TextExtent *te)
{
    LONG x0, x1, shift = is_rev(st.tf) ? -m.pos : 0;
    box(st, m, &x0, &x1);
    te->te_Width = WORD(m.pos);
    te->te_Extent.MinX = WORD(x0 + shift);
    te->te_Extent.MaxX = WORD(x1 + shift);
    if (!m.n) {
        te->te_Height = 0;
        te->te_Extent.MinX = 0;
        te->te_Extent.MaxX = -1;
        te->te_Extent.MinY = 0;
        te->te_Extent.MaxY = -1;
        return;
    }
    te->te_Height = WORD(st.tf->tf_YSize);
    te->te_Extent.MinY = WORD(-LONG(st.tf->tf_Baseline));
    te->te_Extent.MaxY = WORD(LONG(st.tf->tf_YSize) - 1 - st.tf->tf_Baseline);
}

// ---- drawing ----------------------------------------------------------------
// The text is drawn as the core draws it (library/core/gfx_rp.c, measured
// on OS 3.2): row r of each glyph image lands at the pen plus its kerning,
// italic moves row r right by floor((tf_Baseline - r) / 2), bold ORs the
// images again tf_BoldSmear pixels right, underline fills the row below
// the baseline across the box leaving a pixel clear each side of the
// image's pixels, and the whole is a template through the DrawMode over
// the box. A colour font (FSF_COLORFONT, a ColorTextFont) lands each
// pixel's own pen instead (ours, ABI test): its planes go to the bitmap's
// planes in ctf_PlanePick, the others take ctf_PlaneOnOff's bits, a pen of
// ctf_FgColor becomes FgPen with CTF_MAPCOLOR, a later image (a bold
// smear, the next glyph) over an earlier one wins, the underline is FgPen,
// and pixels with no plane bit set are the background.

constexpr LONG CHUNK = 128;     // columns of a row built at a time

LONG slant(const Style &st, LONG r)
{
    if (!(st.algo & FSF_ITALIC)) return 0;
    LONG d = LONG(st.tf->tf_Baseline) - r;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

// Pen of pixel x of row r of glyph image data at bit offset off, or -1
// for no ink.
LONG ink_at(const TextFont *tf, LONG r, LONG x)
{
    if (!is_color(tf)) return plane_bit(tf->tf_CharData, tf, r, x) ? 0 : -1;
    auto *ctf = reinterpret_cast<const ColorTextFont *>(tf);
    LONG depth = ctf->ctf_Depth > 8 ? 8 : ctf->ctf_Depth, p = 0, v = 0;
    bool ink = false;
    for (LONG q = 0; q < 8; q++) {
        if (ctf->ctf_PlanePick >> q & 1) {
            if (p < depth && plane_bit(ctf->ctf_CharData[p], tf, r, x)) {
                v |= 1L << q;
                ink = true;
            }
            p++;
        } else
            v |= ctf->ctf_PlaneOnOff & (1L << q);
    }
    if (!ink) return -1;
    if (ctf->ctf_Flags & CTF_MAPCOLOR && v == ctf->ctf_FgColor) v = 0x100; // FgPen
    return v;
}

void draw(const gt_env *env, RastPort *rp, const Style &st, const Run &run, LONG ox, LONG x0, LONG x1)
{
    const TextFont *tf = st.tf;
    const bool color = is_color(tf);
    const UBYTE mode = UBYTE(rp->DrawMode);
    const ULONG fg = UBYTE(rp->FgPen), bg = UBYTE(rp->BgPen);
    const LONG top = LONG(rp->cp_y) - tf->tf_Baseline;
    WORD ink[CHUNK + 2];        // column cx - 1 + i: a pen, 0x100 FgPen, or -1

    for (LONG r = 0; r < LONG(tf->tf_YSize); r++) {
        const bool uline = st.algo & FSF_UNDERLINED && r == LONG(tf->tf_Baseline) + 1;
        const LONG sl = slant(st, r);
        for (LONG cx = x0; cx <= x1; cx += CHUNK) {
            for (LONG i = 0; i < CHUNK + 2; i++) ink[i] = -1;
            LONG pos = 0;
            for (LONG i = 0; i < run.n; i++) {
                LONG g = glyph_of(tf, run.at(i)), w = glyph_width(tf, g), off = glyph_off(tf, g);
                LONG left = pos + glyph_kern(tf, g) + sl;
                pos += advance(st, g);
                for (LONG k = 0;; k = st.smear) {
                    LONG a = left + k;
                    if (a + w > cx - 1 && a < cx + CHUNK + 1)
                        for (LONG j = 0; j < w; j++) {
                            LONG c = a + j - (cx - 1);
                            if (c < 0 || c >= CHUNK + 2) continue;
                            LONG v = ink_at(tf, r, off + j);
                            if (v >= 0) ink[c] = WORD(v);
                        }
                    if (k == st.smear) break;
                }
            }
            LONG end = cx + CHUNK - 1 > x1 ? x1 : cx + CHUNK - 1;
            for (LONG x = cx; x <= end; x++) {
                LONG c = x - (cx - 1), v = ink[c];
                bool u = uline && v < 0 && ink[c - 1] < 0 && ink[c + 1] < 0;
                LONG px = ox + x, py = top + r;
                if (!color) {
                    bool b = v >= 0 || u;
                    if (mode & INVERSVID) b = !b;
                    if (mode & COMPLEMENT) {
                        if (b || mode & JAM2) env->write_pixel(env->ctx, rp, px, py, 0xFF, 1);
                    } else if (b)
                        env->write_pixel(env->ctx, rp, px, py, fg, 0);
                    else if (mode & JAM2)
                        env->write_pixel(env->ctx, rp, px, py, bg, 0);
                    continue;
                }
                if (u) v = 0x100;
                if (mode & COMPLEMENT) {
                    if (v >= 0) env->write_pixel(env->ctx, rp, px, py, v == 0x100 ? 0xFF : ULONG(v), 1);
                    else if (mode & JAM2) env->write_pixel(env->ctx, rp, px, py, 0xFF, 1);
                } else if (v >= 0)
                    env->write_pixel(env->ctx, rp, px, py, v == 0x100 ? fg : ULONG(v), 0);
                else if (mode & JAM2)
                    env->write_pixel(env->ctx, rp, px, py, bg, 0);
            }
        }
    }
}

// ---- matching ---------------------------------------------------------------

UBYTE lower(UBYTE c) { return c >= 'A' && c <= 'Z' ? UBYTE(c + 32) : c; }

// Font names compare without case (ours, ABI test).
bool same_name(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (;; a++, b++) {
        if (lower(UBYTE(*a)) != lower(UBYTE(*b))) return false;
        if (!*a) return true;
    }
}

const TagItem *tags_of(const TextAttr *ta)
{
    return ta->ta_Style & FSF_TAGGED ? reinterpret_cast<const TTextAttr *>(ta)->tta_Tags : nullptr;
}

} // namespace

// ---- the calls -----------------------------------------------------------

extern "C" {

// ClearEOL: from the pen to the right edge, over the rows text at the pen
// covers (cp_y - TxBaseline, TxHeight rows), in BgPen when the DrawMode
// has JAM2, else in pen 0 (the autodoc's "DrawMode 2": ABI test).
void gt_ClearEOL(const gt_env *env, RastPort *rp)
{
    LONG w, h, y0 = LONG(rp->cp_y) - rp->TxBaseline, y1 = y0 + LONG(rp->TxHeight) - 1, x0 = rp->cp_x;
    ULONG pen = rp->DrawMode & JAM2 ? UBYTE(rp->BgPen) : 0;
    env->rp_size(env->ctx, rp, &w, &h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 > h - 1) y1 = h - 1;
    if (x0 <= w - 1 && y0 <= y1) env->fill(env->ctx, rp, x0, y0, w - 1, y1, pen);
}

// ClearScreen: ClearEOL, then every row below the text's.
void gt_ClearScreen(const gt_env *env, RastPort *rp)
{
    LONG w, h, y0 = LONG(rp->cp_y) - rp->TxBaseline + rp->TxHeight;
    ULONG pen = rp->DrawMode & JAM2 ? UBYTE(rp->BgPen) : 0;
    gt_ClearEOL(env, rp);
    env->rp_size(env->ctx, rp, &w, &h);
    if (y0 < 0) y0 = 0;
    if (w > 0 && y0 <= h - 1) env->fill(env->ctx, rp, 0, y0, w - 1, h - 1, pen);
}

int16_t gt_TextLength(const gt_env *env, RastPort *rp, const uint8_t *s, uint32_t n)
{
    (void)env;
    if (!rp->Font) return 0;
    Style st = style_of(rp);
    return WORD(measure(st, run_of(st.tf, s, LONG(UWORD(n)))).pos);
}

void gt_TextExtent(const gt_env *env, RastPort *rp, const uint8_t *s, uint32_t n, TextExtent *te)
{
    (void)env;
    if (!rp->Font) {
        te->te_Width = te->te_Height = 0;
        te->te_Extent.MinX = te->te_Extent.MinY = 0;
        te->te_Extent.MaxX = te->te_Extent.MaxY = -1;
        return;
    }
    Style st = style_of(rp);
    extent_of(st, measure(st, run_of(st.tf, s, LONG(UWORD(n)))), te);
}

// Text: a one-colour font drawn left to right goes to the core
// (env->mono_text); the rest, and what the core does not take, is drawn
// here pixel by pixel. The pen moves on by the width, or back by it for a
// font drawn right to left, whose string lands to the left of the pen
// (ours, ABI test). No font: nothing (OS 3.2 reads through the pointer).
int32_t gt_Text(const gt_env *env, RastPort *rp, const uint8_t *s, uint32_t n)
{
    TextFont *tf = rp->Font;
    LONG cnt = LONG(UWORD(n));
    if (!tf) return 0;
    if (!is_color(tf) && !is_rev(tf) && env->mono_text && env->mono_text(env->ctx, rp, s, ULONG(cnt))) return 0;
    Style st = style_of(rp);
    Run run = run_of(tf, s, cnt);
    Measure m = measure(st, run);
    LONG x0, x1, origin = rp->cp_x;
    box(st, m, &x0, &x1);
    if (is_rev(tf)) origin -= m.pos;
    if (x0 <= x1) draw(env, rp, st, run, origin, x0, x1);
    rp->cp_x = WORD(is_rev(tf) ? rp->cp_x - m.pos : rp->cp_x + m.pos);
    return 0;
}

// SetFont: the font and its sizes into the RastPort; AlgoStyle and
// TxSpacing stay (ABI test). A null font changes nothing and answers 0.
int32_t gt_SetFont(const gt_env *env, RastPort *rp, TextFont *tf)
{
    (void)env;
    if (!tf) return 0;
    rp->Font = tf;
    rp->TxHeight = tf->tf_YSize;
    rp->TxWidth = tf->tf_XSize;
    rp->TxBaseline = tf->tf_Baseline;
    return 1;
}

// WeighTAMatch (ours, ABI test for OS 3.2's numbers): MAXFONTMATCHWEIGHT
// for an exact match, less a penalty for each difference, so that the
// size counts most, then the style, then the flags:
//   256 a point of size (up to 100), 128 more when the target is larger,
//   64 FSF_EXTENDED, 32 FSF_ITALIC, 16 FSF_BOLD, 8 FSF_UNDERLINED,
//   4 FPF_REVPATH, 2 FPF_PROPORTIONAL,
//   1 FPF_TALLDOT or FPF_WIDEDOT, or a TA_DeviceDPI both give that differs.
// The names are not weighed. Never below 6912, so never 0.
int32_t gt_WeighTAMatch(const gt_env *env, const TextAttr *req, const TextAttr *target, const TagItem *ttags)
{
    (void)env;
    LONG dy = LONG(target->ta_YSize) - LONG(req->ta_YSize), pen = 0;
    LONG ady = dy < 0 ? -dy : dy;
    pen += (ady > 100 ? 100 : ady) * 256;
    if (dy > 0) pen += 128;
    UBYTE ds = UBYTE((req->ta_Style ^ target->ta_Style) & 0x0F);
    if (ds & FSF_EXTENDED) pen += 64;
    if (ds & FSF_ITALIC) pen += 32;
    if (ds & FSF_BOLD) pen += 16;
    if (ds & FSF_UNDERLINED) pen += 8;
    UBYTE df = UBYTE(req->ta_Flags ^ target->ta_Flags);
    if (df & FPF_REVPATH) pen += 4;
    if (df & FPF_PROPORTIONAL) pen += 2;
    bool dot = df & (FPF_TALLDOT | FPF_WIDEDOT);
    const TagItem *a = find_tag(TA_DeviceDPI, tags_of(req)), *b = find_tag(TA_DeviceDPI, ttags);
    if (a && b && a->ti_Data != b->ti_Data) dot = true;
    if (dot) pen += 1;
    return MAXFONTMATCHWEIGHT - pen;
}

// OpenFont: of the fonts in the list with the name asked for, the one
// WeighTAMatch weighs highest, the first in the list of equals (the newest
// added); its tf_Accessors counts up. 0 when no font has the name: other
// sizes are given (diskfont's OpenDiskFont checks the size itself).
TextFont *gt_OpenFont(const gt_env *env, const TextAttr *ta)
{
    if (!ta || !ta->ta_Name) return nullptr;
    TextFont *best = nullptr;
    LONG bw = -1;
    env->forbid(env->ctx);
    for (Node *n = env->fonts->lh_Head; n->ln_Succ; n = n->ln_Succ) {
        auto *tf = reinterpret_cast<TextFont *>(n);
        if (!same_name(ta->ta_Name, n->ln_Name)) continue;
        TextAttr t{n->ln_Name, tf->tf_YSize, tf->tf_Style, tf->tf_Flags};
        TextFontExtension *e = extension_of(tf);
        LONG w = gt_WeighTAMatch(env, ta, &t, e ? e->tfe_Tags : nullptr);
        if (w > bw) {
            bw = w;
            best = tf;
        }
    }
    if (best) best->tf_Accessors++;
    env->permit(env->ctx);
    return best;
}

// CloseFont: one accessor less, never below 0 (ours); a null font is
// allowed. The font stays in the list (diskfont expunges its own).
void gt_CloseFont(const gt_env *env, TextFont *tf)
{
    if (!tf) return;
    env->forbid(env->ctx);
    if (tf->tf_Accessors) tf->tf_Accessors--;
    env->permit(env->ctx);
}

// AskSoftStyle: the styles Text can make that the font does not have
// itself: underline, bold and italic (ABI test whether FSF_EXTENDED is
// among them on OS 3.2). No font: none.
uint32_t gt_AskSoftStyle(const gt_env *env, RastPort *rp)
{
    (void)env;
    if (!rp->Font) return 0;
    return ULONG(~rp->Font->tf_Style) & (FSF_UNDERLINED | FSF_BOLD | FSF_ITALIC);
}

// SetSoftStyle: the bits of style in enable that AskSoftStyle allows
// become AlgoStyle's; answers the style the text now has, the font's own
// and the algorithmic (ABI test).
uint32_t gt_SetSoftStyle(const gt_env *env, RastPort *rp, uint32_t style, uint32_t enable)
{
    ULONG m = enable & gt_AskSoftStyle(env, rp);
    rp->AlgoStyle = UBYTE((rp->AlgoStyle & ~m) | (style & m));
    return ULONG(rp->AlgoStyle) | (rp->Font ? rp->Font->tf_Style : 0);
}

// AskFont: the font's name and flags, the RastPort's TxHeight and the style
// the text has (the font's and AlgoStyle) without FSF_TAGGED, as a plain
// TextAttr (ABI test).
void gt_AskFont(const gt_env *env, RastPort *rp, TextAttr *ta)
{
    (void)env;
    TextFont *tf = rp->Font;
    ta->ta_Name = tf ? tf->tf_Message.mn_Node.ln_Name : nullptr;
    ta->ta_YSize = rp->TxHeight;
    ta->ta_Style = UBYTE(((tf ? tf->tf_Style : 0) | rp->AlgoStyle) & ~FSF_TAGGED);
    ta->ta_Flags = tf ? tf->tf_Flags : 0;
}

// AddFont: to the head of the list, an NT_FONT, no longer FPF_REMOVED.
// A font already in the list stays where it is (ours: OS 3.2 links it
// twice).
void gt_AddFont(const gt_env *env, TextFont *tf)
{
    if (!tf) return;
    env->forbid(env->ctx);
    if (!in_list(env->fonts, &tf->tf_Message.mn_Node)) {
        tf->tf_Message.mn_Node.ln_Type = NT_FONT;
        tf->tf_Flags &= UBYTE(~FPF_REMOVED);
        add_head(env->fonts, &tf->tf_Message.mn_Node);
    }
    env->permit(env->ctx);
}

// RemFont: out of the list, marked FPF_REMOVED; answers 1. A font not in
// the list, or whose extension says TE0F_NOREMFONT, stays and answers 0.
int32_t gt_RemFont(const gt_env *env, TextFont *tf)
{
    if (!tf) return 0;
    int32_t done = 0;
    env->forbid(env->ctx);
    TextFontExtension *e = extension_of(tf);
    if (!(e && e->tfe_Flags0 & TE0F_NOREMFONT) && in_list(env->fonts, &tf->tf_Message.mn_Node)) {
        remove(&tf->tf_Message.mn_Node);
        tf->tf_Flags |= FPF_REMOVED;
        done = 1;
    }
    env->permit(env->ctx);
    return done;
}

// TextFit: how many characters, from s on by dir (1 forward; -1 back from
// the last), fit: their box no wider than bitwidth and no taller than
// bitheight, and inside constraint when one is given. Characters are
// taken until the first that does not fit; te gets the box of those that
// do, as TextExtent gives it for them (no characters: 0 high). The count
// is a WORD's, as Text's (ABI test).
uint32_t gt_TextFit(const gt_env *env, RastPort *rp, const uint8_t *s, uint32_t n, TextExtent *te,
                    const TextExtent *c, int32_t dir, uint32_t bitwidth, uint32_t bitheight)
{
    (void)env;
    LONG cnt = LONG(UWORD(n));
    if (!rp->Font) {
        gt_TextExtent(env, rp, s, 0, te);
        return 0;
    }
    Style st = style_of(rp);
    const TextFont *tf = st.tf;
    bool prepend = (dir < 0) != is_rev(tf);
    LONG miny = -LONG(tf->tf_Baseline), maxy = LONG(tf->tf_YSize) - 1 - tf->tf_Baseline;
    Measure m, ok;
    if (ULONG(tf->tf_YSize) > bitheight || (c && (miny < c->te_Extent.MinY || maxy > c->te_Extent.MaxY)))
        cnt = 0;
    for (LONG i = 0; i < cnt; i++) {
        Measure t = m;
        LONG g = glyph_of(tf, s[i * dir]), x0, x1;
        if (prepend) t.prepend(st, g);
        else t.append(st, g);
        box(st, t, &x0, &x1);
        if (is_rev(tf)) {
            x0 -= t.pos;
            x1 -= t.pos;
        }
        if (x1 - x0 + 1 > LONG(bitwidth)) break;
        if (c && (x0 < c->te_Extent.MinX || x1 > c->te_Extent.MaxX)) break;
        m = t;
    }
    extent_of(st, m, te);
    return ULONG(m.n);
}

// FontExtent: the box of every glyph of the font (the default one too)
// from the pen: from the leftmost kerned image pixel, or the pen, to the
// rightmost image pixel or the end of the widest advance; te_Width the
// widest advance (CharSpace and kerning, no TxSpacing), te_Height
// tf_YSize. No algorithmic style (ABI test).
void gt_FontExtent(const gt_env *env, const TextFont *tf, TextExtent *te)
{
    (void)env;
    LONG lo = 0, hi = -1, wide = 0;
    for (LONG g = 0; g <= LONG(tf->tf_HiChar) - tf->tf_LoChar + 1; g++) {
        LONG k = glyph_kern(tf, g), w = glyph_width(tf, g), a = glyph_space(tf, g) + k;
        if (w) {
            if (k < lo) lo = k;
            if (k + w - 1 > hi) hi = k + w - 1;
        }
        if (a - 1 > hi) hi = a - 1;
        if (a > wide) wide = a;
    }
    te->te_Width = WORD(wide);
    te->te_Height = WORD(tf->tf_YSize);
    te->te_Extent.MinX = WORD(lo);
    te->te_Extent.MaxX = WORD(hi);
    te->te_Extent.MinY = WORD(-LONG(tf->tf_Baseline));
    te->te_Extent.MaxY = WORD(LONG(tf->tf_YSize) - 1 - tf->tf_Baseline);
}

// ExtendFont: gives the font a TextFontExtension (tf_Extension), keeping the
// reply port it replaces, with a copy of tags (utility's CloneTagItems);
// answers 1, or 0 when out of memory. A font that has one keeps it and its
// tags (ABI test).
int32_t gt_ExtendFont(const gt_env *env, TextFont *tf, const TagItem *tags)
{
    if (!tf) return 0;
    env->forbid(env->ctx);
    bool has = extension_of(tf);
    env->permit(env->ctx);
    if (has) return 1;
    auto *e = static_cast<TextFontExtension *>(env->alloc(env->ctx, sizeof(TextFontExtension)));
    if (!e) return 0;
    TagItem *copy = nullptr;
    if (tags && !(copy = env->clone_tags(env->ctx, tags))) {
        env->free(env->ctx, e, sizeof(TextFontExtension));
        return 0;
    }
    e->tfe_MatchWord = TE_MATCHWORD;
    e->tfe_BackPtr = tf;
    e->tfe_Tags = copy;
    env->forbid(env->ctx);
    if (extension_of(tf)) {     // made meanwhile by another task
        env->permit(env->ctx);
        if (copy) env->free_tags(env->ctx, copy);
        env->free(env->ctx, e, sizeof(TextFontExtension));
        return 1;
    }
    e->tfe_OrigReplyPort = tf->tf_Message.mn_ReplyPort;
    tf->tf_Message.mn_ReplyPort = reinterpret_cast<MsgPort *>(e);
    env->permit(env->ctx);
    return 1;
}

// StripFont: takes the extension away again, the reply port back.
void gt_StripFont(const gt_env *env, TextFont *tf)
{
    if (!tf) return;
    env->forbid(env->ctx);
    TextFontExtension *e = extension_of(tf);
    if (e) tf->tf_Message.mn_ReplyPort = e->tfe_OrigReplyPort;
    env->permit(env->ctx);
    if (!e) return;
    if (e->tfe_Tags) env->free_tags(env->ctx, e->tfe_Tags);
    env->free(env->ctx, e, sizeof(TextFontExtension));
}

// ---- the ROM fonts -----------------------------------------------------

static TextFont *rom_font(const gt_env *env, const gt_rom_font *f)
{
    auto *tf = static_cast<TextFont *>(env->alloc(env->ctx, sizeof(TextFont)));
    if (!tf) return nullptr;
    tf->tf_Message.mn_Node.ln_Type = NT_FONT;
    tf->tf_Message.mn_Node.ln_Name = const_cast<char *>(gt_topaz_name);
    tf->tf_Message.mn_Length = sizeof(TextFont);
    tf->tf_YSize = f->ysize;
    tf->tf_Style = FS_NORMAL;
    tf->tf_Flags = FPF_ROMFONT | FPF_DESIGNED;
    tf->tf_XSize = f->xsize;
    tf->tf_Baseline = f->baseline;
    tf->tf_BoldSmear = 1;
    tf->tf_LoChar = f->lochar;
    tf->tf_HiChar = f->hichar;
    tf->tf_CharData = const_cast<UBYTE *>(f->data);
    tf->tf_Modulo = f->modulo;
    tf->tf_CharLoc = const_cast<UWORD *>(f->loc);
    return tf;
}

TextFont *gt_init_rom_fonts(const gt_env *env, TextFont **t9)
{
    TextFont *a = rom_font(env, &gt_topaz8), *b = rom_font(env, &gt_topaz9);
    if (!a || !b) {
        if (a) env->free(env->ctx, a, sizeof(TextFont));
        if (b) env->free(env->ctx, b, sizeof(TextFont));
        return nullptr;
    }
    gt_AddFont(env, b);
    gt_AddFont(env, a);
    *t9 = b;
    return a;
}

void gt_exit_rom_fonts(const gt_env *env, TextFont *t8, TextFont *t9)
{
    TextFont *fonts[2] = {t8, t9};
    for (TextFont *tf : fonts) {
        if (!tf) continue;
        gt_RemFont(env, tf);
        gt_StripFont(env, tf);
        env->free(env->ctx, tf, sizeof(TextFont));
    }
}

} // extern "C"
