/* host NDK overlay (tests only): exec/ports.h's Message. MIT, Dalsin Limited. */
#ifndef EXEC_PORTS_H
#define EXEC_PORTS_H
#include <exec/nodes.h>
struct MsgPort;
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
#endif
