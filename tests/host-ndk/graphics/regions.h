#ifndef GRAPHICS_REGIONS_H
#define GRAPHICS_REGIONS_H
#include <graphics/gfx.h>
struct RegionRectangle { struct RegionRectangle *Next, *Prev; struct Rectangle bounds; };
struct Region { struct Rectangle bounds; struct RegionRectangle *RegionRectangle; };
#endif
