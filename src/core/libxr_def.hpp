#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_2PI
#define M_2PI (2.0 * M_PI)
#endif

#ifndef M_1G
/// \brief 标准重力加速度（m/s²）
/// \brief Standard gravitational acceleration (m/s²)
constexpr double M_1G = 9.80665;
#endif

#ifndef DEF2STR
#define XR_TO_STR(_arg) #_arg
#define DEF2STR(_arg) XR_TO_STR(_arg)
#endif

#ifndef UNUSED
/// \brief 用于抑制未使用变量的警告
/// \brief Macro to suppress unused variable warnings
#define UNUSED(_x) ((void)(_x))
#endif

#ifndef OFFSET_OF
/// \brief 计算结构体成员在结构体中的偏移量
/// \brief Computes the offset of a member within a struct
#define OFFSET_OF(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef MEMBER_SIZE_OF
/// \brief 获取结构体成员的大小
/// \brief Retrieves the size of a member within a struct
#define MEMBER_SIZE_OF(type, member) (sizeof(decltype(((type *)0)->member)))
#endif

#ifndef CONTAINER_OF
/// \brief 通过成员指针获取包含该成员的结构体指针
/// \brief Retrieve the pointer to the containing structure from a member pointer
#define CONTAINER_OF(ptr, type, member) \
  ((type *)((char *)(ptr) - OFFSET_OF(type, member)))  // NOLINT
#endif

/// \brief 缓存行大小
static constexpr size_t LIBXR_CACHE_LINE_SIZE = (sizeof(void *) == 8) ? 64 : 32;

/**
 * @enum ErrorCode
 * @brief 定义错误码枚举
 * @brief Defines an enumeration for error codes
 */
enum class ErrorCode : int8_t
{
  OK = 0,             ///< 操作成功 | Operation successful
  FAILED = -1,        ///< 操作失败 | Operation failed
  INIT_ERR = -2,      ///< 初始化错误 | Initialization error
  ARG_ERR = -3,       ///< 参数错误 | Argument error
  STATE_ERR = -4,     ///< 状态错误 | State error
  SIZE_ERR = -5,      ///< 尺寸错误 | Size error
  CHECK_ERR = -6,     ///< 校验错误 | Check error
  NOT_SUPPORT = -7,   ///< 不支持 | Not supported
  NOT_FOUND = -8,     ///< 未找到 | Not found
  NO_RESPONSE = -9,   ///< 无响应 | No response
  NO_MEM = -10,       ///< 内存不足 | Insufficient memory
  NO_BUFF = -11,      ///< 缓冲区不足 | Insufficient buffer
  TIMEOUT = -12,      ///< 超时 | Timeout
  EMPTY = -13,        ///< 为空 | Empty
  FULL = -14,         ///< 已满 | Full
  BUSY = -15,         ///< 忙碌 | Busy
  PTR_NULL = -16,     ///< 空指针 | Null pointer
  OUT_OF_RANGE = -17  ///< 超出范围 | Out of range
};

/**
 * @enum SizeLimitMode
 * @brief 定义尺寸限制模式
 * @brief Defines size limit modes
 */
enum class SizeLimitMode : uint8_t
{
  EQUAL = 0,  ///< 尺寸必须相等 | Size must be equal
  LESS = 1,   ///< 尺寸必须小于 | Size must be less
  MORE = 2,   ///< 尺寸必须大于 | Size must be more
  NONE = 3    ///< 无限制 | No restriction
};

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef LIBXR_DEBUG_BUILD
/**
 * @brief 断言宏，在调试模式下检查条件是否满足
 * @brief Assertion macro to check conditions in debug mode
 * @param arg 要检查的条件 | Condition to check
 */
#define ASSERT(arg)                                 \
  do                                                \
  {                                                 \
    if (!(arg))                                     \
    {                                               \
      libxr_fatal_error(__FILE__, __LINE__, false); \
    }                                               \
  } while (0)

/**
 * @brief 中断服务例程（ISR）环境下的断言宏
 * @brief Assertion macro for use in ISR (Interrupt Service Routine)
 * @param arg 要检查的条件 | Condition to check
 */
#define ASSERT_ISR(arg)                            \
  do                                               \
  {                                                \
    if (!(arg))                                    \
    {                                              \
      libxr_fatal_error(__FILE__, __LINE__, true); \
    }                                              \
  } while (0)
#else
#define ASSERT(arg) ((void)0)
#define ASSERT_ISR(arg) ((void)0)
#endif

/**
 * @brief 处理致命错误
 * @brief Handles fatal errors
 * @param file 出错的源文件 | Source file where the error occurred
 * @param line 出错的行号 | Line number where the error occurred
 * @param in_isr 是否发生在中断服务例程（ISR） | Whether it occurred in an ISR
 */
extern void libxr_fatal_error(const char *file, uint32_t line, bool in_isr);

namespace LibXR
{
using ErrorCode = ErrorCode;
using SizeLimitMode = SizeLimitMode;

/**
 * @brief 计算两个数的最大值
 * @brief Computes the maximum of two numbers
 * @tparam T1 第一个数的类型 | Type of the first number
 * @tparam T2 第二个数的类型 | Type of the second number
 * @param a 第一个数 | First number
 * @param b 第二个数 | Second number
 * @return 两数中的较大值 | The larger of the two numbers
 */
template <typename T1, typename T2>
constexpr auto max(T1 a, T2 b) -> typename std::common_type<T1, T2>::type
{
  return (a > b) ? a : b;
}

/**
 * @brief 计算两个数的最小值
 * @brief Computes the minimum of two numbers
 * @tparam T1 第一个数的类型 | Type of the first number
 * @tparam T2 第二个数的类型 | Type of the second number
 * @param a 第一个数 | First number
 * @param b 第二个数 | Second number
 * @return 两数中的较小值 | The smaller of the two numbers
 */
template <typename T1, typename T2>
constexpr auto min(T1 a, T2 b) -> typename std::common_type<T1, T2>::type
{
  return (a < b) ? a : b;
}
}  // namespace LibXR
