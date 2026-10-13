#ifndef UTILITY_HOOKS_H
#define UTILITY_HOOKS_H
#include <exec/nodes.h>
struct Hook { struct MinNode h_MinNode; ULONG (*h_Entry)(void); ULONG (*h_SubEntry)(void); APTR h_Data; };
#endif
