#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "format.hpp"
#include "format_match.hpp"
#include "libxr_def.hpp"

namespace LibXR::Print
{
/**
 * @brief Runtime backend that packs arguments and executes compiled format records.
 * @brief 运行期后端，负责打包参数并执行编译后的格式记录。
 */
class Writer
{
 public:
  /**
   * @brief Writes a compiled format to a sink.
   * @brief 将编译后的格式写入输出端。
   */
  template <typename Sink, typename Format, typename... Args>
  [[nodiscard]] static ErrorCode Write(Sink& sink, const Format&, Args&&... args)
  {
    using Built = std::remove_cvref_t<Format>;
    static constexpr bool sink_contract =
        requires(Sink& output, std::string_view text) {
          { output.Write(text) } -> std::convertible_to<ErrorCode>;
        };
    static_assert(sink_contract,
                  "LibXR::Print::Writer: sink must provide "
                  "Write(std::string_view) -> ErrorCode");
    static constexpr bool format_contract =
        requires {
          typename std::remove_cvref_t<decltype(Built::Codes())>::value_type;
          typename std::remove_cvref_t<decltype(Built::ArgumentList())>::value_type;
          { Built::Codes().data() } -> std::convertible_to<const uint8_t*>;
          { Built::Codes().size() } -> std::convertible_to<size_t>;
          { Built::ArgumentList().size() } -> std::convertible_to<size_t>;
          { Built::Profile() } -> std::convertible_to<FormatProfile>;
        } && requires {
          requires(std::same_as<
                   typename std::remove_cvref_t<decltype(Built::Codes())>::value_type,
                   uint8_t>);
          requires(std::same_as<typename std::remove_cvref_t<
                                    decltype(Built::ArgumentList())>::value_type,
                                FormatArgumentInfo>);
        };
    static_assert(format_contract,
                  "LibXR::Print::Writer: format must expose Codes() bytes and "
                  "ArgumentList() metadata plus Profile()");

    static_assert(Built::template Matches<Args...>(),
                  "LibXR::Print::Write: format arguments do not match");

    // The packed argument blob and the code stream use the same source order,
    // so runtime execution only needs to consume the next argument for each
    // value record; no per-record argument index or offset is stored in codes.
    // 参数打包字节块与码流保持完全相同的源串顺序，因此运行期每遇到一条值记录
    // 只需要消费下一个参数；codes 中不需要额外保存参数索引或偏移。
    return WriteTagged<Sink, Built::ArgumentList(), Built::Profile()>(
        sink, Built::Codes().data(), std::forward<Args>(args)...);
  }

 private:
  /**
   * @brief Emits the stack argument byte blob for one compiled argument list.
   * @brief 为某个已编译参数列表生成栈上参数字节块。
   */
  static constexpr uint8_t unspecified_precision = 0xFF;
  static constexpr size_t float_buffer_capacity = 512;
  /// Largest finite float32 value whose integer part still fits in uint32_t. / 整数部分仍可放入 uint32_t 的最大 float32 值上界
  static constexpr float f32_u32_overflow_limit = 4294967296.0f;
  /// Decimal scales used by the narrow float32 fixed-precision fast path. / 窄 float32 定点快路径使用的十进制缩放表
  inline static constexpr std::array<uint32_t, 10> f32_decimal_scales_u32{
      1U,
      10U,
      100U,
      1000U,
      10000U,
      100000U,
      1000000U,
      10000000U,
      100000000U,
      1000000000U,
  };

  /**
   * @brief Tests whether one decoded field-spec bit is set.
   * @brief 判断某个已解码字段修饰位是否被设置。
   */
  [[nodiscard]] static constexpr bool HasFlag(uint8_t flags, uint8_t bit)
  {
    return (flags & bit) != 0;
  }

  /**
   * @brief Runtime view of one decoded field specification byte group.
   * @brief 运行期对单个字段描述字节组的解码视图。
   */
  struct Spec
  {
    uint8_t flags = 0;  ///< FormatFlag bitset / 字段修饰位集合
    uint8_t width = 0;  ///< field width, or zero when absent / 字段宽度，未指定时为 0
    uint8_t precision =
        unspecified_precision;  ///< precision, or unspecified_precision / 字段精度，未指定时为哨兵值

