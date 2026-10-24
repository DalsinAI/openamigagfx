/* SPDX-License-Identifier: MIT */
/* ScreenDepthProbe: which screens the running ROM opens, by resolution and
 * depth, and one of them held on show. Copyright (c) 2026 AmigaChrome Team.
 * MIT.
 *
 * It began as the Team's probe of SysInfo 4.4 quitting at once on the open
 * ROM (24 October 2026): SysInfo opens an ExtNewScreen of 640 x 256, depth
 * 3, ViewModes 0x9020 (display ID 0x8020, Super-High Res), CUSTOMSCREEN |
 * SCREENBEHIND | SCREENQUIET | NS_EXTENDED, topaz 8, SA_Pens. On AGA that
 * needs the 4x bitplane fetch; with only the 1x fetch OpenScreen failed it
 * with OSERR_TOODEEP (7).
 *
 *   ScreenDepthProbe
 * prints, in this order:
 *   - intuition's and graphics' versions;
 *   - "mode ID: notavailable N, maxdepth N" for lores, hires and superhires
 *     (DTAG_DIMS's MaxDepth: on AGA 8 each; OCS/ECS 6 or 5, 4 and 2);
 *   - SysInfo's screen and its neighbours, each "opened, mode ID, depth N"
 *     or "FAILED, error N" (SA_ErrorCode);
 *   - "lores|hires|superhires [laced]:" then, for depths 1 to 8, the depth
 *     and "ok" or "eN": every screen opened behind and closed at once.
 * A ROM with AGA's wider fetch opens all of them on AGA.
 *
 *   ScreenDepthProbe MODES=8020 DEPTH=3 [WIDTH=640] [HEIGHT=256] [SECS=10]
 * opens that screen in front, draws one bar for each of its colours (pens
 * 0 to 2^depth - 1, at most 256), a frame round the screen and a line of
 * text naming it, and keeps it SECS seconds for a screenshot. MODES is the
 * old ViewModes word in hex, as SysInfo gives it. It prints "shown" or the
 * failure. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static UWORD pens[] = { 0, 2, 2, 1, 2, 1, 2, 0, 1, (UWORD)~0 };
static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, 1 };

static void dims(ULONG id)
{
    struct DimensionInfo di;
    ULONG na = ModeNotAvailable(id);
    if (GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof di, DTAG_DIMS, id))
        Printf("  mode %08lx: notavailable %lx, maxdepth %ld\n", id, na, (LONG)di.MaxDepth);
    else
        Printf("  mode %08lx: notavailable %lx, no DimensionInfo\n", id, na);
}

static struct Screen *open_one(UWORD vm, UWORD depth, WORD w, WORD h, BOOL behind, ULONG *err)
{
    struct TagItem tags[] = { { SA_Pens, (ULONG)pens }, { SA_ErrorCode, (ULONG)err }, { TAG_DONE, 0 } };
    struct ExtNewScreen ns = { 0, 0, w, h, depth, 0, 1, vm,
        CUSTOMSCREEN | SCREENQUIET | NS_EXTENDED | (behind ? SCREENBEHIND : 0), &topaz8, NULL, NULL, NULL, tags };
    *err = 0;
    return OpenScreen((struct NewScreen *)&ns);
}

static void try_open(const char *what, UWORD vm, UWORD depth)
{
    ULONG err;
    struct Screen *s = open_one(vm, depth, 640, 256, TRUE, &err);
    if (s) {
        Printf("%-34s opened, mode %08lx, depth %ld\n", what, GetVPModeID(&s->ViewPort), (LONG)s->BitMap.Depth);
        CloseScreen(s);
    } else
        Printf("%-34s FAILED, error %ld\n", what, err);
}

static void sweep(void)
{
    static const char *const names[3] = { "lores", "hires", "superhires" };
    static const UWORD vms[3] = { 0x0000, 0x8000, 0x8020 };
    int res, lace, depth;
    for (lace = 0; lace < 2; lace++)
        for (res = 0; res < 3; res++) {
            Printf("%s%s:", names[res], lace ? " laced" : "");
            for (depth = 1; depth <= 8; depth++) {
                ULONG err;
                struct Screen *s = open_one((UWORD)(vms[res] | (lace ? LACE : 0)), (UWORD)depth,
                                            (WORD)(320 << res), (WORD)(lace ? 512 : 256), TRUE, &err);
                if (s) {
                    Printf(" %ld ok", (LONG)depth);
                    CloseScreen(s);
                } else
                    Printf(" %ld e%ld", (LONG)depth, err);
            }
            Printf("\n");
        }
}

static void show(UWORD vm, UWORD depth, WORD w, WORD h, LONG secs)
{
    ULONG err;
    struct Screen *s = open_one(vm, depth, w, h, FALSE, &err);
    struct RastPort *rp;
    LONG n, i, bw;
    char line[80];
    if (!s) {
        Printf("ViewModes %04lx depth %ld %ldx%ld: FAILED, error %ld\n", (ULONG)vm, (LONG)depth, (LONG)w, (LONG)h, err);
        return;
    }
    rp = &s->RastPort;
    n = 1L << (depth > 8 ? 8 : depth);
    bw = s->Width / n;
    if (bw < 1) bw = 1;
    for (i = 0; i < n; i++) {
        SetAPen(rp, (UBYTE)i);
        RectFill(rp, i * bw, 20, i * bw + bw - 1 < s->Width - 1 ? i * bw + bw - 1 : s->Width - 1, s->Height - 21);
    }
    SetAPen(rp, 1);
    Move(rp, 0, 0);
    Draw(rp, s->Width - 1, 0);
    Draw(rp, s->Width - 1, s->Height - 1);
    Draw(rp, 0, s->Height - 1);
    Draw(rp, 0, 0);
    sprintf(line, "ViewModes %04lx (mode %08lx) depth %ld, %ld x %ld", (ULONG)vm, GetVPModeID(&s->ViewPort),
            (LONG)depth, (LONG)s->Width, (LONG)s->Height);
    SetAPen(rp, 1);
    SetBPen(rp, 0);
    Move(rp, 4, 12);
    Text(rp, (STRPTR)line, strlen(line));
    Printf("shown: %s\n", line);
    Delay(secs * 50);
    CloseScreen(s);
}

int main(void)
{
    static const char tmpl[] = "MODES/K,DEPTH/K/N,WIDTH/K/N,HEIGHT/K/N,SECS/K/N";
    LONG a[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)tmpl, a, NULL);
    if (!rd) {
        PrintFault(IoErr(), (STRPTR)"ScreenDepthProbe");
        return RETURN_FAIL;
    }
    if (a[0]) {
        ULONG vm = 0;
        const char *p = (const char *)a[0];
        for (; *p; p++)
            vm = vm << 4 | (ULONG)(*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
        show((UWORD)vm, (UWORD)(a[1] ? *(LONG *)a[1] : 1), (WORD)(a[2] ? *(LONG *)a[2] : 640),
             (WORD)(a[3] ? *(LONG *)a[3] : 256), a[4] ? *(LONG *)a[4] : 10);
        FreeArgs(rd);
        return 0;
    }
    FreeArgs(rd);
    Printf("intuition %ld.%ld, graphics %ld.%ld\n",
        (LONG)IntuitionBase->LibNode.lib_Version, (LONG)IntuitionBase->LibNode.lib_Revision,
        (LONG)GfxBase->LibNode.lib_Version, (LONG)GfxBase->LibNode.lib_Revision);
    dims(0x00000000); dims(0x00008000); dims(0x00008020); dims(0x00029020);
    try_open("SysInfo's: ViewModes 9020 depth 3", 0x9020, 3);
    try_open("ViewModes 8020 depth 3", 0x8020, 3);
    try_open("ViewModes 8020 depth 2", 0x8020, 2);
    try_open("ViewModes 8000 depth 3", 0x8000, 3);
    try_open("ViewModes 8000 depth 8", 0x8000, 8);
    sweep();
    return 0;
}
