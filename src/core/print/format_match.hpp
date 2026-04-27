#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "format.hpp"

namespace LibXR::Print::Detail::FormatArgument
{
/**
 * @brief Internal C++ argument-type utilities shared by compile-time matching
 *        and runtime packing.
 * @brief 编译期参数匹配与运行期参数打包共用的内部 C++ 参数类型工具。
 *
 * Everything in this namespace is implementation detail and should not leak into
 * the public LibXR::Print surface.
 * 此命名空间内的内容全部属于实现细节，不应泄漏到公开 LibXR::Print 接口中。
 */
template <typename T>
struct TypeTraits
{
  using Decayed = std::remove_cvref_t<T>;
  using Normalized =
      typename std::conditional_t<std::is_enum_v<Decayed>, std::underlying_type<Decayed>,
                                  std::type_identity<Decayed>>::type;

  static constexpr bool is_signed_integer =
      std::is_integral_v<Normalized> && !std::is_same_v<Normalized, bool> &&
      std::is_signed_v<Normalized>;
  static constexpr bool is_unsigned_integer =
      std::is_integral_v<Normalized> && !std::is_same_v<Normalized, bool> &&
      std::is_unsigned_v<Normalized>;
  static constexpr bool is_default_signed_integer =
      is_signed_integer && sizeof(Normalized) <= sizeof(int);
  static constexpr bool is_default_unsigned_integer =
      is_unsigned_integer && sizeof(Normalized) <= sizeof(unsigned int);
  static constexpr bool is_char_array =
      std::is_array_v<Decayed> &&
      std::is_same_v<std::remove_cv_t<std::remove_extent_t<Decayed>>, char>;
  static constexpr bool is_c_string =
      std::is_same_v<Decayed, const char*> || std::is_same_v<Decayed, char*> ||
      is_char_array;
  static constexpr bool is_string_like =
      is_c_string || std::is_same_v<Decayed, std::string_view> ||
      std::is_same_v<Decayed, std::string>;
  static constexpr bool is_pointer_like =
      (std::is_pointer_v<Decayed> &&
       !std::is_function_v<std::remove_pointer_t<Decayed>>) ||
      std::is_same_v<Decayed, std::nullptr_t>;
  static constexpr bool is_character_like = std::is_integral_v<Normalized>;
  static constexpr bool is_float = std::is_same_v<Decayed, float> ||
                                   std::is_same_v<Decayed, double>;
  static constexpr bool is_long_double = std::is_same_v<Decayed, long double>;

  /**
   * @brief Returns whether this C++ argument type satisfies one compile-time rule.
   * @brief 判断当前 C++ 实参类型是否满足某条编译期匹配规则。
   */
  [[nodiscard]] static consteval bool MatchesRule(FormatArgumentRule rule)
  {
    if (rule == FormatArgumentRule::None)
    {
      return true;
    }

    switch (rule)
    {
      case FormatArgumentRule::SignedAny:
        return is_default_signed_integer;
      case FormatArgumentRule::SignedChar:
        return std::is_same_v<Normalized, signed char>;
      case FormatArgumentRule::SignedShort:
        return std::is_same_v<Normalized, short>;
      case FormatArgumentRule::SignedLong:
        return std::is_same_v<Normalized, long>;
      case FormatArgumentRule::SignedLongLong:
        return std::is_same_v<Normalized, long long>;
      case FormatArgumentRule::SignedIntMax:
        return std::is_same_v<Normalized, intmax_t>;
      case FormatArgumentRule::SignedSize:
        return std::is_same_v<Normalized, std::make_signed_t<size_t>>;
      case FormatArgumentRule::SignedPtrDiff:
        return std::is_same_v<Normalized, ptrdiff_t>;
      case FormatArgumentRule::UnsignedAny:
        return is_default_unsigned_integer;
      case FormatArgumentRule::UnsignedChar:
        return std::is_same_v<Normalized, unsigned char>;
      case FormatArgumentRule::UnsignedShort:
        return std::is_same_v<Normalized, unsigned short>;
      case FormatArgumentRule::UnsignedLong:
        return std::is_same_v<Normalized, unsigned long>;
      case FormatArgumentRule::UnsignedLongLong:
        return std::is_same_v<Normalized, unsigned long long>;
      case FormatArgumentRule::UnsignedIntMax:
        return std::is_same_v<Normalized, uintmax_t>;
      case FormatArgumentRule::UnsignedSize:
        return std::is_same_v<Normalized, size_t>;
      case FormatArgumentRule::UnsignedPtrDiff:
        return std::is_same_v<Normalized, std::make_unsigned_t<ptrdiff_t>>;
      case FormatArgumentRule::Pointer:
        return is_pointer_like;
      case FormatArgumentRule::Character:
        return is_character_like;
      case FormatArgumentRule::String:
        return is_string_like;
      case FormatArgumentRule::Float:
        return is_float;
      case FormatArgumentRule::LongDouble:
        return is_long_double;
      default:
        return false;
    }
  }
};

/**
 * @brief Expands one argument list against one rule array.
 * @brief 将一组模板实参与一组规则数组逐项展开比对。
 */
template <typename... Args, size_t N, size_t... I>
[[nodiscard]] consteval bool MatchesImpl(
    const std::array<FormatArgumentInfo, N>& arguments, std::index_sequence<I...>)
{
  return (TypeTraits<Args>::MatchesRule(arguments[I].rule) && ...);
}

/**
 * @brief Returns whether the provided argument list exactly matches the compiled
 *        argument metadata array.
 * @brief 判断给定实参列表是否与编译得到的参数元信息数组完全匹配。
 */
template <typename... Args, size_t N>
[[nodiscard]] consteval bool Matches(const std::array<FormatArgumentInfo, N>& arguments)
{
  if constexpr (sizeof...(Args) != N)
  {
    return false;
  }
  else
  {
    return MatchesImpl<Args...>(arguments, std::make_index_sequence<N>{});
  }
}
}  // namespace LibXR::Print::Detail::FormatArgument