    [[nodiscard]] constexpr bool LeftAlign() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::LeftAlign));
    }

    [[nodiscard]] constexpr bool ForceSign() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::ForceSign));
    }

    [[nodiscard]] constexpr bool SpaceSign() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::SpaceSign));
    }

    [[nodiscard]] constexpr bool Alternate() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::Alternate));
    }

    [[nodiscard]] constexpr bool ZeroPad() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::ZeroPad));
    }

    [[nodiscard]] constexpr bool UpperCase() const
    {
      return Writer::HasFlag(flags, static_cast<uint8_t>(FormatFlag::UpperCase));
    }

    [[nodiscard]] constexpr bool HasPrecision() const
    {
      return precision != unspecified_precision;
    }
  };

  template <typename T>
  [[nodiscard]] static constexpr std::string_view ToStringView(const T& text)
  {
    using Traits = Detail::FormatArgument::TypeTraits<T>;

    if constexpr (std::is_same_v<typename Traits::Decayed, std::string_view>)
    {
      return text;
    }
    else if constexpr (std::is_same_v<typename Traits::Decayed, std::string>)
    {
      return std::string_view(text.data(), text.size());
    }
    else if constexpr (std::is_same_v<typename Traits::Decayed, const char*> ||
                       std::is_same_v<typename Traits::Decayed, char*>)
    {
      if (text == nullptr)
      {
        return "(null)";
      }
      return std::string_view(text, std::strlen(text));
    }
    else if constexpr (Traits::is_char_array)
    {
      return std::string_view(text, std::strlen(text));
    }
    else
    {
      return {};
    }
  }

  /**
   * @brief Normalizes one matched C++ argument into the packed runtime storage
   *        kind required by one compiled argument slot.
   * @brief 将一个已匹配的 C++ 实参归一化为某个编译参数槽要求的运行期打包存储类型。
   */
  template <FormatPackKind pack, typename T>
  [[nodiscard]] static constexpr auto PackValue(T&& value)
  {
    using Traits = Detail::FormatArgument::TypeTraits<T>;
    using Normalized = typename Traits::Normalized;

    if constexpr (pack == FormatPackKind::I32)
    {
      if constexpr (Traits::is_signed_integer)
      {
        return static_cast<int32_t>(static_cast<Normalized>(value));
      }
    }
    else if constexpr (pack == FormatPackKind::I64)
    {
      if constexpr (Traits::is_signed_integer)
      {
        return static_cast<int64_t>(static_cast<Normalized>(value));
      }
    }
    else if constexpr (pack == FormatPackKind::U32)
    {
      if constexpr (std::is_same_v<Normalized, bool>)
      {
        return static_cast<uint32_t>(value ? 1U : 0U);
      }
      else if constexpr (std::is_integral_v<Normalized>)
      {
        return static_cast<uint32_t>(static_cast<std::make_unsigned_t<Normalized>>(
            static_cast<Normalized>(value)));
      }
    }
    else if constexpr (pack == FormatPackKind::U64)
    {
      if constexpr (std::is_same_v<Normalized, bool>)
      {
        return static_cast<uint64_t>(value ? 1U : 0U);
      }
      else if constexpr (std::is_integral_v<Normalized>)
      {
        return static_cast<uint64_t>(static_cast<std::make_unsigned_t<Normalized>>(
            static_cast<Normalized>(value)));
      }
    }
    else if constexpr (pack == FormatPackKind::Pointer)
    {
      if constexpr (Traits::is_pointer_like)
      {
        if constexpr (std::is_same_v<typename Traits::Decayed, std::nullptr_t>)
        {
          return static_cast<uintptr_t>(0);
        }
        else
        {
          return (value == nullptr) ? static_cast<uintptr_t>(0)
                                    : reinterpret_cast<uintptr_t>(value);
        }
      }
    }
    else if constexpr (pack == FormatPackKind::Character)
    {
      if constexpr (Traits::is_character_like)
      {
        return static_cast<char>(static_cast<Normalized>(value));
      }
    }
    else if constexpr (pack == FormatPackKind::StringView)
    {
      if constexpr (Traits::is_string_like)
      {
        return ToStringView(value);
      }
    }
    else if constexpr (pack == FormatPackKind::F32)
    {
      if constexpr (std::is_arithmetic_v<Normalized>)
      {
        return static_cast<float>(value);
      }
    }
    else if constexpr (pack == FormatPackKind::F64)
    {
      if constexpr (std::is_arithmetic_v<Normalized>)
      {
        return static_cast<double>(value);
      }
    }
    else if constexpr (pack == FormatPackKind::LongDouble)
    {
      if constexpr (std::is_arithmetic_v<Normalized>)
      {
        return static_cast<long double>(value);
      }
    }
    else
    {
      static_assert(pack != pack,
                    "LibXR::Print::Writer::PackValue: unsupported packed argument kind");
    }
  }

  template <typename T>
  static void StoreArgument(uint8_t*& out, const T& value)
  {
    std::memcpy(out, &value, sizeof(T));
    out += sizeof(T);
  }

  template <auto ArgumentInfoList>
  [[nodiscard]] static consteval size_t PackedArgumentBytes()
  {
    size_t bytes = 0;
    for (const auto& argument : ArgumentInfoList)
    {
      bytes += FormatArgumentBytes(argument.pack);
    }
    return bytes;
  }

  template <auto ArgumentInfoList, typename Tuple>
  static void StoreArguments(uint8_t*& out, Tuple& tuple)
  {
    [&]<size_t... I>(std::index_sequence<I...>) {
      (StoreArgument(out, PackValue<ArgumentInfoList[I].pack>(std::get<I>(tuple))), ...);
    }(std::make_index_sequence<ArgumentInfoList.size()>{});
  }

  template <std::unsigned_integral UInt>
  [[nodiscard]] static size_t AppendUnsigned(char* out, UInt value, uint8_t base,
                                             bool upper_case)
  {
    constexpr char lower_digits[] = "0123456789abcdef";
    constexpr char upper_digits[] = "0123456789ABCDEF";
    const char* digits = upper_case ? upper_digits : lower_digits;
    char reverse[32];
    size_t count = 0;

    if (value == 0)
    {
      out[0] = '0';
      return 1;
    }

    while (value != 0)
    {
      reverse[count++] = digits[value % base];
      value /= base;
    }

    for (size_t i = 0; i < count; ++i)
    {
      out[i] = reverse[count - i - 1];
    }

    return count;
  }

  [[nodiscard]] static size_t AppendSmallUnsigned(char* out, uint8_t value)
  {
    return AppendUnsigned(out, value, 10, false);
  }

  /// Returns the padding width needed to expand one payload to the requested field width. / 返回把某段载荷扩展到目标字段宽度所需的填充长度
  [[nodiscard]] static constexpr size_t FieldPadding(uint8_t width, size_t payload_size)
  {
    return (width > payload_size) ? static_cast<size_t>(width) - payload_size : 0;
  }

  /// Returns the extra leading-zero count introduced by integer precision. / 返回整数精度引入的额外前导零个数
  [[nodiscard]] static constexpr size_t IntegerPrecisionZeros(
      const Spec& spec, size_t digit_count)
  {
    return (spec.HasPrecision() && spec.precision > digit_count)
               ? static_cast<size_t>(spec.precision) - digit_count
               : 0;
  }

  /// Returns the integer radix selected by one unsigned runtime type. / 返回某个无符号运行期类型对应的整数进制
  [[nodiscard]] static constexpr uint8_t IntegerBase(FormatType type)
  {
    switch (type)
    {
      case FormatType::Unsigned32:
      case FormatType::Unsigned64:
        return 10;
      case FormatType::Octal32:
      case FormatType::Octal64:
        return 8;
      case FormatType::HexLower32:
      case FormatType::HexLower64:
      case FormatType::HexUpper32:
      case FormatType::HexUpper64:
        return 16;
      default:
        return 0;
    }
  }

  /// Returns whether one unsigned runtime type should emit uppercase hex digits. / 判断某个无符号运行期类型是否应输出大写十六进制数字
  [[nodiscard]] static constexpr bool IntegerUpperCase(FormatType type)
  {
    return type == FormatType::HexUpper32 || type == FormatType::HexUpper64;
  }

  /// Returns the alternate-form prefix carried outside the digit payload. / 返回放在数字载荷之外的备用格式前缀
  template <std::unsigned_integral UInt>
  [[nodiscard]] static constexpr std::string_view IntegerPrefix(
      FormatType type, const Spec& spec, UInt value)
  {
    if (!spec.Alternate() || value == 0)
    {
      return {};
    }
    if (type == FormatType::HexLower32 || type == FormatType::HexLower64)
    {
      return "0x";
    }
    if (type == FormatType::HexUpper32 || type == FormatType::HexUpper64)
    {
      return "0X";
    }
    return {};
  }

  /**
   * @brief Applies %#o special rules directly onto the generated digit payload.
   * @brief 直接在已生成的数字载荷上应用 %#o 的特殊规则。
   *
   * Octal alternate form differs from hex: it is represented by a leading zero
   * in the digit payload itself, not by a detached prefix string. This helper
   * also preserves the required single 0 when %#.0o formats zero.
   * 八进制备用格式与十六进制不同：它通过数字载荷本体前导一个 0 来表示，
   * 而不是额外分离出的前缀字符串。本函数也负责在 %#.0o 格式化零值时保留
   * 必需的单个 0。
   */
  template <std::unsigned_integral UInt>
  [[nodiscard]] static size_t ApplyAlternateOctal(char* digits, size_t digit_count,
                                                  const Spec& spec, UInt value)
  {
    if (!spec.Alternate())
    {
      return (value == 0 && spec.precision == 0) ? 0 : digit_count;
    }

    if (value == 0 && spec.precision == 0)
    {
      return 1;
    }

    if (value == 0)
    {
      return digit_count;
    }

    if (spec.HasPrecision() && spec.precision > digit_count)
    {
      return digit_count;
    }

    digits[digit_count++] = '0';
    for (size_t i = digit_count - 1; i > 0; --i)
    {
      digits[i] = digits[i - 1];
    }
    digits[0] = '0';
    return digit_count;
  }

  /// Returns whether one runtime type is handled by the shared float text backend. / 判断某个运行期类型是否由共享浮点文本后端处理
  [[nodiscard]] static constexpr bool UsesFloatTextBackend(FormatType type)
  {
    switch (type)
    {
      case FormatType::FloatScientific:
      case FormatType::DoubleScientific:
      case FormatType::LongDoubleScientific:
      case FormatType::FloatGeneral:
      case FormatType::DoubleGeneral:
      case FormatType::LongDoubleGeneral:
      case FormatType::FloatFixed:
      case FormatType::DoubleFixed:
      case FormatType::LongDoubleFixed:
        return true;
      default:
        return false;
    }
  }

  /// Returns whether one runtime float type is currently enabled by feature switches. / 判断某个运行期浮点类型是否被当前功能开关启用
  [[nodiscard]] static constexpr bool FloatEnabled(FormatType type)
  {
    switch (type)
    {
      case FormatType::FloatFixed:
        return Config::enable_float_fixed;
      case FormatType::FloatScientific:
        return Config::enable_float_scientific;
      case FormatType::FloatGeneral:
        return Config::enable_float_general;
      case FormatType::DoubleFixed:
        return Config::enable_float_double && Config::enable_float_fixed;
      case FormatType::DoubleScientific:
        return Config::enable_float_double && Config::enable_float_scientific;
      case FormatType::DoubleGeneral:
        return Config::enable_float_double && Config::enable_float_general;
      case FormatType::LongDoubleFixed:
        return Config::enable_float_long_double && Config::enable_float_fixed;
      case FormatType::LongDoubleScientific:
        return Config::enable_float_long_double && Config::enable_float_scientific;
      case FormatType::LongDoubleGeneral:
        return Config::enable_float_long_double && Config::enable_float_general;
      default:
        return false;
    }
  }

  /// Appends one character to a bounded local formatting buffer. / 向有界本地格式化缓冲区追加一个字符
  [[nodiscard]] static bool AppendBufferChar(char* buffer, size_t capacity, size_t& size,
                                             char ch)
  {
    if (size >= capacity)
    {
      return false;
    }
    buffer[size++] = ch;
    return true;
  }

  /// Appends one text span to a bounded local formatting buffer. / 向有界本地格式化缓冲区追加一段文本
  [[nodiscard]] static bool AppendBufferText(char* buffer, size_t capacity, size_t& size,
                                             std::string_view text)
  {
    if (text.size() > capacity - size)
    {
      return false;
    }
    std::memcpy(buffer + size, text.data(), text.size());
    size += text.size();
    return true;
  }

  /// Appends one uint32_t decimal value and pads leading zeros up to width. / 追加一个 uint32_t 十进制值，并在前面补零到目标宽度
  [[nodiscard]] static bool AppendBufferU32ZeroPad(char* buffer, size_t capacity,
                                                   size_t& size, uint32_t value,
                                                   uint8_t width)
  {
    char digits[10];
    size_t digit_count = AppendUnsigned(digits, value, 10, false);
    size_t zero_count = (width > digit_count) ? static_cast<size_t>(width) - digit_count : 0;

    for (size_t i = 0; i < zero_count; ++i)
    {
      if (!AppendBufferChar(buffer, capacity, size, '0'))
      {
        return false;
      }
    }

    return AppendBufferText(buffer, capacity, size, std::string_view(digits, digit_count));
  }

  /// Returns round(value * scale) using the exact float32 bit pattern and nearest-even ties. / 基于精确 float32 位模式并按最近偶数处理平局返回 round(value * scale)
  [[nodiscard]] static uint64_t RoundScaledF32(float value, uint32_t scale)
  {
    uint32_t bits = std::bit_cast<uint32_t>(value);
    uint32_t exponent_bits = (bits >> 23) & 0xFFU;
    uint32_t fraction_bits = bits & 0x7FFFFFU;
    uint32_t significand =
        (exponent_bits == 0) ? fraction_bits : ((1U << 23) | fraction_bits);
    int exponent2 =
        (exponent_bits == 0) ? -149 : static_cast<int>(exponent_bits) - 150;
    uint64_t numerator = static_cast<uint64_t>(significand) * scale;

    if (exponent2 >= 0)
    {
      return numerator << exponent2;
    }

    unsigned int shift = static_cast<unsigned int>(-exponent2);
    if (shift >= 64)
    {
      return 0;
    }
    uint64_t quotient = numerator >> shift;
    uint64_t remainder = numerator & ((uint64_t{1} << shift) - 1U);
    uint64_t halfway = uint64_t{1} << (shift - 1);
    if (remainder > halfway || (remainder == halfway && (quotient & 1U) != 0U))
    {
      ++quotient;
    }
    return quotient;
  }

  template <typename Float>
  struct DecimalScale
  {
    int exponent = 0;  ///< decimal exponent / 十进制指数
    Float scale = 1;   ///< 10 ^ exponent / 10 的 exponent 次幂
  };

  template <typename Float>
  [[nodiscard]] static Float Power10(int exponent)
  {
    Float result = 1;
    Float base = 10;
    unsigned int remaining =
        static_cast<unsigned int>(exponent < 0 ? -exponent : exponent);

    while (remaining != 0)
    {
      if ((remaining & 1U) != 0U)
      {
        if (exponent < 0)
        {
          result /= base;
        }
        else
        {
          result *= base;
        }
      }

      remaining >>= 1U;
      if (remaining != 0U)
      {
        base *= base;
      }
    }

    return result;
  }

  /// Normalizes one finite positive value so that value divided by scale stays in [1, 10). / 将一个有限正值规范化，使 value 除以 scale 后落在 [1, 10)
  template <typename Float>
  [[nodiscard]] static DecimalScale<Float> NormalizeDecimal(Float value)
  {
    DecimalScale<Float> normalized{};
    if (value == 0)
    {
      return normalized;
    }

    int binary_exponent = 0;
    std::frexp(value, &binary_exponent);
    constexpr Float log10_of_2 =
        static_cast<Float>(0.30102999566398119521373889472449L);
    normalized.exponent =
        static_cast<int>(static_cast<Float>(binary_exponent - 1) * log10_of_2);
    normalized.scale = Power10<Float>(normalized.exponent);

    Float scaled = value / normalized.scale;
    while (scaled < 1)
    {
      normalized.scale /= 10;
      --normalized.exponent;
      scaled *= 10;
    }
    while (scaled >= 10)
    {
      normalized.scale *= 10;
      ++normalized.exponent;
      scaled /= 10;
    }

    return normalized;
  }

  /// Extracts one base-10 digit at the current scale while tolerating tiny FP residue. / 在当前十进制权重下提取一位数字，并容忍微小浮点残差
  template <typename Float>
  [[nodiscard]] static uint8_t ExtractDigit(Float& value, Float scale)
  {
    Float scaled = value / scale;
    auto digit = static_cast<int>(scaled + static_cast<Float>(1e-12L));
    if (digit < 0)
    {
      digit = 0;
    }
    else if (digit > 9)
    {
      digit = 9;
    }

    value -= static_cast<Float>(digit) * scale;
    Float epsilon = scale * static_cast<Float>(1e-9L);
    if (value < 0 && value > -epsilon)
    {
      value = 0;
    }

    return static_cast<uint8_t>(digit);
  }

  /// Trims trailing zeros for general float output and removes a dangling decimal point when alternate form is absent. / 在未启用备用格式时修剪通用浮点输出的尾随零，并去掉孤立小数点
  [[nodiscard]] static size_t TrimGeneralText(char* text, size_t size)
  {
    size_t exponent_pos = size;
    for (size_t i = 0; i < size; ++i)
    {
      if (text[i] == 'e' || text[i] == 'E')
      {
        exponent_pos = i;
        break;
      }
    }

    size_t mantissa_end = exponent_pos;
    while (mantissa_end > 0 && text[mantissa_end - 1] == '0')
    {
      --mantissa_end;
    }
    if (mantissa_end > 0 && text[mantissa_end - 1] == '.')
    {
      --mantissa_end;
    }

    if (exponent_pos == size)
    {
      return mantissa_end;
    }

    std::memmove(text + mantissa_end, text + exponent_pos, size - exponent_pos);
    return mantissa_end + (size - exponent_pos);
  }

  /// Fixed-only float32 formatter that uses a uint32_t scaled-fraction fast path when possible. / 仅供 float32 定点输出使用的格式化器，在可行时优先走 uint32_t 缩放小数快路径
  [[nodiscard]] static bool FormatF32FixedPrecText(float value, uint8_t precision,
                                                   char* out, size_t& out_size)
  {
    out_size = 0;

    // Stage 1: special IEEE spellings.
    // 第 1 阶段：处理 IEEE 特殊值文本。
    if (std::isnan(value))
    {
      return AppendBufferText(out, float_buffer_capacity, out_size, "nan");
    }
    if (std::isinf(value))
    {
      return AppendBufferText(out, float_buffer_capacity, out_size, "inf");
    }

    // Stage 2: narrow uint32_t-backed fixed formatter for the common case.
    // 第 2 阶段：常见场景优先走基于 uint32_t 的窄定点格式化器。
    if (precision < f32_decimal_scales_u32.size() && value < f32_u32_overflow_limit)
    {
      uint32_t integer_part = static_cast<uint32_t>(value);
      uint32_t scale = f32_decimal_scales_u32[precision];
      uint64_t scaled_total = RoundScaledF32(value, scale);
      uint64_t scaled_integer = static_cast<uint64_t>(integer_part) * scale;
      uint32_t fractional_part =
          (scaled_total >= scaled_integer)
              ? static_cast<uint32_t>(scaled_total - scaled_integer)
              : 0U;

      if (fractional_part >= scale)
      {
        fractional_part -= scale;
        if (integer_part == std::numeric_limits<uint32_t>::max())
        {
          if (!AppendBufferText(out, float_buffer_capacity, out_size, "4294967296"))
          {
            return false;
          }
          if (precision == 0)
          {
            return true;
          }
          if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
          {
            return false;
          }
          for (uint8_t i = 0; i < precision; ++i)
          {
            if (!AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
            {
              return false;
            }
          }
          return true;
        }
        ++integer_part;
      }

      if (!AppendBufferU32ZeroPad(out, float_buffer_capacity, out_size, integer_part, 1))
      {
        return false;
      }
      if (precision == 0)
      {
        return true;
      }
      if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
      {
        return false;
      }
      return AppendBufferU32ZeroPad(out, float_buffer_capacity, out_size, fractional_part,
                                    precision);
    }

    // Stage 3: generic fixed-only fallback for larger precision or magnitude.
    // 第 3 阶段：针对更大精度或更大数量级的通用仅定点回退路径。
    float rounded = value;
    float rounding = 0.5f;
    for (uint8_t i = 0; i < precision; ++i)
    {
      rounding *= 0.1f;
    }
    rounded += rounding;

    if (rounded < 1.0f)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
      {
        return false;
      }
    }
    else
    {
      float integer_scale = 1.0f;
      while (true)
      {
        float next_scale = integer_scale * 10.0f;
        if (!std::isfinite(next_scale) || rounded < next_scale)
        {
          break;
        }
        integer_scale = next_scale;
      }

      while (integer_scale >= 1.0f)
      {
        int digit = static_cast<int>(rounded / integer_scale);
        if (digit < 0)
        {
          digit = 0;
        }
        else if (digit > 9)
        {
          digit = 9;
        }

        if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                              static_cast<char>('0' + digit)))
        {
          return false;
        }

        rounded -= static_cast<float>(digit) * integer_scale;
        float epsilon = integer_scale * 1e-6f;
        if (rounded < 0.0f && rounded > -epsilon)
        {
          rounded = 0.0f;
        }
        integer_scale *= 0.1f;
      }
    }

    if (precision == 0)
    {
      return true;
    }
    if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
    {
      return false;
    }

    for (uint8_t i = 0; i < precision; ++i)
    {
      rounded *= 10.0f;
      int digit = static_cast<int>(rounded + 1e-6f);
      if (digit < 0)
      {
        digit = 0;
      }
      else if (digit > 9)
      {
        digit = 9;
      }

      if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                            static_cast<char>('0' + digit)))
      {
        return false;
      }

      rounded -= static_cast<float>(digit);
      if (rounded < 0.0f && rounded > -1e-5f)
      {
        rounded = 0.0f;
      }
    }

    return true;
  }

  template <typename Float>
  [[nodiscard]] static bool FormatFixedText(Float value, uint8_t precision, bool alternate,
                                            char* out, size_t& out_size)
  {
    Float rounded =
        value + static_cast<Float>(0.5L) * Power10<Float>(-static_cast<int>(precision));
    auto normalized = NormalizeDecimal(rounded);
    int integer_exponent = (rounded == 0) ? 0 : normalized.exponent;
    int start_pos = (integer_exponent > 0) ? integer_exponent : 0;
    Float scale = Power10<Float>(start_pos);

    out_size = 0;
    for (int pos = start_pos; pos >= 0; --pos)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                            static_cast<char>('0' + ExtractDigit(rounded, scale))))
      {
        return false;
      }
      scale /= 10;
    }

    if (precision != 0 || alternate)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
      {
        return false;
      }
    }

    for (uint8_t i = 0; i < precision; ++i)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                            static_cast<char>('0' + ExtractDigit(rounded, scale))))
      {
        return false;
      }
      scale /= 10;
    }

    return true;
  }

  [[nodiscard]] static bool AppendExponentText(char* out, size_t& out_size, int exponent,
                                               bool upper_case)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          upper_case ? 'E' : 'e'))
    {
      return false;
    }
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          exponent < 0 ? '-' : '+'))
    {
      return false;
    }

    char digits[16];
    unsigned int magnitude =
        static_cast<unsigned int>(exponent < 0 ? -exponent : exponent);
    size_t digit_count = AppendUnsigned(digits, magnitude, 10, false);
    if (digit_count < 2 &&
        !AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
    {
      return false;
    }

    return AppendBufferText(out, float_buffer_capacity, out_size,
                            std::string_view(digits, digit_count));
  }

  template <typename Float>
  [[nodiscard]] static bool FormatScientificText(Float value, uint8_t precision,
                                                 bool alternate, bool upper_case, char* out,
                                                 size_t& out_size)
  {
    auto initial = NormalizeDecimal(value);
    Float rounded = value;
    if (value != 0)
    {
      rounded += static_cast<Float>(0.5L) *
                 Power10<Float>(initial.exponent - static_cast<int>(precision));
    }

    auto normalized = NormalizeDecimal(rounded);
    int exponent = (rounded == 0) ? 0 : normalized.exponent;
    Float scale = Power10<Float>(exponent);

    out_size = 0;
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + ExtractDigit(rounded, scale))))
    {
      return false;
    }

    if (precision != 0 || alternate)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
      {
        return false;
      }
    }

    scale /= 10;
    for (uint8_t i = 0; i < precision; ++i)
    {
      if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                            static_cast<char>('0' + ExtractDigit(rounded, scale))))
      {
        return false;
      }
      scale /= 10;
    }

    return AppendExponentText(out, out_size, exponent, upper_case);
  }

  template <typename Float>
  [[nodiscard]] static bool FormatFloatText(FormatType type, const Spec& spec, Float value,
                                            char* out, size_t& out_size)
  {
    out_size = 0;

    if (std::isnan(value))
    {
      return AppendBufferText(out, float_buffer_capacity, out_size,
                              spec.UpperCase() ? "NAN" : "nan");
    }
    if (std::isinf(value))
    {
      return AppendBufferText(out, float_buffer_capacity, out_size,
                              spec.UpperCase() ? "INF" : "inf");
    }

    uint8_t precision = spec.HasPrecision() ? spec.precision : 6;
    switch (type)
    {
      case FormatType::FloatFixed:
      case FormatType::DoubleFixed:
      case FormatType::LongDoubleFixed:
        return FormatFixedText(value, precision, spec.Alternate(), out, out_size);
      case FormatType::FloatScientific:
      case FormatType::DoubleScientific:
      case FormatType::LongDoubleScientific:
        return FormatScientificText(value, precision, spec.Alternate(), spec.UpperCase(),
                                    out, out_size);
      case FormatType::FloatGeneral:
      case FormatType::DoubleGeneral:
      case FormatType::LongDoubleGeneral:
      {
        uint8_t significant = precision == 0 ? 1 : precision;
        int exponent = (value == 0) ? 0 : NormalizeDecimal(value).exponent;
        if (exponent < -4 || exponent >= significant)
        {
          if (!FormatScientificText(value, static_cast<uint8_t>(significant - 1),
                                    spec.Alternate(), spec.UpperCase(), out, out_size))
          {
            return false;
          }
        }
        else
        {
          int fractional_precision = static_cast<int>(significant) - (exponent + 1);
          if (fractional_precision < 0)
          {
            fractional_precision = 0;
          }
          if (!FormatFixedText(value, static_cast<uint8_t>(fractional_precision),
                               spec.Alternate(), out, out_size))
          {
            return false;
          }
        }

        if (!spec.Alternate())
        {
          out_size = TrimGeneralText(out, out_size);
        }
        return true;
      }
      default:
        return false;
    }
  }

  class CodeReader;
  class ArgumentReader;

  template <typename Sink, FormatProfile Profile>
  class Executor;

  template <typename Sink, FormatProfile Profile>
  [[nodiscard]] __attribute__((noinline)) static ErrorCode Execute(
      Sink& sink, const uint8_t* codes, const uint8_t* args)
  {
    return Executor<Sink, Profile>(sink, codes, args).Run();
  }

  template <typename Sink, auto ArgumentInfoList, FormatProfile Profile, typename... Args>
  [[nodiscard]] __attribute__((noinline)) static ErrorCode WriteTagged(
      Sink& sink, const uint8_t* codes, Args&&... args)
  {
    if constexpr (sizeof...(Args) == 0)
    {
      return Execute<Sink, Profile>(sink, codes, nullptr);
    }
    else
    {
      constexpr size_t packed_arg_bytes = PackedArgumentBytes<ArgumentInfoList>();
      uint8_t packed[packed_arg_bytes];
      auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
      auto* cursor = packed;
      StoreArguments<ArgumentInfoList>(cursor, tuple);
      return Execute<Sink, Profile>(sink, codes, packed);
    }
  }
};

