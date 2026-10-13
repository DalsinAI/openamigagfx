#!/usr/bin/env python3
"""OpenGfx graphics.library: makes gfx_vectors.h (the vector table, in
graphics.lvo's order) and gfx_stubs.c (a stub for every gfx_lvo_, gfx_init_,
gfx_exit_ and gfx_hook_ symbol no area's objects define, so the library
links with any subset of the areas; OWNERS.md).

  gen_stubs.py graphics.lvo DEFINED OUTDIR

DEFINED is a file of the symbols the areas' objects define (nm output or
one name a line; a leading underscore is ignored). MIT, Dalsin Limited."""
import re, sys

def lvo_names(path):
    out = []
    for line in open(path):
        f = line.split("#")[0].split()
        if not f: continue
        out.append("private_%d" % -int(f[0]) if f[1] == "private" else f[1])
    return out

# the hooks' stubs: what gfx_env.h says each does when its area is missing
HOOKS = {
    "gfx_init_face": "int gfx_init_face(struct gfx_env *env) { (void)env; return 1; }",
    "gfx_init_display": "int gfx_init_display(struct gfx_env *env) { (void)env; return 1; }",
    "gfx_init_text": "int gfx_init_text(struct gfx_env *env) { (void)env; return 1; }",
    "gfx_init_gels": "int gfx_init_gels(struct gfx_env *env) { (void)env; return 1; }",
    "gfx_exit_face": "void gfx_exit_face(struct gfx_env *env) { (void)env; }",
    "gfx_exit_display": "void gfx_exit_display(struct gfx_env *env) { (void)env; }",
    "gfx_exit_text": "void gfx_exit_text(struct gfx_env *env) { (void)env; }",
    "gfx_exit_gels": "void gfx_exit_gels(struct gfx_env *env) { (void)env; }",
    "gfx_hook_vp_colors": "void gfx_hook_vp_colors(struct gfx_env *env, void *vp, uint32_t first, uint32_t count)\n"
                          "{ (void)env; (void)vp; (void)first; (void)count; }",
    # a TextFont: tf_YSize at 20, tf_XSize at 24, tf_Baseline at 26
    "gfx_hook_set_font": "void gfx_hook_set_font(struct gfx_env *env, struct gfx_rastport *rp, void *font)\n"
                         "{\n    const uint8_t *f = (const uint8_t *)font;\n    (void)env;\n    if (!f) return;\n"
                         "    rp->font = font;\n    rp->txheight = (uint16_t)(f[20] << 8 | f[21]);\n"
                         "    rp->txwidth = (uint16_t)(f[24] << 8 | f[25]);\n"
                         "    rp->txbaseline = (uint16_t)(f[26] << 8 | f[27]);\n}",
    "gfx_hook_blt_bitmap": "int32_t gfx_hook_blt_bitmap(struct gfx_env *env, struct gfx_bitmap *src, int32_t sx, int32_t sy,\n"
                           "    struct gfx_bitmap *dst, int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm,\n"
                           "    uint32_t mask, uint8_t *temp)\n{ (void)env; (void)src; (void)sx; (void)sy; (void)dst; (void)dx;\n"
                           "  (void)dy; (void)w; (void)h; (void)minterm; (void)mask; (void)temp; return -1; }",
    "gfx_hook_wait_blit": "void gfx_hook_wait_blit(struct gfx_env *env) { (void)env; }",
    "gfx_hook_vsprites": "void gfx_hook_vsprites(struct gfx_env *env, void *vp, const void *slots, uint32_t n)\n"
                         "{ (void)env; (void)vp; (void)slots; (void)n; }",
}

def main():
    names = lvo_names(sys.argv[1])
    defined = set()
    for line in open(sys.argv[2]):
        for w in line.split():
            if re.match(r"^_?gfx_(lvo|init|exit|hook)_\w+$", w):
                defined.add(w.lstrip("_") if w.startswith("_gfx") else w)
    out = sys.argv[3]
    with open(out + "/gfx_vectors.h", "w") as v:
        v.write("/* made by gen_stubs.py from graphics.lvo: do not edit */\n")
        for n in names: v.write("extern void gfx_lvo_%s(void);\n" % n)
        v.write("#define GFX_VECTORS \\\n")
        for i, n in enumerate(names): v.write("    (APTR)gfx_lvo_%s, /* -%d */ \\\n" % (n, 30 + 6 * i))
        v.write("\n")
    stubbed = []
    with open(out + "/gfx_stubs.c", "w") as s:
        s.write("/* made by gen_stubs.py: what no area defines yet */\n#include \"gfx_env.h\"\n\n")
        for n in names:
            if "gfx_lvo_" + n not in defined:
                s.write("uint32_t gfx_lvo_%s(void) { return 0; }\n" % n)
                stubbed.append(n)
        for sym, body in HOOKS.items():
            if sym not in defined:
                s.write(body + "\n")
                stubbed.append(sym)
    print("gen_stubs: %d LVOs, %d stubbed: %s" % (len(names), len(stubbed), " ".join(stubbed) or "none"))

main()
