/* host shim (tools/hostinc): lets src/video.c and the kit headers compile on the host for tools/vhost.c */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef uint8_t UBYTE; typedef int8_t BYTE; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint32_t ULONG; typedef int32_t LONG; typedef void *APTR;
#endif
