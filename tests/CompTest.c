/* SPDX-License-Identifier: MIT */
/* CompTest: OpenLayers' composited mode on the Workbench screen, from the
 * Shell. Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 *   CompTest INFO         is the Workbench screen composited (and how)
 *   CompTest TIME [N]     the whole screen composed again, and a full-screen window behind and in front, N times
 *   CompTest DRAG [N]     a 640 x 400 layer moved N times by 7,3 and back, as a drag does
 *   CompTest SCREEN       draws on the screen's own RastPort outside every layer, composes,
 *                         and reads the pixel back: the compose has drawn over it
 *   CompTest WINDOW [OPACITY] [SECS]   a window at OPACITY (0-255) with WA_Opacity, for SECS
 *   CompTest LATENCY [N]  how soon a RectFill in a window shows on the screen, N times
 *   CompTest CUSTOM [SECS]   a custom (game) screen on the Workbench's RTG mode: it stays classic
 *   CompTest AGA [SECS]      a custom screen in PAL high resolution (AGA): it stays classic
 *
 * Times are from timer.device's E-clock. On a classic (not composited)
 * screen TIME says so and DRAG times classic layers. Built with the
 * os32-gcc16 stove: m68k-amigaos-gcc -noixemul -O2 -o CompTest tests/CompTest.c */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Device *TimerBase;
static struct timerequest tr;

#define WA_Opacity (TAG_USER + 0x41430001UL)

static ULONG look(struct Layer *l, ULONG what, ULONG value)
{
    register struct Library *a6 __asm("a6") = LayersBase;
    register void *a0 __asm("a0") = l;
    register ULONG d0 __asm("d0") = what;
    register ULONG d1 __asm("d1") = value;
    __asm__ volatile("jsr -258(%%a6)" : "+r"(d0), "+r"(a0), "+r"(d1) : "r"(a6) : "a1", "cc", "memory");
    return d0;
}

static int ours(void)
{
    return LayersBase->lib_Version == 47 && LayersBase->lib_Revision >= 101;
}

/* microseconds from the E-clock, in integers */
static unsigned long long now_us(ULONG *freq)
{
    struct EClockVal e;
    *freq = ReadEClock(&e);
    return (((unsigned long long)e.ev_hi << 32) | e.ev_lo) * 1000000ULL / *freq;
}

/* composited: the screen's bar layer answers LAYERLOOK_QUERY */
static int composited(struct Screen *s)
{
    struct Layer *l = s->LayerInfo.top_layer;
    if (!ours() || !l) return 0;
    return look(l, 0x100, 0) != ~0UL;
}