/**
 * @brief Sequential reader for the compiled record stream.
 * @brief 编译后记录流的顺序读取器。
 */
class Writer::CodeReader
{
 public:
  /// Creates a reader over the beginning of one compiled byte blob. / 从单个编译字节块起点构造读取器
  explicit CodeReader(const uint8_t* codes) : pos_(codes), base_(codes)
  {
  }

  /// Reads the next runtime opcode. / 读取下一条运行期操作码
  [[nodiscard]] FormatOp ReadOp() { return static_cast<FormatOp>(*pos_++); }

  /// Reads a native-endian POD value emitted by the compile-time emitter. / 读取编译期发射器按本机字节序写入的 POD 值
  template <typename T>
  [[nodiscard]] T Read()
  {
    T value{};
    std::memcpy(&value, pos_, sizeof(T));
    pos_ += sizeof(T);
    return value;
  }

  /// Reads the semantic type byte carried by one GenericField payload. / 读取 GenericField 载荷中的语义类型字节
  [[nodiscard]] FormatType ReadFormatType() { return static_cast<FormatType>(*pos_++); }

  /**
   * @brief Reads the 3-byte field payload that follows one GenericField type byte.
   * @brief 读取紧跟在 GenericField 类型字节后的 3 字节字段载荷。
   *
   * The opcode and semantic type bytes are read separately. This call only
   * consumes flags, width, and precision, in that order.
   * 操作码与语义类型字节由外层单独读取；本函数只继续读取后续
   * flags、width、precision 这 3 个字节，顺序固定。
   */
  [[nodiscard]] Spec ReadSpec()
  {
    return Spec{.flags = *pos_++, .width = *pos_++, .precision = *pos_++};
  }

