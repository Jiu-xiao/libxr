#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR
{
template <size_t N>
struct FixedString
{
  char value[N]{};

  constexpr FixedString(const char (&text)[N])
  {
    for (size_t i = 0; i < N; ++i)
    {
      value[i] = text[i];
    }
  }

  [[nodiscard]] constexpr size_t Size() const { return N - 1; }

  [[nodiscard]] constexpr const char* Data() const { return value; }
};

template <size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

enum class FormatRecordType : uint8_t
{
  End,
  TextInline,
  TextRef,
  SignedDecimal,
  UnsignedDecimal,
  Octal,
  HexLower,
  HexUpper,
  Pointer,
  Character,
  CString,
  FloatFixed,
  FloatScientific,
  FloatGeneral,
};

enum FormatFlag : uint8_t
{
  LeftAlign = 1U << 0,
  ForceSign = 1U << 1,
  SpaceSign = 1U << 2,
  AlternateForm = 1U << 3,
  ZeroPad = 1U << 4,
  UpperCase = 1U << 5,
};

inline constexpr size_t format_inline_text_limit = 2 * sizeof(size_t) - 1;
using TextOffset = uint16_t;
using TextLength = uint16_t;

template <typename Sink>
concept FormatSink = requires(Sink& sink, std::string_view text)
{
  { sink.Write(text) } -> std::same_as<ErrorCode>;
};

template <FixedString Text>
class Format;

namespace Print
{
template <FormatSink Sink, FixedString Text, typename... Args>
ErrorCode Write(Sink& sink, const Format<Text>& format, Args&&... args);
}

namespace Detail
{
enum class FormatLength : uint8_t
{
  Default,
  Char,
  Short,
  Long,
  LongLong,
  IntMax,
  Size,
  PtrDiff,
  LongDouble,
};

template <typename T>
using DecayT = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T, bool = std::is_enum_v<DecayT<T>>>
struct ValueBaseType
{
  using type = DecayT<T>;
};

template <typename T>
struct ValueBaseType<T, true>
{
  using type = std::underlying_type_t<DecayT<T>>;
};

template <typename T>
using ValueBaseTypeT = typename ValueBaseType<T>::type;

template <typename T>
inline constexpr bool is_signed_value =
    std::is_integral_v<ValueBaseTypeT<T>> && !std::is_same_v<ValueBaseTypeT<T>, bool> &&
    std::is_signed_v<ValueBaseTypeT<T>>;

template <typename T>
inline constexpr bool is_unsigned_value =
    std::is_integral_v<ValueBaseTypeT<T>> && !std::is_same_v<ValueBaseTypeT<T>, bool> &&
    std::is_unsigned_v<ValueBaseTypeT<T>>;

template <typename T, typename Expected>
inline constexpr bool is_exact_base_type = std::is_same_v<ValueBaseTypeT<T>, Expected>;

template <typename T>
inline constexpr bool is_character_array =
    std::is_array_v<DecayT<T>> &&
    std::is_same_v<std::remove_extent_t<DecayT<T>>, char>;

template <typename T>
inline constexpr bool is_cstring_like = std::is_same_v<DecayT<T>, const char*> ||
                                        std::is_same_v<DecayT<T>, char*> ||
                                        is_character_array<T>;

template <typename T>
inline constexpr bool is_string_like = is_cstring_like<T> ||
                                       std::is_same_v<DecayT<T>, std::string_view> ||
                                       std::is_same_v<DecayT<T>, std::string>;

template <typename T>
inline constexpr bool is_pointer_value =
    std::is_pointer_v<DecayT<T>> || std::is_same_v<DecayT<T>, std::nullptr_t>;

constexpr bool IsDigit(char ch) { return ch >= '0' && ch <= '9'; }

constexpr bool HasFlag(uint8_t flags, uint8_t bit) { return (flags & bit) != 0; }

consteval void FormatParseFailNumberOverflow()
{
  __builtin_trap();
}

consteval void FormatParseFailUnexpectedEnd()
{
  __builtin_trap();
}

consteval void FormatParseFailDynamicField()
{
  __builtin_trap();
}

consteval void FormatParseFailInvalidSpecifier()
{
  __builtin_trap();
}

consteval void FormatParseFailInvalidLength()
{
  __builtin_trap();
}

consteval void FormatBuildFailTextOffsetOverflow()
{
  __builtin_trap();
}

consteval void FormatBuildFailTextLengthOverflow()
{
  __builtin_trap();
}

consteval void FormatBuildFailLayoutMismatch()
{
  __builtin_trap();
}

template <typename UInt>
consteval UInt ParseUnsigned(const char* text, size_t size, size_t& pos, UInt limit)
{
  if (pos >= size || !IsDigit(text[pos]))
  {
    return 0;
  }

  UInt value = 0;
  while (pos < size && IsDigit(text[pos]))
  {
    auto digit = static_cast<UInt>(text[pos] - '0');
    if (value > static_cast<UInt>((limit - digit) / 10))
    {
      FormatParseFailNumberOverflow();
    }
    value = static_cast<UInt>(value * 10 + digit);
    ++pos;
  }

  return value;
}

struct ParsedRecord
{
  FormatRecordType type = FormatRecordType::End;
  FormatLength length = FormatLength::Default;
  uint8_t flags = 0;
  uint8_t width = 0;
  uint8_t precision = std::numeric_limits<uint8_t>::max();
};

constexpr bool IsLengthAllowed(FormatRecordType type, FormatLength length)
{
  switch (type)
  {
    case FormatRecordType::SignedDecimal:
    case FormatRecordType::UnsignedDecimal:
    case FormatRecordType::Octal:
    case FormatRecordType::HexLower:
    case FormatRecordType::HexUpper:
      return length != FormatLength::LongDouble;

    case FormatRecordType::Pointer:
    case FormatRecordType::Character:
    case FormatRecordType::CString:
      return length == FormatLength::Default;

    case FormatRecordType::FloatFixed:
    case FormatRecordType::FloatScientific:
    case FormatRecordType::FloatGeneral:
      return length == FormatLength::Default || length == FormatLength::LongDouble;

    case FormatRecordType::End:
    case FormatRecordType::TextInline:
    case FormatRecordType::TextRef:
      return false;
  }

  return false;
}

template <FixedString Text>
consteval ParsedRecord ParseRecord(size_t& pos)
{
  ParsedRecord record;
  const auto size = Text.Size();

  ++pos;
  if (pos >= size)
  {
    FormatParseFailUnexpectedEnd();
  }

  while (pos < size)
  {
    switch (Text.value[pos])
    {
      case '-':
        record.flags |= LeftAlign;
        ++pos;
        continue;
      case '+':
        record.flags |= ForceSign;
        ++pos;
        continue;
      case ' ':
        record.flags |= SpaceSign;
        ++pos;
        continue;
      case '#':
        record.flags |= AlternateForm;
        ++pos;
        continue;
      case '0':
        record.flags |= ZeroPad;
        ++pos;
        continue;
      default:
        break;
    }
    break;
  }

  if (pos < size && Text.value[pos] == '*')
  {
    FormatParseFailDynamicField();
  }

  record.width =
      ParseUnsigned<uint8_t>(Text.Data(), size, pos, std::numeric_limits<uint8_t>::max());

  if (pos < size && Text.value[pos] == '.')
  {
    ++pos;
    if (pos < size && Text.value[pos] == '*')
    {
      FormatParseFailDynamicField();
    }

    if (pos < size && IsDigit(Text.value[pos]))
    {
      record.precision = ParseUnsigned<uint8_t>(
          Text.Data(), size, pos, std::numeric_limits<uint8_t>::max() - 1);
    }
    else
    {
      record.precision = 0;
    }
  }

  if (pos < size)
  {
    if (Text.value[pos] == 'h' || Text.value[pos] == 'l')
    {
      char length = Text.value[pos++];
      record.length = (length == 'h') ? FormatLength::Short : FormatLength::Long;
      if (pos < size && Text.value[pos] == length)
      {
        record.length = (length == 'h') ? FormatLength::Char : FormatLength::LongLong;
        ++pos;
      }
    }
    else if (Text.value[pos] == 'j')
    {
      record.length = FormatLength::IntMax;
      ++pos;
    }
    else if (Text.value[pos] == 'z')
    {
      record.length = FormatLength::Size;
      ++pos;
    }
    else if (Text.value[pos] == 't')
    {
      record.length = FormatLength::PtrDiff;
      ++pos;
    }
    else if (Text.value[pos] == 'L')
    {
      record.length = FormatLength::LongDouble;
      ++pos;
    }
  }

  if (pos >= size)
  {
    FormatParseFailUnexpectedEnd();
  }

  switch (Text.value[pos])
  {
    case 'd':
    case 'i':
      record.type = FormatRecordType::SignedDecimal;
      break;
    case 'u':
      record.type = FormatRecordType::UnsignedDecimal;
      break;
    case 'o':
      record.type = FormatRecordType::Octal;
      break;
    case 'x':
      record.type = FormatRecordType::HexLower;
      break;
    case 'X':
      record.type = FormatRecordType::HexUpper;
      break;
    case 'p':
      record.type = FormatRecordType::Pointer;
      break;
    case 'c':
      record.type = FormatRecordType::Character;
      break;
    case 's':
      record.type = FormatRecordType::CString;
      break;
    case 'f':
      record.type = FormatRecordType::FloatFixed;
      break;
    case 'F':
      record.type = FormatRecordType::FloatFixed;
      record.flags |= UpperCase;
      break;
    case 'e':
      record.type = FormatRecordType::FloatScientific;
      break;
    case 'E':
      record.type = FormatRecordType::FloatScientific;
      record.flags |= UpperCase;
      break;
    case 'g':
      record.type = FormatRecordType::FloatGeneral;
      break;
    case 'G':
      record.type = FormatRecordType::FloatGeneral;
      record.flags |= UpperCase;
      break;
    default:
      FormatParseFailInvalidSpecifier();
      break;
  }

  if (!IsLengthAllowed(record.type, record.length))
  {
    FormatParseFailInvalidLength();
  }

  ++pos;
  return record;
}

struct FormatLayout
{
  size_t record_bytes = 1;
  size_t text_pool_bytes = 0;
  size_t args = 0;
};

constexpr size_t TextRecordBytes(size_t len)
{
  if (len == 0)
  {
    return 0;
  }
  if (len <= format_inline_text_limit)
  {
    return 1 + len + 1;
  }
  return 1 + sizeof(TextOffset) + sizeof(TextLength);
}

template <size_t StorageSize>
consteval void EmitByte(std::array<uint8_t, StorageSize>& data, size_t& out, uint8_t value)
{
  data[out++] = value;
}

template <size_t StorageSize, typename T>
consteval void EmitNative(std::array<uint8_t, StorageSize>& data, size_t& out, T value)
{
  auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
  for (auto byte : bytes)
  {
    data[out++] = byte;
  }
}

template <size_t StorageSize, FixedString Text>
consteval void EmitTextSpan(std::array<uint8_t, StorageSize>& data, size_t& record_out,
                            size_t& text_out, size_t record_bytes, size_t offset, size_t len)
{
  if (len == 0)
  {
    return;
  }

  if (len <= format_inline_text_limit)
  {
    EmitByte(data, record_out, static_cast<uint8_t>(FormatRecordType::TextInline));
    for (size_t i = 0; i < len; ++i)
    {
      EmitByte(data, record_out, static_cast<uint8_t>(Text.value[offset + i]));
    }
    EmitByte(data, record_out, 0);
    return;
  }

  auto text_offset = text_out - record_bytes;
  if (text_offset > std::numeric_limits<TextOffset>::max())
  {
    FormatBuildFailTextOffsetOverflow();
  }
  if (len > std::numeric_limits<TextLength>::max())
  {
    FormatBuildFailTextLengthOverflow();
  }

  EmitByte(data, record_out, static_cast<uint8_t>(FormatRecordType::TextRef));
  EmitNative(data, record_out, static_cast<TextOffset>(text_offset));
  EmitNative(data, record_out, static_cast<TextLength>(len));

  for (size_t i = 0; i < len; ++i)
  {
    data[text_out++] = static_cast<uint8_t>(Text.value[offset + i]);
  }
}

template <size_t StorageSize>
consteval void EmitInlineChar(std::array<uint8_t, StorageSize>& data, size_t& out, char value)
{
  EmitByte(data, out, static_cast<uint8_t>(FormatRecordType::TextInline));
  EmitByte(data, out, static_cast<uint8_t>(value));
  EmitByte(data, out, 0);
}

template <size_t StorageSize>
consteval void EmitValueRecord(std::array<uint8_t, StorageSize>& data, size_t& out,
                               const ParsedRecord& record, uint8_t arg_index)
{
  EmitByte(data, out, static_cast<uint8_t>(record.type));
  EmitByte(data, out, arg_index);
  EmitByte(data, out, record.flags);
  EmitByte(data, out, record.width);
  EmitByte(data, out, record.precision);
}

template <FixedString Text>
consteval FormatLayout AnalyzeFormat()
{
  FormatLayout layout;
  size_t pos = 0;
  size_t text_begin = 0;

  while (pos < Text.Size())
  {
    if (Text.value[pos] != '%')
    {
      ++pos;
      continue;
    }

    if (pos + 1 < Text.Size() && Text.value[pos + 1] == '%')
    {
      layout.record_bytes += TextRecordBytes(pos - text_begin);
      layout.record_bytes += TextRecordBytes(1);
      pos += 2;
      text_begin = pos;
      continue;
    }

    auto text_len = pos - text_begin;
    layout.record_bytes += TextRecordBytes(text_len);
    if (text_len > format_inline_text_limit)
    {
      layout.text_pool_bytes += text_len;
    }
    auto parse_pos = pos;
    ParseRecord<Text>(parse_pos);
    layout.record_bytes += 5;
    ++layout.args;
    pos = parse_pos;
    text_begin = pos;
  }

  auto text_len = Text.Size() - text_begin;
  layout.record_bytes += TextRecordBytes(text_len);
  if (text_len > format_inline_text_limit)
  {
    layout.text_pool_bytes += text_len;
  }
  return layout;
}

template <FixedString Text, size_t RecordBytes, size_t TextPoolBytes>
consteval auto BuildData()
{
  std::array<uint8_t, RecordBytes + TextPoolBytes> data{};
  size_t record_out = 0;
  size_t text_out = RecordBytes;
  size_t pos = 0;
  size_t text_begin = 0;
  uint8_t arg_index = 0;

  while (pos < Text.Size())
  {
    if (Text.value[pos] != '%')
    {
      ++pos;
      continue;
    }

    if (pos + 1 < Text.Size() && Text.value[pos + 1] == '%')
    {
      EmitTextSpan<RecordBytes + TextPoolBytes, Text>(data, record_out, text_out, RecordBytes,
                                                      text_begin, pos - text_begin);
      EmitInlineChar(data, record_out, '%');
      pos += 2;
      text_begin = pos;
      continue;
    }

    EmitTextSpan<RecordBytes + TextPoolBytes, Text>(data, record_out, text_out, RecordBytes,
                                                    text_begin, pos - text_begin);
    auto parse_pos = pos;
    auto record = ParseRecord<Text>(parse_pos);
    EmitValueRecord(data, record_out, record, arg_index++);
    pos = parse_pos;
    text_begin = pos;
  }

  EmitTextSpan<RecordBytes + TextPoolBytes, Text>(data, record_out, text_out, RecordBytes,
                                                  text_begin, Text.Size() - text_begin);
  EmitByte(data, record_out, static_cast<uint8_t>(FormatRecordType::End));
  if (record_out != RecordBytes || text_out != data.size())
  {
    FormatBuildFailLayoutMismatch();
  }
  return data;
}

template <typename T>
consteval bool ValidateSignedIntegerArgument(FormatLength length)
{
  switch (length)
  {
    case FormatLength::Default:
      return is_signed_value<T>;
    case FormatLength::Char:
      return is_exact_base_type<T, signed char>;
    case FormatLength::Short:
      return is_exact_base_type<T, short>;
    case FormatLength::Long:
      return is_exact_base_type<T, long>;
    case FormatLength::LongLong:
      return is_exact_base_type<T, long long>;
    case FormatLength::IntMax:
      return is_exact_base_type<T, intmax_t>;
    case FormatLength::Size:
      return is_exact_base_type<T, std::make_signed_t<size_t>>;
    case FormatLength::PtrDiff:
      return is_exact_base_type<T, ptrdiff_t>;
    case FormatLength::LongDouble:
      return false;
  }

  return false;
}

template <typename T>
consteval bool ValidateUnsignedIntegerArgument(FormatLength length)
{
  switch (length)
  {
    case FormatLength::Default:
      return is_unsigned_value<T>;
    case FormatLength::Char:
      return is_exact_base_type<T, unsigned char>;
    case FormatLength::Short:
      return is_exact_base_type<T, unsigned short>;
    case FormatLength::Long:
      return is_exact_base_type<T, unsigned long>;
    case FormatLength::LongLong:
      return is_exact_base_type<T, unsigned long long>;
    case FormatLength::IntMax:
      return is_exact_base_type<T, uintmax_t>;
    case FormatLength::Size:
      return is_exact_base_type<T, size_t>;
    case FormatLength::PtrDiff:
      return is_exact_base_type<T, std::make_unsigned_t<ptrdiff_t>>;
    case FormatLength::LongDouble:
      return false;
  }

  return false;
}

template <typename T>
consteval bool ValidateFloatArgument(FormatLength length)
{
  switch (length)
  {
    case FormatLength::Default:
      return std::is_same_v<DecayT<T>, float> || std::is_same_v<DecayT<T>, double>;
    case FormatLength::LongDouble:
      return std::is_same_v<DecayT<T>, long double>;
    case FormatLength::Char:
    case FormatLength::Short:
    case FormatLength::Long:
    case FormatLength::LongLong:
    case FormatLength::IntMax:
    case FormatLength::Size:
    case FormatLength::PtrDiff:
      return false;
  }

  return false;
}

template <typename T>
consteval bool ValidateRecordArgument(const ParsedRecord& record)
{
  switch (record.type)
  {
    case FormatRecordType::SignedDecimal:
      return ValidateSignedIntegerArgument<T>(record.length);
    case FormatRecordType::UnsignedDecimal:
    case FormatRecordType::Octal:
    case FormatRecordType::HexLower:
    case FormatRecordType::HexUpper:
      return ValidateUnsignedIntegerArgument<T>(record.length);
    case FormatRecordType::Pointer:
      return is_pointer_value<T>;
    case FormatRecordType::Character:
      return std::is_integral_v<ValueBaseTypeT<T>> || std::is_enum_v<DecayT<T>>;
    case FormatRecordType::CString:
      return is_string_like<T>;
    case FormatRecordType::FloatFixed:
    case FormatRecordType::FloatScientific:
    case FormatRecordType::FloatGeneral:
      return ValidateFloatArgument<T>(record.length);
    case FormatRecordType::End:
    case FormatRecordType::TextInline:
    case FormatRecordType::TextRef:
      return true;
  }

  return false;
}

template <typename Tuple, size_t I = 0>
consteval bool ValidateTupleIndex(const ParsedRecord& record, size_t index)
{
  if constexpr (I >= std::tuple_size_v<Tuple>)
  {
    return false;
  }
  else
  {
    if (I == index)
    {
      return ValidateRecordArgument<std::tuple_element_t<I, Tuple>>(record);
    }
    return ValidateTupleIndex<Tuple, I + 1>(record, index);
  }
}

template <typename T>
constexpr std::string_view ToStringView(const T& text)
{
  if constexpr (std::is_same_v<DecayT<T>, std::string_view>)
  {
    return text;
  }
  else if constexpr (std::is_same_v<DecayT<T>, std::string>)
  {
    return std::string_view(text);
  }
  else if constexpr (std::is_same_v<DecayT<T>, const char*> ||
                     std::is_same_v<DecayT<T>, char*>)
  {
    return text != nullptr ? std::string_view(text) : std::string_view("(null)");
  }
  else if constexpr (is_character_array<T>)
  {
    return std::string_view(text);
  }
  else
  {
    return {};
  }
}

template <typename UInt>
size_t AppendUnsigned(char* out, UInt value, uint8_t base, bool upper_case)
{
  char reversed[sizeof(UInt) * 8]{};
  size_t size = 0;

  do
  {
    auto digit = static_cast<uint8_t>(value % base);
    if (digit < 10)
    {
      reversed[size++] = static_cast<char>('0' + digit);
    }
    else
    {
      reversed[size++] = static_cast<char>((upper_case ? 'A' : 'a') + digit - 10);
    }
    value /= base;
  } while (value != 0);

  for (size_t i = 0; i < size; ++i)
  {
    out[i] = reversed[size - 1 - i];
  }
  return size;
}

inline size_t MinSize(size_t a, size_t b) { return a < b ? a : b; }

template <FormatSink Sink>
ErrorCode WritePadding(Sink& sink, char fill, size_t count)
{
  if (count == 0)
  {
    return ErrorCode::OK;
  }

  char block[16];
  std::memset(block, fill, sizeof(block));

  while (count > 0)
  {
    auto chunk = MinSize(count, sizeof(block));
    auto ec = sink.Write(std::string_view(block, chunk));
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    count -= chunk;
  }

  return ErrorCode::OK;
}

template <FormatSink Sink>
ErrorCode WriteTextField(Sink& sink, std::string_view text, uint8_t width, bool left_align)
{
  auto pad = width > text.size() ? static_cast<size_t>(width - text.size()) : 0;

  if (!left_align)
  {
    auto ec = WritePadding(sink, ' ', pad);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  auto ec = sink.Write(text);
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  if (left_align)
  {
    return WritePadding(sink, ' ', pad);
  }

  return ErrorCode::OK;
}

template <FormatSink Sink>
ErrorCode WriteIntegerField(Sink& sink, char sign_char, std::string_view prefix,
                            std::string_view digits, uint8_t flags, uint8_t width,
                            uint8_t precision)
{
  auto precision_specified = precision != std::numeric_limits<uint8_t>::max();
  auto prefix_size = prefix.size() + (sign_char == '\0' ? 0 : 1);
  auto zero_fill = static_cast<size_t>(0);

  if (precision_specified && precision > digits.size())
  {
    zero_fill = precision - digits.size();
  }

  auto total = prefix_size + zero_fill + digits.size();
  auto left_align = HasFlag(flags, LeftAlign);
  auto zero_pad = HasFlag(flags, ZeroPad) && !left_align && !precision_specified;
  auto fill_size = width > total ? static_cast<size_t>(width - total) : 0;

  if (!left_align && !zero_pad)
  {
    auto ec = WritePadding(sink, ' ', fill_size);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (sign_char != '\0')
  {
    auto ec = sink.Write(std::string_view(&sign_char, 1));
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (!prefix.empty())
  {
    auto ec = sink.Write(prefix);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (zero_pad)
  {
    auto ec = WritePadding(sink, '0', fill_size);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (zero_fill > 0)
  {
    auto ec = WritePadding(sink, '0', zero_fill);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (!digits.empty())
  {
    auto ec = sink.Write(digits);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  if (left_align)
  {
    return WritePadding(sink, ' ', fill_size);
  }

  return ErrorCode::OK;
}

template <typename T>
char ResolveSignChar(T value, uint8_t flags)
{
  if (value < 0)
  {
    return '-';
  }
  if (HasFlag(flags, ForceSign))
  {
    return '+';
  }
  if (HasFlag(flags, SpaceSign))
  {
    return ' ';
  }
  return '\0';
}

template <FormatSink Sink, typename T>
ErrorCode WriteSignedDecimal(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision,
                             T value)
{
  using Base = ValueBaseTypeT<T>;
  using Unsigned = std::make_unsigned_t<Base>;

  Base base_value = static_cast<Base>(value);
  auto negative = base_value < 0;
  Unsigned magnitude = static_cast<Unsigned>(base_value);
  if (negative)
  {
    magnitude = static_cast<Unsigned>(Unsigned(0) - magnitude);
  }

  char digits_buffer[sizeof(Unsigned) * 8]{};
  size_t digits_size = 0;

  if (!(precision == 0 && magnitude == 0))
  {
    digits_size = AppendUnsigned(digits_buffer, magnitude, 10, false);
  }

  return WriteIntegerField(sink, ResolveSignChar(base_value, flags), {},
                           std::string_view(digits_buffer, digits_size), flags, width,
                           precision);
}

template <FormatSink Sink, typename T>
ErrorCode WriteUnsignedInteger(Sink& sink, FormatRecordType type, uint8_t flags, uint8_t width,
                               uint8_t precision, T value)
{
  using Base = ValueBaseTypeT<T>;
  auto base_value = static_cast<Base>(value);

  uint8_t base = 10;
  bool upper_case = false;
  std::string_view prefix;

  switch (type)
  {
    case FormatRecordType::UnsignedDecimal:
      base = 10;
      break;
    case FormatRecordType::Octal:
      base = 8;
      if (HasFlag(flags, AlternateForm) && base_value != 0)
      {
        prefix = "0";
      }
      break;
    case FormatRecordType::HexLower:
      base = 16;
      if (HasFlag(flags, AlternateForm) && base_value != 0)
      {
        prefix = "0x";
      }
      break;
    case FormatRecordType::HexUpper:
      base = 16;
      upper_case = true;
      if (HasFlag(flags, AlternateForm) && base_value != 0)
      {
        prefix = "0X";
      }
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  char digits_buffer[sizeof(Base) * 8]{};
  size_t digits_size = 0;

  if (!(precision == 0 && base_value == 0))
  {
    digits_size = AppendUnsigned(digits_buffer, base_value, base, upper_case);
  }
  else if (type == FormatRecordType::Octal && HasFlag(flags, AlternateForm))
  {
    digits_buffer[0] = '0';
    digits_size = 1;
  }

  return WriteIntegerField(sink, '\0', prefix, std::string_view(digits_buffer, digits_size),
                           flags, width, precision);
}

template <FormatSink Sink, typename T>
ErrorCode WritePointer(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision, T value)
{
  uintptr_t address = 0;
  if constexpr (std::is_same_v<DecayT<T>, std::nullptr_t>)
  {
    address = 0;
  }
  else if constexpr (std::is_pointer_v<DecayT<T>>)
  {
    address = reinterpret_cast<uintptr_t>(value);
  }
  else
  {
    return ErrorCode::ARG_ERR;
  }

  char digits_buffer[sizeof(uintptr_t) * 2]{};
  auto digits_size = AppendUnsigned(digits_buffer, address, 16, false);
  auto effective_precision =
      precision == std::numeric_limits<uint8_t>::max() ? uint8_t(1) : precision;

  return WriteIntegerField(sink, '\0', "0x",
                           std::string_view(digits_buffer, digits_size), flags, width,
                           effective_precision);
}

template <FormatSink Sink, typename T>
ErrorCode WriteCharacter(Sink& sink, uint8_t flags, uint8_t width, T value)
{
  char ch = static_cast<char>(static_cast<ValueBaseTypeT<T>>(value));
  return WriteTextField(sink, std::string_view(&ch, 1), width, HasFlag(flags, LeftAlign));
}

template <FormatSink Sink, typename T>
ErrorCode WriteCString(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision,
                       const T& value)
{
  auto text = ToStringView(value);
  if (precision != std::numeric_limits<uint8_t>::max() && text.size() > precision)
  {
    text = text.substr(0, precision);
  }

  return WriteTextField(sink, text, width, HasFlag(flags, LeftAlign));
}

inline size_t AppendSmallUnsigned(char* out, uint8_t value)
{
  if (value >= 100)
  {
    out[0] = static_cast<char>('0' + value / 100);
    out[1] = static_cast<char>('0' + (value / 10) % 10);
    out[2] = static_cast<char>('0' + value % 10);
    return 3;
  }

  if (value >= 10)
  {
    out[0] = static_cast<char>('0' + value / 10);
    out[1] = static_cast<char>('0' + value % 10);
    return 2;
  }

  out[0] = static_cast<char>('0' + value);
  return 1;
}

template <FormatSink Sink, typename T>
ErrorCode WriteFloat(Sink& sink, FormatRecordType type, uint8_t flags, uint8_t width,
                     uint8_t precision, T value)
{
  char format[20]{};
  size_t format_size = 0;

  format[format_size++] = '%';

  if (HasFlag(flags, LeftAlign))
  {
    format[format_size++] = '-';
  }
  if (HasFlag(flags, ForceSign))
  {
    format[format_size++] = '+';
  }
  else if (HasFlag(flags, SpaceSign))
  {
    format[format_size++] = ' ';
  }
  if (HasFlag(flags, AlternateForm))
  {
    format[format_size++] = '#';
  }
  if (HasFlag(flags, ZeroPad) && !HasFlag(flags, LeftAlign))
  {
    format[format_size++] = '0';
  }
  if (width != 0)
  {
    format_size += AppendSmallUnsigned(format + format_size, width);
  }
  if (precision != std::numeric_limits<uint8_t>::max())
  {
    format[format_size++] = '.';
    format_size += AppendSmallUnsigned(format + format_size, precision);
  }
  if constexpr (std::is_same_v<DecayT<T>, long double>)
  {
    format[format_size++] = 'L';
  }

  switch (type)
  {
    case FormatRecordType::FloatFixed:
      format[format_size++] = HasFlag(flags, UpperCase) ? 'F' : 'f';
      break;
    case FormatRecordType::FloatScientific:
      format[format_size++] = HasFlag(flags, UpperCase) ? 'E' : 'e';
      break;
    case FormatRecordType::FloatGeneral:
      format[format_size++] = HasFlag(flags, UpperCase) ? 'G' : 'g';
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  format[format_size] = '\0';

  char buffer[384]{};
  int size = 0;
  if constexpr (std::is_same_v<DecayT<T>, long double>)
  {
    size = std::snprintf(buffer, sizeof(buffer), format, value);
  }
  else
  {
    size = std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(value));
  }

  if (size < 0)
  {
    return ErrorCode::FAILED;
  }
  if (static_cast<size_t>(size) >= sizeof(buffer))
  {
    size = sizeof(buffer) - 1;
  }

  return sink.Write(std::string_view(buffer, static_cast<size_t>(size)));
}

template <typename T>
ErrorCode WriteValue(FormatRecordType type, uint8_t flags, uint8_t width, uint8_t precision,
                     auto& sink, T&& value)
{
  if constexpr (std::is_floating_point_v<DecayT<T>>)
  {
    switch (type)
    {
      case FormatRecordType::FloatFixed:
      case FormatRecordType::FloatScientific:
      case FormatRecordType::FloatGeneral:
        return WriteFloat(sink, type, flags, width, precision, std::forward<T>(value));
      default:
        return ErrorCode::ARG_ERR;
    }
  }
  else if constexpr (is_string_like<T>)
  {
    if (type == FormatRecordType::CString)
    {
      return WriteCString(sink, flags, width, precision, std::forward<T>(value));
    }
    return ErrorCode::ARG_ERR;
  }
  else if constexpr (is_pointer_value<T>)
  {
    if (type == FormatRecordType::Pointer)
    {
      return WritePointer(sink, flags, width, precision, std::forward<T>(value));
    }
    return ErrorCode::ARG_ERR;
  }
  else if constexpr (std::is_integral_v<ValueBaseTypeT<T>> || std::is_enum_v<DecayT<T>>)
  {
    switch (type)
    {
      case FormatRecordType::SignedDecimal:
        if constexpr (is_signed_value<T>)
        {
          return WriteSignedDecimal(sink, flags, width, precision, std::forward<T>(value));
        }
        return ErrorCode::ARG_ERR;

      case FormatRecordType::UnsignedDecimal:
      case FormatRecordType::Octal:
      case FormatRecordType::HexLower:
      case FormatRecordType::HexUpper:
        if constexpr (is_unsigned_value<T>)
        {
          return WriteUnsignedInteger(sink, type, flags, width, precision,
                                      std::forward<T>(value));
        }
        return ErrorCode::ARG_ERR;

      case FormatRecordType::Character:
        return WriteCharacter(sink, flags, width, std::forward<T>(value));

      default:
        return ErrorCode::ARG_ERR;
    }
  }
  else
  {
    return ErrorCode::ARG_ERR;
  }
}

template <typename T>
T LoadNative(const uint8_t*& pos)
{
  T value{};
  std::memcpy(&value, pos, sizeof(T));
  pos += sizeof(T);
  return value;
}
}  // namespace Detail

template <FixedString Text>
class Format
{
 public:
  using RecordType = FormatRecordType;

  [[nodiscard]] static constexpr size_t ArgumentCount() { return argument_count_; }
  [[nodiscard]] static constexpr size_t RecordBytes() { return record_bytes_; }

  [[nodiscard]] static constexpr const auto& Data() { return data_; }

  template <typename... Args>
  [[nodiscard]] static consteval bool MatchesArguments()
  {
    return ValidateArguments<Args...>();
  }

  template <FormatSink Sink, typename... Args>
  ErrorCode WriteTo(Sink& sink, Args&&... args) const
  {
    return Print::Write(sink, *this, std::forward<Args>(args)...);
  }

 private:
  inline static constexpr auto layout_ = Detail::AnalyzeFormat<Text>();
  static constexpr size_t argument_count_ = layout_.args;
  static constexpr size_t record_bytes_ = layout_.record_bytes;

  template <typename... Args>
  static consteval bool ValidateArguments()
  {
    using Tuple = std::tuple<std::remove_cvref_t<Args>...>;
    size_t pos = 0;
    size_t arg_index = 0;

    while (pos < Text.Size())
    {
      if (Text.value[pos] != '%')
      {
        ++pos;
        continue;
      }

      if (pos + 1 < Text.Size() && Text.value[pos + 1] == '%')
      {
        pos += 2;
        continue;
      }

      auto parse_pos = pos;
      auto record = Detail::ParseRecord<Text>(parse_pos);
      if (!Detail::ValidateTupleIndex<Tuple>(record, arg_index))
      {
        return false;
      }

      ++arg_index;
      pos = parse_pos;
    }

    return arg_index == std::tuple_size_v<Tuple>;
  }

  inline static constexpr auto data_ =
      Detail::BuildData<Text, layout_.record_bytes, layout_.text_pool_bytes>();
};

namespace Print
{
namespace Detail
{
template <typename T>
inline constexpr bool always_false = false;

enum class ArgumentType : uint8_t
{
  Signed,
  Unsigned,
  Float64,
  LongDouble,
  String,
  Pointer,
};

struct PackedString
{
  const char* data = nullptr;
  size_t size = 0;
};

struct Argument
{
  ArgumentType type = ArgumentType::Unsigned;

  union Value
  {
    int64_t signed_value;
    uint64_t unsigned_value;
    double float64_value;
    long double long_double_value;
    PackedString string_value;
    uintptr_t pointer_value;

    constexpr Value() : unsigned_value(0) {}
  } value;
};

template <typename T>
Argument PackArgument(T&& value)
{
  using Base = LibXR::Detail::ValueBaseTypeT<T>;
  Argument arg{};

  if constexpr (std::is_same_v<LibXR::Detail::DecayT<T>, long double>)
  {
    arg.type = ArgumentType::LongDouble;
    arg.value.long_double_value = value;
  }
  else if constexpr (std::is_floating_point_v<LibXR::Detail::DecayT<T>>)
  {
    arg.type = ArgumentType::Float64;
    arg.value.float64_value = static_cast<double>(value);
  }
  else if constexpr (LibXR::Detail::is_string_like<T>)
  {
    auto text = LibXR::Detail::ToStringView(value);
    arg.type = ArgumentType::String;
    arg.value.string_value = {text.data(), text.size()};
  }
  else if constexpr (LibXR::Detail::is_pointer_value<T>)
  {
    arg.type = ArgumentType::Pointer;
    if constexpr (std::is_same_v<LibXR::Detail::DecayT<T>, std::nullptr_t>)
    {
      arg.value.pointer_value = 0;
    }
    else
    {
      arg.value.pointer_value = reinterpret_cast<uintptr_t>(value);
    }
  }
  else if constexpr (std::is_integral_v<Base> || std::is_enum_v<LibXR::Detail::DecayT<T>>)
  {
    if constexpr (LibXR::Detail::is_signed_value<T>)
    {
      arg.type = ArgumentType::Signed;
      arg.value.signed_value = static_cast<int64_t>(static_cast<Base>(value));
    }
    else
    {
      arg.type = ArgumentType::Unsigned;
      arg.value.unsigned_value = static_cast<uint64_t>(static_cast<Base>(value));
    }
  }
  else
  {
    static_assert(always_false<T>, "Unsupported print argument type");
  }

  return arg;
}

template <FormatSink Sink>
ErrorCode WriteSignedValue(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision,
                           int64_t value)
{
  using Unsigned = uint64_t;

  auto negative = value < 0;
  Unsigned magnitude = static_cast<Unsigned>(value);
  if (negative)
  {
    magnitude = static_cast<Unsigned>(Unsigned(0) - magnitude);
  }

  char digits_buffer[sizeof(Unsigned) * 8]{};
  size_t digits_size = 0;
  if (!(precision == 0 && magnitude == 0))
  {
    digits_size = LibXR::Detail::AppendUnsigned(digits_buffer, magnitude, 10, false);
  }

  return LibXR::Detail::WriteIntegerField(
      sink, LibXR::Detail::ResolveSignChar(value, flags), {},
      std::string_view(digits_buffer, digits_size), flags, width, precision);
}

template <FormatSink Sink>
ErrorCode WriteUnsignedValue(Sink& sink, FormatRecordType type, uint8_t flags, uint8_t width,
                             uint8_t precision, uint64_t value)
{
  uint8_t base = 10;
  bool upper_case = false;
  std::string_view prefix;

  switch (type)
  {
    case FormatRecordType::UnsignedDecimal:
      break;
    case FormatRecordType::Octal:
      base = 8;
      if (LibXR::Detail::HasFlag(flags, AlternateForm) && value != 0)
      {
        prefix = "0";
      }
      break;
    case FormatRecordType::HexLower:
      base = 16;
      if (LibXR::Detail::HasFlag(flags, AlternateForm) && value != 0)
      {
        prefix = "0x";
      }
      break;
    case FormatRecordType::HexUpper:
      base = 16;
      upper_case = true;
      if (LibXR::Detail::HasFlag(flags, AlternateForm) && value != 0)
      {
        prefix = "0X";
      }
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  char digits_buffer[sizeof(uint64_t) * 8]{};
  size_t digits_size = 0;
  if (!(precision == 0 && value == 0))
  {
    digits_size = LibXR::Detail::AppendUnsigned(digits_buffer, value, base, upper_case);
  }
  else if (type == FormatRecordType::Octal && LibXR::Detail::HasFlag(flags, AlternateForm))
  {
    digits_buffer[0] = '0';
    digits_size = 1;
  }

  return LibXR::Detail::WriteIntegerField(sink, '\0', prefix,
                                          std::string_view(digits_buffer, digits_size), flags,
                                          width, precision);
}

template <FormatSink Sink>
ErrorCode WritePointerValue(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision,
                            uintptr_t value)
{
  char digits_buffer[sizeof(uintptr_t) * 2]{};
  auto digits_size = LibXR::Detail::AppendUnsigned(digits_buffer, value, 16, false);
  auto effective_precision =
      precision == std::numeric_limits<uint8_t>::max() ? uint8_t(1) : precision;

  return LibXR::Detail::WriteIntegerField(sink, '\0', "0x",
                                          std::string_view(digits_buffer, digits_size), flags,
                                          width, effective_precision);
}

template <FormatSink Sink>
ErrorCode WriteCharacterValue(Sink& sink, uint8_t flags, uint8_t width, const Argument& arg)
{
  char ch = '\0';

  switch (arg.type)
  {
    case ArgumentType::Signed:
      ch = static_cast<char>(arg.value.signed_value);
      break;
    case ArgumentType::Unsigned:
      ch = static_cast<char>(arg.value.unsigned_value);
      break;
    case ArgumentType::Float64:
    case ArgumentType::LongDouble:
    case ArgumentType::String:
    case ArgumentType::Pointer:
      return ErrorCode::ARG_ERR;
  }

  return LibXR::Detail::WriteTextField(sink, std::string_view(&ch, 1), width,
                                       LibXR::Detail::HasFlag(flags, LeftAlign));
}

template <FormatSink Sink>
ErrorCode WriteStringValue(Sink& sink, uint8_t flags, uint8_t width, uint8_t precision,
                           const Argument& arg)
{
  if (arg.type != ArgumentType::String)
  {
    return ErrorCode::ARG_ERR;
  }

  auto text = std::string_view(arg.value.string_value.data, arg.value.string_value.size);
  if (precision != std::numeric_limits<uint8_t>::max() && text.size() > precision)
  {
    text = text.substr(0, precision);
  }

  return LibXR::Detail::WriteTextField(sink, text, width,
                                       LibXR::Detail::HasFlag(flags, LeftAlign));
}

template <FormatSink Sink>
ErrorCode Execute(Sink& sink, const uint8_t* data, size_t record_bytes, const Argument* args,
                  size_t arg_count)
{
  const uint8_t* pos = data;
  const char* text_base = reinterpret_cast<const char*>(data + record_bytes);

  while (true)
  {
    auto type = static_cast<FormatRecordType>(*pos++);
    switch (type)
    {
      case FormatRecordType::End:
        return ErrorCode::OK;

      case FormatRecordType::TextInline:
      {
        const char* begin = reinterpret_cast<const char*>(pos);
        while (*pos != 0)
        {
          ++pos;
        }
        auto ec =
            sink.Write(std::string_view(begin, reinterpret_cast<const char*>(pos) - begin));
        ++pos;
        if (ec != ErrorCode::OK)
        {
          return ec;
        }
        break;
      }

      case FormatRecordType::TextRef:
      {
        auto offset = LibXR::Detail::LoadNative<TextOffset>(pos);
        auto size = LibXR::Detail::LoadNative<TextLength>(pos);
        auto ec = sink.Write(std::string_view(text_base + offset, size));
        if (ec != ErrorCode::OK)
        {
          return ec;
        }
        break;
      }

      default:
      {
        auto arg_index = static_cast<size_t>(*pos++);
        auto flags = *pos++;
        auto width = *pos++;
        auto precision = *pos++;

        if (arg_index >= arg_count)
        {
          return ErrorCode::ARG_ERR;
        }

        const auto& arg = args[arg_index];
        switch (type)
        {
          case FormatRecordType::SignedDecimal:
            if (arg.type != ArgumentType::Signed)
            {
              return ErrorCode::ARG_ERR;
            }
            if (auto ec = WriteSignedValue(sink, flags, width, precision,
                                           arg.value.signed_value);
                ec != ErrorCode::OK)
            {
              return ec;
            }
            break;

          case FormatRecordType::UnsignedDecimal:
          case FormatRecordType::Octal:
          case FormatRecordType::HexLower:
          case FormatRecordType::HexUpper:
            if (arg.type != ArgumentType::Unsigned)
            {
              return ErrorCode::ARG_ERR;
            }
            if (auto ec = WriteUnsignedValue(sink, type, flags, width, precision,
                                             arg.value.unsigned_value);
                ec != ErrorCode::OK)
            {
              return ec;
            }
            break;

          case FormatRecordType::Pointer:
            if (arg.type != ArgumentType::Pointer)
            {
              return ErrorCode::ARG_ERR;
            }
            if (auto ec = WritePointerValue(sink, flags, width, precision,
                                            arg.value.pointer_value);
                ec != ErrorCode::OK)
            {
              return ec;
            }
            break;

          case FormatRecordType::Character:
            if (auto ec = WriteCharacterValue(sink, flags, width, arg); ec != ErrorCode::OK)
            {
              return ec;
            }
            break;

          case FormatRecordType::CString:
            if (auto ec = WriteStringValue(sink, flags, width, precision, arg);
                ec != ErrorCode::OK)
            {
              return ec;
            }
            break;

          case FormatRecordType::FloatFixed:
          case FormatRecordType::FloatScientific:
          case FormatRecordType::FloatGeneral:
            if (arg.type == ArgumentType::Float64)
            {
              if (auto ec = LibXR::Detail::WriteFloat(sink, type, flags, width, precision,
                                                      arg.value.float64_value);
                  ec != ErrorCode::OK)
              {
                return ec;
              }
              break;
            }

            if (arg.type == ArgumentType::LongDouble)
            {
              if (auto ec = LibXR::Detail::WriteFloat(sink, type, flags, width, precision,
                                                      arg.value.long_double_value);
                  ec != ErrorCode::OK)
              {
                return ec;
              }
              break;
            }

            return ErrorCode::ARG_ERR;

          case FormatRecordType::End:
          case FormatRecordType::TextInline:
          case FormatRecordType::TextRef:
            return ErrorCode::ARG_ERR;
        }
        break;
      }
    }
  }
}
}  // namespace Detail

template <FormatSink Sink, FixedString Text, typename... Args>
ErrorCode Write(Sink& sink, const Format<Text>& format, Args&&... args)
{
  static_assert(sizeof...(Args) == Format<Text>::ArgumentCount(),
                "LibXR::Format argument count does not match the format string");
  static_assert(Format<Text>::template MatchesArguments<Args...>(),
                "LibXR::Format argument types do not match the format string or length "
                "modifiers");

  if constexpr (sizeof...(Args) == 0)
  {
    return Detail::Execute(sink, format.Data().data(), format.RecordBytes(), nullptr, 0);
  }
  else
  {
    Detail::Argument packed[] = {Detail::PackArgument(std::forward<Args>(args))...};
    return Detail::Execute(sink, format.Data().data(), format.RecordBytes(), packed,
                           sizeof...(Args));
  }
}
}  // namespace Print
}  // namespace LibXR
