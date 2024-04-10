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
  NO_ERR,
  ERR_FAIL,
  ERR_MEM,
  ERR_ARG,
  ERR_STATE,
  ERR_SIZE,
  ERR_NOT_FOUND,
  ERR_NOT_SUPPORT,
  ERR_TIMEOUT,
  ERR_NO_REPONSE,
  ERR_CHECK,
  ERR_BUSY,
  ERR_INIT,
  ERR_EMPTY,
  ERR_FULL,
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