  /// Reads a null-terminated short text payload embedded in the record stream. / 读取内嵌在记录流中的短文本
  [[nodiscard]] std::string_view ReadInlineText()
  {
    auto text = reinterpret_cast<const char*>(pos_);
    size_t size = std::strlen(text);
    pos_ += size + 1;
    return std::string_view(text, size);
  }

  /// Reads an offset/size pair pointing into the trailing text pool. The offset is already rebased against the final code blob base. / 读取指向尾部文本池的偏移和长度；该偏移已经按最终代码块起点完成重定位
  [[nodiscard]] std::string_view ReadTextRef()
  {
    auto offset = Read<uint16_t>();
    auto size = Read<uint16_t>();
    auto text = reinterpret_cast<const char*>(base_ + offset);
    return std::string_view(text, size);
  }

 private:
  const uint8_t* pos_ = nullptr;
  const uint8_t* base_ = nullptr;
};

/**
 * @brief Sequential reader for the packed runtime argument byte blob.
 * @brief 运行期参数字节块的顺序读取器。
 */
class Writer::ArgumentReader
{
 public:
  /// Creates a reader over one packed runtime argument blob. / 从单个运行期参数打包字节块构造读取器
  explicit ArgumentReader(const uint8_t* data) : pos_(data) {}

  /// Reads one packed argument value without requiring alignment. / 以无对齐要求的方式读取一个已打包参数值
  template <typename T>
  [[nodiscard]] T Read()
  {
    T value{};
    std::memcpy(&value, pos_, sizeof(T));
    pos_ += sizeof(T);
    return value;
  }

