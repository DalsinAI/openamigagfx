/* host NDK (tests only): exec types. MIT, Dalsin Limited. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef void *APTR;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef int16_t BOOL;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef UBYTE *PLANEPTR;
typedef LONG BPTR;
#define VOID void
#define TRUE 1
#define FALSE 0
#endif
