/* host NDK (tests only): the exec calls OpenLayers makes; the test supplies them. */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/semaphores.h>
APTR AllocMem(ULONG size, ULONG flags);
void FreeMem(APTR p, ULONG size);
void InitSemaphore(struct SignalSemaphore *s);
void ObtainSemaphore(struct SignalSemaphore *s);
void ReleaseSemaphore(struct SignalSemaphore *s);
#endif