 private:
  const uint8_t* pos_ = nullptr;
};

/**
 * @brief Per-sink bytecode executor specialized by the compiled format profile.
 * @brief 按编译格式 profile 特化的按输出端类型区分字节码执行器。
 */
template <typename Sink, FormatProfile Profile>
class Writer::Executor
{
 public:
  /// Binds one sink with one compiled byte blob and one packed argument blob. / 将一个输出端、一份编译字节块和一份参数字节块绑定起来
  Executor(Sink& sink, const uint8_t* codes, const uint8_t* args)
      : sink_(sink),
        codes_(codes),
        args_(args)
  {
  }

  /// Runs until the compiled record stream reaches FormatOp::End. / 持续执行记录流，直到遇到 FormatOp::End
  [[nodiscard]] ErrorCode Run()
  {
    while (true)
    {
      auto op = codes_.ReadOp();
      if (op == FormatOp::End)
      {
        return ErrorCode::OK;
      }

      auto ec = DispatchOp(op);
      if (ec != ErrorCode::OK)
      {
        return ec;
      }
    }
  }

 private:
  // Raw sink and generic field-writing helpers.
  // 原始输出与通用字段写出辅助函数。
  [[nodiscard]] ErrorCode WriteRaw(std::string_view text) { return sink_.Write(text); }
  [[nodiscard]] ErrorCode WritePadding(char fill, size_t count)
  {
    char chunk[16];
    std::memset(chunk, fill, sizeof(chunk));

    while (count != 0)
    {
      size_t step = (count < sizeof(chunk)) ? count : sizeof(chunk);
      auto ec = WriteRaw(std::string_view(chunk, step));
      if (ec != ErrorCode::OK)
      {
        return ec;
      }
      count -= step;
    }

    return ErrorCode::OK;
  }
  [[nodiscard]] ErrorCode WriteTextField(std::string_view text, const Spec& spec)
  {
    size_t pad = FieldPadding(spec.width, text.size());
    size_t left_pad = spec.LeftAlign() ? 0 : pad;
    size_t right_pad = spec.LeftAlign() ? pad : 0;

    if (auto ec = WritePadding(' ', left_pad); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = WriteRaw(text); ec != ErrorCode::OK)
    {
      return ec;
    }
    return WritePadding(' ', right_pad);
  }
  [[nodiscard]] ErrorCode WriteIntegerField(char sign_char, std::string_view prefix,
                                            std::string_view digits,
                                            const Spec& spec)
  {
    auto write_char = [this](char ch) -> ErrorCode {
      if (ch == '\0')
      {
        return ErrorCode::OK;
      }
      return WriteRaw(std::string_view(&ch, 1));
    };
    auto write_text = [this](std::string_view text) -> ErrorCode {
      if (text.empty())
      {
        return ErrorCode::OK;
      }
      return WriteRaw(text);
    };

    size_t zeros = IntegerPrecisionZeros(spec, digits.size());
    size_t total = digits.size() + zeros + prefix.size() +
                   static_cast<size_t>(sign_char != '\0');
    size_t pad = FieldPadding(spec.width, total);
    bool zero_fill = spec.ZeroPad() && !spec.LeftAlign() && !spec.HasPrecision();
    size_t left_pad = (!spec.LeftAlign() && !zero_fill) ? pad : 0;
    size_t middle_zeros = zero_fill ? pad : 0;
    size_t right_pad = spec.LeftAlign() ? pad : 0;

    if (auto ec = WritePadding(' ', left_pad); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = write_char(sign_char); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = write_text(prefix); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = WritePadding('0', middle_zeros); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = WritePadding('0', zeros); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = write_text(digits); ec != ErrorCode::OK)
    {
      return ec;
    }
    return WritePadding(' ', right_pad);
  }
  [[nodiscard]] ErrorCode WriteFloatField(char sign_char, std::string_view text,
                                          const Spec& spec)
  {
    auto write_char = [this](char ch) -> ErrorCode {
      if (ch == '\0')
      {
        return ErrorCode::OK;
      }
      return WriteRaw(std::string_view(&ch, 1));
    };

    size_t total = text.size() + static_cast<size_t>(sign_char != '\0');
    size_t pad = FieldPadding(spec.width, total);
    bool zero_fill = spec.ZeroPad() && !spec.LeftAlign();
    size_t left_pad = (!spec.LeftAlign() && !zero_fill) ? pad : 0;
    size_t middle_zeros = zero_fill ? pad : 0;
    size_t right_pad = spec.LeftAlign() ? pad : 0;

    if (auto ec = WritePadding(' ', left_pad); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = write_char(sign_char); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = WritePadding('0', middle_zeros); ec != ErrorCode::OK)
    {
      return ec;
    }
    if (auto ec = WriteRaw(text); ec != ErrorCode::OK)
    {
      return ec;
    }
    return WritePadding(' ', right_pad);
  }

