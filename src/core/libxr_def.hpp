#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_2PI
#define M_2PI (2.0 * M_PI)
#endif

#ifndef M_1G
constexpr double M_1G = 9.80665;
#endif

#ifndef DEF2STR
#define _XR_TO_STR(_arg) #_arg
#define DEF2STR(_arg) _XR_TO_STR(_arg)
#endif

#ifndef UNUSED
#define UNUSED(_x) ((void)(_x))
#endif

#ifndef typeof
#define typeof __typeof__
#endif

#ifndef OFFSET_OF
#define OFFSET_OF(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef MEMBER_SIZE_OF
#define MEMBER_SIZE_OF(type, member) (sizeof(decltype(((type *)0)->member)))
#endif

#define CONTAINER_OF(ptr, type, member)                                        \
  ((type *)((char *)(ptr) - OFFSET_OF(type, member)))

enum class ErrorCode {
  OK = 0,
  FAILED = -1,
  INIT_ERR = -2,
  ARG_ERR = -3,
  STATE_ERR = -4,
  SIZE_ERR = -5,
  CHECK_ERR = -6,
  NOT_SUPPORT = -7,
  NOT_FOUND = -8,
  NO_REPONSE = -9,
  NO_MEM = -10,
  NO_BUFF = -11,
  TIMEOUT = -12,
  EMPTY = -13,
  FULL = -14,
  BUSY = -15,
  PTR_NULL = -16,
  OUT_OF_RANGE = -17,
};

enum class SizeLimitMode { EQUAL = 0, LESS = 1, MORE = 2, NONE = 3 };

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef LIBXR_DEBUG_BUILD
#define ASSERT(arg)                                                            \
  do {                                                                         \
    if (!(arg)) {                                                              \
      LibXR::Assert::FatalError(__FILE__, __LINE__, false);                    \
    }                                                                          \
  } while (0)

#define ASSERT_ISR(arg)                                                        \
  do {                                                                         \
    if (!(arg)) {                                                              \
      LibXR::Assert::FatalError(__FILE__, __LINE__, true);                     \
    }                                                                          \
  } while (0)
#else
#define ASSERT(arg) ((void)0)
#define ASSERT_ISR(arg) ((void)0)
#endif

namespace LibXR {
template <typename T1, typename T2>
constexpr auto MAX(T1 a, T2 b) -> typename std::common_type<T1, T2>::type {
  return (a > b) ? a : b;
}

template <typename T1, typename T2>
constexpr auto MIN(T1 a, T2 b) -> typename std::common_type<T1, T2>::type {
  return (a < b) ? a : b;
}
} // namespace LibXR
