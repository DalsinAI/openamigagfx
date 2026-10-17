/* SatProbe: a shared pen obtained more than 255 times, on OS 3.2.3's graphics.library or ours.
 *   SatProbe [N [ALL]]   (ALL: released N times, the control)
 * On the Workbench screen's map: ObtainBestPen of a colour no pen has (0x123456, PRECISION_EXACT)
 * N times (default 300), then ReleasePen N-1 times. Prints the pen, its RefCnt after the obtains and
 * after the releases (our PalExtra layout; OS 3.2.3's reads otherwise), then takes every free pen with new
 * colours and says whether that pen was among them, i.e. freed while still held. Then releases what is left.
 * Built with the os32-gcc16 stove:
 *   m68k-amigaos-gcc -noixemul -m68020 -O2 -o SatProbe tests/SatProbe.c
 * Measured on OS 3.2.3 (graphics 47.10, 24-bit ACRTG Workbench, W33): with 300 and 1000 obtains and one
 * fewer releases the pen stays held; with all released it is handed out again.
 * Team 5. MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    struct Screen *s = LockPubScreen(NULL);
    struct ColorMap *cm;
    struct PaletteExtra *pe;
    LONG n = argc > 1 ? atol(argv[1]) : 300, i, pen = -1;
    ULONG rgb[3];
    if (!s)
        return 20;
    cm = s->ViewPort.ColorMap;
    pe = cm->PalExtra;
    for (i = 0; i < n; i++) {
        LONG p = ObtainBestPen(cm, 0x12121212UL, 0x34343434UL, 0x56565656UL, OBP_Precision, PRECISION_EXACT, TAG_DONE);
        if (i == 0)
            pen = p;
        else if (p != pen)
            Printf("obtain %ld gave pen %ld, not %ld\n", i, p, pen);
    }
    Printf("pen %ld after %ld obtains: refcnt %ld\n", pen, n, pe && pe->pe_RefCnt && pen >= 0 ? (LONG)pe->pe_RefCnt[pen] : -1L);
    if (pen < 0) {
        UnlockPubScreen(NULL, s);
        return 5;
    }
    for (i = 0; i < n - 1; i++)
        ReleasePen(cm, pen);
    if (argc > 2) {     /* ALL: the last one too, the control: the pen must be free now */
        ReleasePen(cm, pen);
        Printf("released all %ld\n", n);
    }
    GetRGB32(cm, pen, 1, rgb);
    Printf("after %ld releases: refcnt %ld rgb %02lx%02lx%02lx\n", n - 1, pe && pe->pe_RefCnt ? (LONG)pe->pe_RefCnt[pen] : -1L,
           rgb[0] >> 24, rgb[1] >> 24, rgb[2] >> 24);
    /* is it still held? take every free pen with new colours (exact, fail if none): a freed pen is among them */
    {
        static LONG got[256];
        LONG k, m = 0, hit = 0;
        for (k = 0; k < 256; k++) {
            got[k] = ObtainBestPen(cm, (ULONG)k * 0x01010101UL, 0x77777777UL, 0x99999999UL, OBP_Precision, PRECISION_EXACT,
                                   OBP_FailIfBad, TRUE, TAG_DONE);
            if (got[k] == pen)
                hit = 1;
            if (got[k] >= 0)
                m++;
        }
        GetRGB32(cm, pen, 1, rgb);
        Printf("%ld new colours got pens; pen %ld %s, its colour now %02lx%02lx%02lx\n", m, pen,
               hit ? "was handed out again (free)" : "was not handed out (held)", rgb[0] >> 24, rgb[1] >> 24, rgb[2] >> 24);
        for (k = 0; k < 256; k++)
            if (got[k] >= 0)
                ReleasePen(cm, got[k]);
        if (hit || argc > 2) {
            UnlockPubScreen(NULL, s);
            return 0;
        }
    }
    ReleasePen(cm, pen);
    UnlockPubScreen(NULL, s);
    return 0;
}