  template <std::signed_integral Int>
  [[nodiscard]] static char ResolveSignChar(Int value, const Spec& spec)
  {
    if (value < 0)
    {
      return '-';
    }
    if (spec.ForceSign())
    {
      return '+';
    }
    if (spec.SpaceSign())
    {
      return ' ';
    }
    return '\0';
  }

  template <typename T>
  [[nodiscard]] static char ResolveFloatSignChar(T value, const Spec& spec)
  {
    if (std::signbit(value))
    {
      return '-';
    }
    if (spec.ForceSign())
    {
      return '+';
    }
    if (spec.SpaceSign())
    {
      return ' ';
    }
    return '\0';
  }

  template <std::signed_integral Int>
  [[nodiscard]] ErrorCode WriteSigned(const Spec& spec, Int value)
  {
    using UInt = std::make_unsigned_t<Int>;
    char digit_buffer[32];
    UInt bits = static_cast<UInt>(value);
    UInt magnitude = (value < 0) ? (UInt{0} - bits) : bits;
    size_t digit_count = AppendUnsigned(digit_buffer, magnitude, 10, false);

    std::string_view digits(digit_buffer, digit_count);
    if (value == 0 && spec.precision == 0)
    {
      digits = {};
    }

    return WriteIntegerField(ResolveSignChar(value, spec), {}, digits, spec);
  }

