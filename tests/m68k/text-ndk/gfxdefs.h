/* host NDK overlay (tests only): the graphics constants and structures
 * library/core/gfx_rp.c uses that tests/host-ndk leaves out, forced in with
 * -include, so tests/m68k/test_gfx_text.sh can draw mono text through the
 * real core. Clean-room, from the published layouts. MIT, AmigaChrome Team. */
#ifndef TEXT_NDK_GFXDEFS_H
#define TEXT_NDK_GFXDEFS_H
#include <exec/types.h>
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
#define FRST_DOT 0x01
#define ONE_DOT 0x02
#define DBUFFER 0x04
#define AREAOUTLINE 0x08
#define NOCROSSFILL 0x20
struct AreaInfo {
    WORD *VctrTbl, *VctrPtr;
    BYTE *FlagTbl, *FlagPtr;
    WORD Count, MaxCount, FirstX, FirstY;
};
struct TmpRas { BYTE *RasPtr; LONG Size; };
#endif
