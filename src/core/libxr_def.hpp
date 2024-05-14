#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX
#define MAX(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a > _b ? _a : _b;                                                         \
  })
#endif

#ifndef MIN
#define MIN(a, b)                                                              \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a < _b ? _a : _b;                                                         \
  })
#endif

#ifndef DEF2STR
#define _XR_TO_STR(_arg) #_arg
#define DEF2STR(_arg) _XR_TO_STR(_arg)
#endif

#ifndef UNUSED
#define UNUSED(_x) ((void)(_x))
#endif

#ifndef OFFSET_OF
#define OFFSET_OF(type, member) ((size_t) & ((type *)0)->member)
#endif

#ifndef MEMBER_SIZE_OF
#define MEMBER_SIZE_OF(type, member) (sizeof(typeof(((type *)0)->member)))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - ms_offset_of(type, member));                     \
  })
#endif

typedef enum {
  NO_ERR = 0,
  ERR_FAIL = -1,
  ERR_MEM = -2,
  ERR_ARG = -3,
  ERR_STATE = -4,
  ERR_SIZE = -5,
  ERR_NOT_FOUND = -6,
  ERR_NOT_SUPPORT = -7,
  ERR_TIMEOUT = -8,
  ERR_NO_REPONSE = -9,
  ERR_CHECK = -10,
  ERR_BUSY = -11,
  ERR_INIT = -12,
  ERR_EMPTY = -13,
  ERR_FULL = -14,
  ERR_NULL = -15,
} ErrorCode;

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef LIBXR_DEBUG_BUILD
#define ASSERT(arg)                                                            \
  if (!(arg)) {                                                                \
    LibXR::Assert::FatalError(__FILE__, __LINE__);                             \
  }
#else
#define ASSERT(arg) (void)0;
#endif