  template <std::unsigned_integral UInt>
  [[nodiscard]] ErrorCode WriteUnsigned(FormatType type, const Spec& spec, UInt value)
  {
    uint8_t base = IntegerBase(type);
    if (base == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    bool upper_case = IntegerUpperCase(type);
    auto prefix = IntegerPrefix(type, spec, value);

    char digit_buffer[33];
    size_t digit_count = AppendUnsigned(digit_buffer, value, base, upper_case);

    if (type == FormatType::Octal32 || type == FormatType::Octal64)
    {
      digit_count = ApplyAlternateOctal(digit_buffer, digit_count, spec, value);
    }
    else if (value == 0 && spec.precision == 0)
    {
      digit_count = 0;
    }

    std::string_view digits(digit_buffer, digit_count);
    return WriteIntegerField('\0', prefix, digits, spec);
  }
  [[nodiscard]] ErrorCode WritePointer(const Spec& spec, uintptr_t value)
  {
    char digit_buffer[2 * sizeof(uintptr_t)];
    size_t digit_count = AppendUnsigned(digit_buffer, value, 16, false);
    Spec actual = spec;

    if (!actual.HasPrecision() || actual.precision == 0)
    {
      actual.precision = 1;
    }

    return WriteIntegerField('\0', "0x", std::string_view(digit_buffer, digit_count),
                             actual);
  }
  [[nodiscard]] ErrorCode WriteCharacter(const Spec& spec, char ch)
  {
    return WriteTextField(std::string_view(&ch, 1), spec);
  }
  [[nodiscard]] ErrorCode WriteString(const Spec& spec, std::string_view text)
  {
    auto view = text;
    if (spec.HasPrecision() && spec.precision < view.size())
    {
      view = view.substr(0, spec.precision);
    }

    return WriteTextField(view, spec);
  }

  template <typename T>
  [[nodiscard]] ErrorCode WriteFloat(FormatType type, const Spec& spec, T value)
  {
    if (!UsesFloatTextBackend(type))
    {
      return ErrorCode::ARG_ERR;
    }

    char sign_char = ResolveFloatSignChar(value, spec);
    T magnitude = std::signbit(value) ? -static_cast<T>(value) : static_cast<T>(value);
    char output_buffer[float_buffer_capacity];
    size_t output_size = 0;
    if (!FormatFloatText(type, spec, magnitude, output_buffer, output_size))
    {
      return ErrorCode::NO_BUFF;
    }

    return WriteFloatField(sign_char, std::string_view(output_buffer, output_size), spec);
  }

  /// Fast path for one raw uint32_t decimal field. / 单个原始 uint32_t 十进制字段的快路径
  [[nodiscard]] ErrorCode WriteU32Dec(uint32_t value)
  {
    char digit_buffer[10];
    size_t digit_count = AppendUnsigned(digit_buffer, value, 10, false);
    return WriteRaw(std::string_view(digit_buffer, digit_count));
  }

  /// Fast path for one zero-padded uint32_t decimal field. / 单个零填充 uint32_t 十进制字段的快路径
  [[nodiscard]] ErrorCode WriteU32ZeroPadWidth(uint8_t width, uint32_t value)
  {
    char digit_buffer[10];
    size_t digit_count = AppendUnsigned(digit_buffer, value, 10, false);
    size_t zeros = FieldPadding(width, digit_count);
    if (auto ec = WritePadding('0', zeros); ec != ErrorCode::OK)
    {
      return ec;
    }
    return WriteRaw(std::string_view(digit_buffer, digit_count));
  }

  /// Fast path for one raw string argument. / 单个原始字符串参数的快路径
  [[nodiscard]] ErrorCode WriteStringRaw(std::string_view text)
  {
    return WriteRaw(text);
  }

  /// Fast path for one fixed float with explicit precision. / 单个带显式精度的定点 float 快路径
  [[nodiscard]] ErrorCode WriteF32FixedPrec(uint8_t precision, float value)
  {
    char sign_char = std::signbit(value) ? '-' : '\0';
    float magnitude = std::signbit(value) ? -value : value;
    char output_buffer[float_buffer_capacity];
    size_t output_size = 0;
    if (!FormatF32FixedPrecText(magnitude, precision, output_buffer, output_size))
    {
      return ErrorCode::NO_BUFF;
    }

    if (sign_char != '\0')
    {
      if (auto ec = WriteRaw(std::string_view(&sign_char, 1)); ec != ErrorCode::OK)
      {
        return ec;
      }
    }

    return WriteRaw(std::string_view(output_buffer, output_size));
  }

  /// Fast path for one fixed double with explicit precision. / 单个带显式精度的定点 double 快路径
  [[nodiscard]] ErrorCode WriteF64FixedPrec(uint8_t precision, double value)
  {
    return WriteFloat(FormatType::DoubleFixed, Spec{.precision = precision}, value);
  }

  // Small bridges that keep GenericField dispatch readable while preserving the
  // existing "read spec -> read next packed argument -> call concrete writer"
  // execution order.
  // 这些小桥接函数只负责让 GenericField 分发更易读，同时保持原有的
  // “读 spec -> 读下一个已打包参数 -> 调具体 writer” 执行顺序不变。
  template <std::signed_integral Int>
  [[nodiscard]] ErrorCode DispatchSignedField()
  {
    return WriteSigned(codes_.ReadSpec(), args_.Read<Int>());
  }

  template <FormatType Type, std::unsigned_integral UInt>
  [[nodiscard]] ErrorCode DispatchUnsignedField()
  {
    return WriteUnsigned(Type, codes_.ReadSpec(), args_.Read<UInt>());
  }

  template <FormatType Type, typename Float>
  [[nodiscard]] ErrorCode DispatchFloatField()
  {
    return WriteFloat(Type, codes_.ReadSpec(), args_.Read<Float>());
  }

  [[nodiscard]] ErrorCode DispatchPointerField()
  {
    return WritePointer(codes_.ReadSpec(), args_.Read<uintptr_t>());
  }

  [[nodiscard]] ErrorCode DispatchCharacterField()
  {
    return WriteCharacter(codes_.ReadSpec(), args_.Read<char>());
  }

  [[nodiscard]] ErrorCode DispatchStringField()
  {
    return WriteString(codes_.ReadSpec(), args_.Read<std::string_view>());
  }

  /// Dispatches one GenericField payload to the corresponding wide fallback. / 将一个 GenericField 载荷分发到对应的宽回退路径
  [[nodiscard]] ErrorCode DispatchGenericField(FormatType type)
  {
    switch (type)
    {
      case FormatType::Signed32:
        if constexpr (!Config::enable_integer)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchSignedField<int32_t>();
      case FormatType::Signed64:
        if constexpr (!Config::enable_integer || !Config::enable_integer_64bit)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchSignedField<int64_t>();
      case FormatType::Unsigned32:
        if constexpr (!Config::enable_integer)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::Unsigned32, uint32_t>();
      case FormatType::Unsigned64:
        if constexpr (!Config::enable_integer || !Config::enable_integer_64bit)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::Unsigned64, uint64_t>();
      case FormatType::Octal32:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::Octal32, uint32_t>();
      case FormatType::Octal64:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                      !Config::enable_integer_64bit)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::Octal64, uint64_t>();
      case FormatType::HexLower32:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::HexLower32, uint32_t>();
      case FormatType::HexLower64:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                      !Config::enable_integer_64bit)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::HexLower64, uint64_t>();
      case FormatType::HexUpper32:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::HexUpper32, uint32_t>();
      case FormatType::HexUpper64:
        if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                      !Config::enable_integer_64bit)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchUnsignedField<FormatType::HexUpper64, uint64_t>();
      case FormatType::Pointer:
        if constexpr (!Config::enable_pointer)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchPointerField();
      case FormatType::Character:
        if constexpr (!Config::enable_text)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchCharacterField();
      case FormatType::String:
        if constexpr (!Config::enable_text)
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchStringField();
      case FormatType::FloatFixed:
        if constexpr (!FloatEnabled(FormatType::FloatFixed))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::FloatFixed, float>();
      case FormatType::DoubleFixed:
        if constexpr (!FloatEnabled(FormatType::DoubleFixed))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::DoubleFixed, double>();
      case FormatType::FloatScientific:
        if constexpr (!FloatEnabled(FormatType::FloatScientific))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::FloatScientific, float>();
      case FormatType::DoubleScientific:
        if constexpr (!FloatEnabled(FormatType::DoubleScientific))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::DoubleScientific, double>();
      case FormatType::FloatGeneral:
        if constexpr (!FloatEnabled(FormatType::FloatGeneral))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::FloatGeneral, float>();
      case FormatType::DoubleGeneral:
        if constexpr (!FloatEnabled(FormatType::DoubleGeneral))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::DoubleGeneral, double>();
      case FormatType::LongDoubleFixed:
        if constexpr (!FloatEnabled(FormatType::LongDoubleFixed))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::LongDoubleFixed, long double>();
      case FormatType::LongDoubleScientific:
        if constexpr (!FloatEnabled(FormatType::LongDoubleScientific))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::LongDoubleScientific, long double>();
      case FormatType::LongDoubleGeneral:
        if constexpr (!FloatEnabled(FormatType::LongDoubleGeneral))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchFloatField<FormatType::LongDoubleGeneral, long double>();
      case FormatType::TextInline:
      case FormatType::TextRef:
      case FormatType::TextSpace:
      case FormatType::End:
      default:
        return ErrorCode::STATE_ERR;
    }
  }

  /// Dispatches one runtime opcode to the selected specialized path. / 将一个运行期操作码分发到选中的特化路径
  [[nodiscard]] ErrorCode DispatchOp(FormatOp op)
  {
    switch (op)
    {
      case FormatOp::TextInline:
        return WriteRaw(codes_.ReadInlineText());
      case FormatOp::TextRef:
        return WriteRaw(codes_.ReadTextRef());
      case FormatOp::TextSpace:
        return WriteRaw(" ");
      case FormatOp::U32Dec:
        if constexpr (!HasProfile(Profile, FormatProfile::U32) ||
                      !Config::enable_integer)
        {
          return ErrorCode::STATE_ERR;
        }
        return WriteU32Dec(args_.Read<uint32_t>());
      case FormatOp::U32ZeroPadWidth:
        if constexpr (!HasProfile(Profile, FormatProfile::U32) ||
                      !Config::enable_integer)
        {
          return ErrorCode::STATE_ERR;
        }
        return WriteU32ZeroPadWidth(codes_.Read<uint8_t>(), args_.Read<uint32_t>());
      case FormatOp::StringRaw:
        if constexpr (!HasProfile(Profile, FormatProfile::TextArg) ||
                      !Config::enable_text)
        {
          return ErrorCode::STATE_ERR;
        }
        return WriteStringRaw(args_.Read<std::string_view>());
      case FormatOp::F32FixedPrec:
        if constexpr (!HasProfile(Profile, FormatProfile::F32Fixed) ||
                      !FloatEnabled(FormatType::FloatFixed))
        {
          return ErrorCode::STATE_ERR;
        }
        return WriteF32FixedPrec(codes_.Read<uint8_t>(), args_.Read<float>());
      case FormatOp::F64FixedPrec:
        if constexpr (!HasProfile(Profile, FormatProfile::F64Fixed) ||
                      !FloatEnabled(FormatType::DoubleFixed))
        {
          return ErrorCode::STATE_ERR;
        }
        return WriteF64FixedPrec(codes_.Read<uint8_t>(), args_.Read<double>());
      case FormatOp::GenericField:
        if constexpr (!HasProfile(Profile, FormatProfile::Generic))
        {
          return ErrorCode::STATE_ERR;
        }
        return DispatchGenericField(codes_.ReadFormatType());
      case FormatOp::End:
      default:
        return ErrorCode::STATE_ERR;
    }
  }

  Sink& sink_;
  CodeReader codes_;
  ArgumentReader args_;
};
}  // namespace LibXR::Print
