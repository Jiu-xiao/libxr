#pragma once

/**
 * @brief Writer 的运行期参数归一化与打包辅助函数 / Runtime argument normalization and packing helpers for Writer
 */

/**
 * @brief 返回一个 char 数组参数在边界内的 C 字符串长度 / Return the bounded C-string length inside one char array argument
 * @tparam N 数组长度 / Array extent
 * @param text char 数组参数 / Char array argument
 * @return 返回边界内首个 NUL 的位置 / Returns the first in-range NUL position
 * @note 这条路径要求数组边界内必须存在结尾 NUL；否则内部 `ASSERT` 会触发 /
 *       This path requires a terminating NUL inside the array bound; otherwise
 *       the internal `ASSERT` fires
 */
template <size_t N>
constexpr size_t Writer::BoundedTextLength(const char (&text)[N]) noexcept
{
  static_assert(N > 0, "LibXR::Print::Writer::BoundedTextLength: char array must be non-empty");
  size_t size = 0;
  while (size < N && text[size] != '\0')
  {
    ++size;
  }
  ASSERT(size < N);
  // Safety: if no NUL was found (size == N) and ASSERT is stripped in release
  // builds, return N-1 instead of N to prevent the caller from constructing a
  // string_view that reads past the array boundary.
  return size < N ? size : N - 1;
}

/**
 * @brief 将一个字符串类运行期参数归一化为 `std::string_view` / Normalize one string-like runtime argument into `std::string_view`
 * @tparam T 运行期实参类型 / Runtime argument type
 * @param text 运行期实参值 / Runtime argument value
 * @return 返回归一化后的文本视图 / Returns the normalized text view
 * @note 空 C 字符串指针会被归一化为字面量 `"(null)"` /
 *       Null C-string pointers are normalized to the literal `"(null)"`
 */
template <typename T>
constexpr std::string_view Writer::ToStringView(const T& text)
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
    return std::string_view(text, BoundedTextLength(text));
  }
  else
  {
    return {};
  }
}

/**
 * @brief 将一个已匹配的 C++ 实参归一化为某个编译参数槽要求的运行期打包存储类型 / Normalize one matched C++ argument into the packed runtime storage kind required by one compiled argument slot
 * @tparam pack 目标打包存储类型 / Target packed storage kind
 * @tparam T 运行期实参类型 / Runtime argument type
 * @param value 运行期实参值 / Runtime argument value
 * @return 返回该参数槽需要的归一化打包值 / Returns the normalized packed value for this slot
 */
template <FormatPackKind pack, typename T>
constexpr auto Writer::PackValue(T&& value)
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
    static_assert(
        dependent_false_v<pack>,
        "LibXR::Print::Writer::PackValue: unsupported packed argument kind");
  }
}

/**
 * @brief 把一个已打包参数值复制进字节块，并推进写指针 / Copy one packed argument value into the byte blob and advance the cursor
 * @tparam T 已打包值类型 / Packed value type
 * @param out 当前写指针；会前进 `sizeof(T)` / Current write cursor; advanced by `sizeof(T)`
 * @param value 待写入的已打包值 / Packed value to store
 */
template <typename T>
void Writer::StoreArgument(uint8_t*& out, const T& value)
{
  std::memcpy(out, &value, sizeof(T));
  out += sizeof(T);
}

/**
 * @brief 返回一个编译期参数列表所需的参数打包字节数 / Return the packed-byte size required by one compiled argument list
 * @tparam ArgumentInfoList 编译期参数元信息列表 / Compile-time argument metadata list
 * @return 返回该参数列表在运行期打包后需要的总字节数 / Returns the packed runtime byte size for this argument list
 */
template <auto ArgumentInfoList>
consteval size_t Writer::PackedArgumentBytes()
{
  size_t bytes = 0;
  for (const auto& argument : ArgumentInfoList)
  {
    bytes += FormatArgumentBytes(argument.pack);
  }
  return bytes;
}

/**
 * @brief 按字段执行顺序把运行期参数打包成最终字节块 / Pack runtime arguments into the final byte blob in field execution order
 * @tparam ArgumentInfoList 编译期参数元信息列表 / Compile-time argument metadata list
 * @tparam ArgumentOrder 每个输出字段对应的源参数索引表 / Source-argument index per emitted field
 * @tparam Tuple 持有转发后运行期实参的 tuple 类型 / Tuple type holding forwarded runtime arguments
 * @param out 当前写指针；会随着写入推进 / Current write cursor; advanced as values are stored
 * @param tuple 转发后的运行期实参集合 / Forwarded runtime arguments
 */
template <auto ArgumentInfoList, auto ArgumentOrder, typename Tuple>
void Writer::StoreArgumentsOrdered(uint8_t*& out, Tuple& tuple)
{
  [&]<size_t... I>(std::index_sequence<I...>) {
    (StoreArgument(out, PackValue<ArgumentInfoList[I].pack>(
                            std::get<ArgumentOrder[I]>(tuple))),
     ...);
  }(std::make_index_sequence<ArgumentInfoList.size()>{});
}
