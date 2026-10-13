/* host NDK overlay (tests only): utility/tagitem.h. MIT, Dalsin Limited. */
#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H
#include <exec/types.h>
struct TagItem { ULONG ti_Tag, ti_Data; };
#define TAG_DONE 0UL
#define TAG_END 0UL
#define TAG_IGNORE 1UL
#define TAG_MORE 2UL
#define TAG_SKIP 3UL
#define TAG_USER 0x80000000UL
#endif
