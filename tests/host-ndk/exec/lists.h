#ifndef EXEC_LISTS_H
#define EXEC_LISTS_H
#include <exec/nodes.h>
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type; UBYTE l_pad; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
#endif
