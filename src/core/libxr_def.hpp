#pragma once

#include <concepts>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifndef DEF2STR
#define XR_TO_STR(_arg) #_arg
#define DEF2STR(_arg) XR_TO_STR(_arg)
#endif

#ifndef UNUSED
/// \brief 用于抑制未使用变量的警告
/// \brief Macro to suppress unused variable warnings
#define UNUSED(_x) ((void)(_x))
#endif

namespace LibXR
{
/// \brief PI 常量 / PI constant
inline constexpr double PI = 3.14159265358979323846;

/// \brief 2PI 常量 / 2PI constant
inline constexpr double TWO_PI = 2.0 * PI;

/// \brief 标准重力加速度（m/s²） / Standard gravitational acceleration (m/s²)
inline constexpr double STANDARD_GRAVITY = 9.80665;

/// \brief 缓存行大小 / Cache line size
inline constexpr size_t CACHE_LINE_SIZE = (sizeof(void*) == 8) ? 64 : 32;

/// \brief 平台自然对齐大小 / Native platform alignment size
inline constexpr size_t ALIGN_SIZE = sizeof(void*);

/**
 * @brief 指向非静态数据成员的成员指针
 * @brief Pointer to a non-static data member
 */
template <typename OwnerType, typename MemberType>
concept MemberObjectPointer = std::is_member_object_pointer_v<MemberType OwnerType::*>;

/**
 * @brief 具有公共类型且可比较大小的类型对
 * @brief Type pair with a common type and ordering
 */
template <typename LeftType, typename RightType>
concept CommonOrdered = std::common_with<LeftType, RightType> &&
                        requires(const LeftType& left, const RightType& right)
{
  { left < right } -> std::convertible_to<bool>;
  { left > right } -> std::convertible_to<bool>;
};

/**
 * @brief 计算成员在宿主对象中的偏移量
 * @brief Computes the offset of a member within the owning object
 * @param member 指向成员的成员指针，如 `&Type::member` | Member pointer such as `&Type::member`
 * @return 成员偏移量 | Member offset
 */
template <typename OwnerType, typename MemberType>
requires MemberObjectPointer<OwnerType, MemberType>
[[nodiscard]] inline size_t OffsetOf(MemberType OwnerType::*member) noexcept
{
  return reinterpret_cast<size_t>(
      &(reinterpret_cast<const volatile OwnerType*>(0)->*member));
}

/**
 * @brief 通过成员指针恢复其所属对象指针
 * @brief Recover the owning object pointer from a member pointer
 * @param ptr 指向成员的指针 | Pointer to the member
 * @param member 指向成员的成员指针，如 `&Type::member` | Member pointer such as `&Type::member`
 * @return 所属对象指针 | Pointer to the owning object
 */
template <typename OwnerType, typename MemberType>
requires MemberObjectPointer<OwnerType, MemberType>
[[nodiscard]] inline OwnerType* ContainerOf(MemberType* ptr,
                                            MemberType OwnerType::*member) noexcept
{
  return reinterpret_cast<OwnerType*>(reinterpret_cast<std::byte*>(ptr) -
                                      OffsetOf(member));
}

template <typename OwnerType, typename MemberType>
requires MemberObjectPointer<OwnerType, MemberType>
[[nodiscard]] inline const OwnerType* ContainerOf(const MemberType* ptr,
                                                  MemberType OwnerType::*member) noexcept
{
  return reinterpret_cast<const OwnerType*>(reinterpret_cast<const std::byte*>(ptr) -
                                            OffsetOf(member));
}

/**
 * @enum ErrorCode
 * @brief 定义错误码枚举
 * @brief Defines an enumeration for error codes
 */
enum class ErrorCode : int8_t
{
  PENDING = 1,        ///< 等待中 | Pending
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
}  // namespace LibXR

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef ASSERT_FROM_CALLBACK
#undef ASSERT_FROM_CALLBACK
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
 * @brief 回调环境下使用的断言宏（可来自中断或线程）
 * @brief Assertion macro for callbacks (may run in ISR or thread context).
 *
 * @param arg    要检查的条件 | Condition to check
 * @param in_isr 当前是否在中断上下文 | Whether currently in ISR context
 */
#define ASSERT_FROM_CALLBACK(arg, in_isr)              \
  do                                                   \
  {                                                    \
    if (!(arg))                                        \
    {                                                  \
      libxr_fatal_error(__FILE__, __LINE__, (in_isr)); \
    }                                                  \
  } while (0)
#else
#define ASSERT(arg) (void(arg), (void)0)
#define ASSERT_FROM_CALLBACK(arg, in_isr) \
  do                                      \
  {                                       \
    (void)(arg);                          \
    (void)(in_isr);                       \
  } while (0)
#endif

/**
 * @brief 处理致命错误
 * @brief Handles fatal errors
 * @param file 出错的源文件 | Source file where the error occurred
 * @param line 出错的行号 | Line number where the error occurred
 * @param in_isr 是否发生在中断服务例程（ISR） | Whether it occurred in an ISR
 */
extern "C" void libxr_fatal_error(const char* file, uint32_t line, bool in_isr);

namespace LibXR
{
/**
 * @brief 计算两个数的最大值
 * @brief Computes the maximum of two numbers
 * @tparam LeftType 第一个数的类型 | Type of the first number
 * @tparam RightType 第二个数的类型 | Type of the second number
 * @param a 第一个数 | First number
 * @param b 第二个数 | Second number
 * @return 两数中的较大值 | The larger of the two numbers
 */
template <typename LeftType, typename RightType>
requires CommonOrdered<LeftType, RightType>
constexpr auto max(LeftType a, RightType b) -> std::common_type_t<LeftType, RightType>
{
  return (a > b) ? a : b;
}

/**
 * @brief 计算两个数的最小值
 * @brief Computes the minimum of two numbers
 * @tparam LeftType 第一个数的类型 | Type of the first number
 * @tparam RightType 第二个数的类型 | Type of the second number
 * @param a 第一个数 | First number
 * @param b 第二个数 | Second number
 * @return 两数中的较小值 | The smaller of the two numbers
 */
template <typename LeftType, typename RightType>
requires CommonOrdered<LeftType, RightType>
constexpr auto min(LeftType a, RightType b) -> std::common_type_t<LeftType, RightType>
{
  return (a < b) ? a : b;
}
}  // namespace LibXR