int main(int argc, char **argv)
{
    struct Screen *s;
    const char *cmd = argc > 1 ? argv[1] : "INFO";
    int n = argc > 2 ? atoi(argv[2]) : 0, rc = 0;
    ULONG f;

    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0)) return 20;
    TimerBase = tr.tr_node.io_Device;
    if (!(s = LockPubScreen(NULL))) return 20;
    printf("layers.library %d.%d, screen %dx%d depth %d: %s\n", LayersBase->lib_Version, LayersBase->lib_Revision,
           s->Width, s->Height, (int)GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH),
           composited(s) ? "composited" : "classic");
    if (!stricmp(cmd, "INFO")) {
        struct Layer *l;
        int k = 0;
        for (l = s->LayerInfo.top_layer; l; l = l->back, k++) {
            ULONG q = ours() ? look(l, 0x100, 0) : ~0UL;
            printf("layer %d: %d,%d-%d,%d flags $%04x window %p look %s opacity %d bits %x\n", k, l->bounds.MinX,
                   l->bounds.MinY, l->bounds.MaxX, l->bounds.MaxY, l->Flags, l->Window,
                   q == ~0UL ? "classic" : "composited", (int)(q == ~0UL ? 255 : q & 255), (int)(q == ~0UL ? 0 : (q >> 8) & 255));
        }
    } else if (!stricmp(cmd, "TIME")) {
        /* the whole screen again: SetLayerInfoBounds composes all of a
         * composited screen (classic layers cut the stack again and draw
         * nothing); then a window as big as the screen sent behind and
         * brought to the front, N times each (classic layers copy it out
         * and back; composited ones compose all of it) */
        unsigned long long t0, t1;
        struct Layer *l;
        int i;
        if (!n) n = 20;
        t0 = now_us(&f);
        for (i = 0; i < n; i++) SetLayerInfoBounds(&s->LayerInfo, NULL);
        t1 = now_us(&f);
        printf("%s, the whole screen again: %d in %lu us, %lu us each\n", composited(s) ? "composited" : "classic", n,
               (unsigned long)(t1 - t0), (unsigned long)((t1 - t0) / n));
        l = CreateUpfrontLayer(&s->LayerInfo, s->RastPort.BitMap, 0, s->BarHeight + 1, s->Width - 1, s->Height - 1,
                               LAYERSMART, NULL);
        if (!l) { printf("no layer\n"); rc = 10; goto out; }
        SetAPen(l->rp, 3);
        RectFill(l->rp, 0, 0, s->Width - 1, s->Height - s->BarHeight - 2);
        Delay(10);
        t0 = now_us(&f);
        for (i = 0; i < n; i++) {
            BehindLayer(0, l);
            UpfrontLayer(0, l);
        }
        t1 = now_us(&f);
        printf("%s, a full-screen window behind and in front: %d of each in %lu us, %lu us each\n",
               composited(s) ? "composited" : "classic", n, (unsigned long)(t1 - t0), (unsigned long)((t1 - t0) / (2 * n)));
        DeleteLayer(0, l);
    } else if (!stricmp(cmd, "DRAG")) {
        struct Layer *l;
        unsigned long long t0, t1;
        int i;
        if (!n) n = 100;
        LockLayerInfo(&s->LayerInfo);
        l = CreateUpfrontLayer(&s->LayerInfo, s->RastPort.BitMap, 200, 200, 839, 599, LAYERSMART, NULL);
        UnlockLayerInfo(&s->LayerInfo);
        if (!l) { printf("no layer\n"); rc = 10; goto out; }
        SetAPen(l->rp, 2);
        RectFill(l->rp, 0, 0, 639, 399);
        SetAPen(l->rp, 1);
        for (i = 0; i < 400; i += 8) { Move(l->rp, 0, i); Draw(l->rp, 639, 399 - i); }
        if (composited(s)) look(l, 1, 1);
        Delay(10);
        t0 = now_us(&f);
        for (i = 0; i < n; i++) MoveLayer(0, l, (i & 1) ? -7 : 7, (i & 1) ? -3 : 3);
        t1 = now_us(&f);
        printf("%s drag: %d moves of a 640x400 layer in %lu us, %lu us each\n",
               composited(s) ? "composited" : "classic", n, (unsigned long)(t1 - t0), (unsigned long)((t1 - t0) / n));
        DeleteLayer(0, l);
    } else if (!stricmp(cmd, "SCREEN")) {
        /* the top left 8 x 8 of the bar is a layer (the bar's); the
         * screen's RastPort draws there all the same, outside the layers'
         * ClipRects */
        struct RastPort *rp = &s->RastPort;
        LONG before, after, drawn;
        int x = s->Width - 4, y = 2;
        before = ReadPixel(rp, x, y);
        SetAPen(rp, before == 1 ? 2 : 1);
        RectFill(rp, x - 3, y - 2, x, y + 2);
        drawn = ReadPixel(rp, x, y);
        if (composited(s)) SetLayerInfoBounds(&s->LayerInfo, NULL);     /* composes all of it */
        else Delay(2);
        after = ReadPixel(rp, x, y);
        printf("screen pixel %d,%d: %d before, %d drawn, %d after the compose: %s\n", x, y, (int)before, (int)drawn, (int)after,
               composited(s) ? (after == before ? "drawn over" : "NOT drawn over") : "classic");
        if (composited(s) && after != before) rc = 5;
    } else if (!stricmp(cmd, "LATENCY")) {
        /* how soon what a window draws shows on the screen: RectFill in a
         * window, then the screen's own pixel there read until it changes */
        struct Window *w = OpenWindowTags(NULL, WA_Left, 400, WA_Top, 300, WA_Width, 300, WA_Height, 200, WA_Title,
                                          (ULONG)"Latency", WA_DragBar, TRUE, WA_PubScreen, (ULONG)s, TAG_DONE);
        unsigned long long t0, t1, worst = 0, sum = 0;
        int i, k, x = 400 + 150, y = 300 + 100;
        if (!n) n = 10;
        if (!w) { rc = 10; goto out; }
        Delay(25);
        for (i = 0; i < n; i++) {
            LONG want;
            SetAPen(w->RPort, (i & 1) ? 1 : 2);
            t0 = now_us(&f);
            RectFill(w->RPort, 100, 60, 200, 140);
            want = ReadPixel(w->RPort, 150, 100);
            for (k = 0; k < 200000 && ReadPixel(&s->RastPort, x, y) != want; k++)
                ;
            t1 = now_us(&f);
            sum += t1 - t0;
            if (t1 - t0 > worst) worst = t1 - t0;
            Delay(5);
        }
        printf("%s: drawn to shown %lu us on average, %lu us at worst (%d draws)\n",
               composited(s) ? "composited" : "classic", (unsigned long)(sum / n), (unsigned long)worst, n);
        CloseWindow(w);
    } else if (!stricmp(cmd, "CUSTOM") || !stricmp(cmd, "AGA")) {
        /* a game's screen: custom (not public), on the Workbench's RTG mode or
         * on PAL high resolution (AGA), with a framed window */
        int aga = !stricmp(cmd, "AGA"), secs = n ? n : 10, i;
        ULONG id = aga ? 0x00029000UL : GetVPModeID(&s->ViewPort);
        struct Screen *g = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, aga ? 640 : s->Width, SA_Height,
                                          aga ? 256 : s->Height, SA_Depth, aga ? 4 : 16, SA_Type, CUSTOMSCREEN,
                                          SA_Title, (ULONG)(aga ? "AGA game screen" : "RTG game screen (custom)"),
                                          SA_ShowTitle, TRUE, TAG_DONE);
        struct Window *w = g ? OpenWindowTags(NULL, WA_CustomScreen, (ULONG)g, WA_Left, 40, WA_Top, 40, WA_Width,
                                              aga ? 400 : 700, WA_Height, aga ? 150 : 400, WA_Title,
                                              (ULONG)"A game's window", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                                              TAG_DONE) : NULL;
        if (!w) { printf("no %s screen\n", cmd); if (g) CloseScreen(g); rc = 10; goto out; }
        for (i = 0; i < 8; i++) {
            SetAPen(w->RPort, i + 1);
            RectFill(w->RPort, w->BorderLeft + 8 + i * 40, w->BorderTop + 8, w->BorderLeft + 40 + i * 40,
                     w->Height - w->BorderBottom - 8);
        }
        printf("%s screen %08lx: %s (window layer %s)\n", cmd, (unsigned long)id, composited(g) ? "composited" : "classic",
               ours() && look(w->WLayer, 0x100, 0) != ~0UL ? "composited" : "classic");
        if (composited(g)) rc = 5;
        Delay(secs * 50);
        CloseWindow(w);
        CloseScreen(g);
    } else if (!stricmp(cmd, "WINDOW")) {
        ULONG op = n ? (ULONG)n : 160;
        int secs = argc > 3 ? atoi(argv[3]) : 20;
        struct Window *w = OpenWindowTags(NULL, WA_Left, 300, WA_Top, 260, WA_Width, 520, WA_Height, 300,
                                          WA_Title, (ULONG)"Translucent: WA_Opacity", WA_DragBar, TRUE,
                                          WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_PubScreen, (ULONG)s,
                                          WA_Opacity, op, WA_IDCMP, IDCMP_CLOSEWINDOW, TAG_DONE);
        if (w) {
            int i;
            for (i = 0; i < 12; i++) {
                SetAPen(w->RPort, (i & 3) + 1);
                RectFill(w->RPort, w->BorderLeft + 10 + i * 40, w->BorderTop + 10, w->BorderLeft + 40 + i * 40,
                         w->Height - w->BorderBottom - 10);
            }
            SetAPen(w->RPort, 1);
            Move(w->RPort, w->BorderLeft + 12, w->BorderTop + 40);
            Text(w->RPort, "WA_Opacity", 10);
            printf("window at opacity %d: look %08x\n", (int)op, (unsigned)(ours() ? look(w->WLayer, 0x100, 0) : 0));
            Delay(secs * 50);
            CloseWindow(w);
        } else rc = 10;
    }
out:
    UnlockPubScreen(NULL, s);
    CloseDevice((struct IORequest *)&tr);
    return rc;
}
