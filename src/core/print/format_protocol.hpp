#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

/**
 * @brief Print feature defaults for direct header consumers.
 * @brief 供直接包含头文件的用户使用的打印功能默认值。
 *
 * CMake exports the same names as 0 or 1 target compile definitions. When the
 * headers are used without CMake, these fallback values match the ordinary
 * embedded default profile: decimal integers, text, fixed float, base 8/16
 * integers, width, and precision stay enabled, while alternate form (#),
 * pointers, explicit argument indexing, 64-bit integer, double, long double,
 * scientific, and general float formatting stay disabled.
 * CMake 会用同名 0 或 1 编译定义导出这些开关；不经过 CMake 直接使用头文件时，
 * 这里的回退值与常规嵌入式默认配置一致：保留十进制整数、文本、定点浮点、
 * 8/16 进制整数、宽度与精度，默认关闭备用格式（#）、指针、显式参数索引、
 * 64 位整数、double、long double、科学计数法与通用浮点格式。
 */
#ifndef LIBXR_PRINT_ENABLE_INTEGER
#define LIBXR_PRINT_ENABLE_INTEGER 1
#endif

#ifndef LIBXR_PRINT_ENABLE_TEXT
#define LIBXR_PRINT_ENABLE_TEXT 1
#endif

#ifndef LIBXR_PRINT_ENABLE_POINTER
#define LIBXR_PRINT_ENABLE_POINTER 0
#endif

#ifndef LIBXR_PRINT_ENABLE_FLOAT
#define LIBXR_PRINT_ENABLE_FLOAT 1
#endif

#ifndef LIBXR_PRINT_INTEGER_ENABLE_BASE8_16
#define LIBXR_PRINT_INTEGER_ENABLE_BASE8_16 1
#endif

#ifndef LIBXR_PRINT_INTEGER_ENABLE_64BIT
#define LIBXR_PRINT_INTEGER_ENABLE_64BIT 0
#endif

#ifndef LIBXR_PRINT_FLOAT_ENABLE_FIXED
#define LIBXR_PRINT_FLOAT_ENABLE_FIXED 1
#endif

#ifndef LIBXR_PRINT_FLOAT_ENABLE_DOUBLE
#define LIBXR_PRINT_FLOAT_ENABLE_DOUBLE 0
#endif

#ifndef LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC
#define LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC 0
#endif

#ifndef LIBXR_PRINT_FLOAT_ENABLE_GENERAL
#define LIBXR_PRINT_FLOAT_ENABLE_GENERAL 0
#endif

#ifndef LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE
#define LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE 0
#endif

#ifndef LIBXR_PRINT_ENABLE_WIDTH
#define LIBXR_PRINT_ENABLE_WIDTH 1
#endif

#ifndef LIBXR_PRINT_ENABLE_PRECISION
#define LIBXR_PRINT_ENABLE_PRECISION 1
#endif

#ifndef LIBXR_PRINT_ENABLE_ALTERNATE
#define LIBXR_PRINT_ENABLE_ALTERNATE 0
#endif

#ifndef LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING
#define LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING 0
#endif

namespace LibXR::Print::Config
{
/// Enables signed and unsigned decimal integer conversions. / 启用有符号与无符号十进制整数转换
inline constexpr bool enable_integer = LIBXR_PRINT_ENABLE_INTEGER;
/// Enables character and string conversions. / 启用字符与字符串转换
inline constexpr bool enable_text = LIBXR_PRINT_ENABLE_TEXT;
/// Enables pointer conversions. / 启用指针转换
inline constexpr bool enable_pointer = LIBXR_PRINT_ENABLE_POINTER;
/// Master switch for all floating-point conversions. / 所有浮点转换的总开关
inline constexpr bool enable_float = LIBXR_PRINT_ENABLE_FLOAT;

/// Enables binary, octal, and hexadecimal integer conversions when integers are enabled. / 在整数功能开启时启用二进制、八进制和十六进制转换
inline constexpr bool enable_integer_base8_16 =
    enable_integer && LIBXR_PRINT_INTEGER_ENABLE_BASE8_16;
/// Enables 64-bit integer formatting families when integers are enabled. / 在整数功能开启时启用 64 位整数格式化族
inline constexpr bool enable_integer_64bit =
    enable_integer && LIBXR_PRINT_INTEGER_ENABLE_64BIT;

/// Enables %f / %F when floating-point support is enabled. / 在浮点功能开启时启用 %f / %F
inline constexpr bool enable_float_fixed =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_FIXED;
/// Enables double-backed default float formatting when floating-point support is enabled. / 在浮点功能开启时启用基于 double 的默认浮点格式化
inline constexpr bool enable_float_double =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_DOUBLE;
/// Enables %e / %E when floating-point support is enabled. / 在浮点功能开启时启用 %e / %E
inline constexpr bool enable_float_scientific =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC;
/// Enables %g / %G when floating-point support is enabled. / 在浮点功能开启时启用 %g / %G
inline constexpr bool enable_float_general =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_GENERAL;
/// Enables the L floating-point length modifier when floating-point support is enabled. / 在浮点功能开启时启用 L 长度修饰
inline constexpr bool enable_float_long_double =
    enable_float_double && LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE;

/// Enables constant field width parsing. / 启用常量字段宽度解析
inline constexpr bool enable_width = LIBXR_PRINT_ENABLE_WIDTH;
/// Enables constant precision parsing. / 启用常量精度解析
inline constexpr bool enable_precision = LIBXR_PRINT_ENABLE_PRECISION;
/// Enables alternate-form syntax such as # for integer prefixes and float decimal-point retention. / 启用备用格式语法，例如用于整数前缀和浮点保留小数点的 #
inline constexpr bool enable_alternate = LIBXR_PRINT_ENABLE_ALTERNATE;
/// Enables source-level explicit argument indexing such as printf n$ and format {1}. / 启用源级显式参数索引，例如 printf 的 n$ 和 format 的 {1}
inline constexpr bool enable_explicit_argument_indexing =
    LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING;
}  // namespace LibXR::Print::Config

namespace LibXR::Print
{
/**
 * @brief Core format protocol vocabulary shared by every frontend and the runtime writer.
 * @brief 所有前端与运行期 writer 共用的核心格式协议词汇。
 *
 * FormatArgumentRule answers which C++ argument types are accepted at compile time.
 * FormatPackKind answers how one accepted argument is packed into the runtime
 * argument blob. FormatType answers which semantic render path the runtime should
 * take after unpacking. FormatOp answers which concrete bytecode record is stored
 * in Codes(). FormatProfile is the coarse summary used to specialize the runtime
 * executor for the op families that actually appear.
 * FormatArgumentRule 回答编译期允许哪些 C++ 实参类型；FormatPackKind 回答已接受
 * 的实参在运行期参数字节块里如何打包；FormatType 回答运行期解包后应走哪条语义写出
 * 路径；FormatOp 回答 Codes() 里实际存的是哪种字节码记录；FormatProfile 则是对
 * 当前字节码里实际出现了哪些操作码族的粗粒度汇总，用来特化运行期执行器。
 */
/**
 * @brief Compile-time argument matching rules attached to each runtime argument.
 * @brief 每个运行期参数附带的编译期匹配规则。
 */
enum class FormatArgumentRule : uint8_t
{
  None,              ///< no runtime argument is consumed / 不消耗运行期参数
  SignedAny,         ///< default-width signed integer family / 默认宽度有符号整数族
  SignedChar,        ///< exact signed char / 精确匹配 signed char
  SignedShort,       ///< exact short / 精确匹配 short
  SignedLong,        ///< exact long / 精确匹配 long
  SignedLongLong,    ///< exact long long / 精确匹配 long long
  SignedIntMax,      ///< exact intmax_t / 精确匹配 intmax_t
  SignedSize,        ///< exact signed counterpart of size_t / 精确匹配 size_t 的有符号对应类型
  SignedPtrDiff,     ///< exact ptrdiff_t / 精确匹配 ptrdiff_t
  UnsignedAny,       ///< default-width unsigned integer family / 默认宽度无符号整数族
  UnsignedChar,      ///< exact unsigned char / 精确匹配 unsigned char
  UnsignedShort,     ///< exact unsigned short / 精确匹配 unsigned short
  UnsignedLong,      ///< exact unsigned long / 精确匹配 unsigned long
  UnsignedLongLong,  ///< exact unsigned long long / 精确匹配 unsigned long long
  UnsignedIntMax,    ///< exact uintmax_t / 精确匹配 uintmax_t
  UnsignedSize,      ///< exact size_t / 精确匹配 size_t
  UnsignedPtrDiff,   ///< exact unsigned counterpart of ptrdiff_t / 精确匹配 ptrdiff_t 的无符号对应类型
  Pointer,           ///< object pointer or nullptr / 对象指针或 nullptr
  Character,         ///< any integral or enum accepted by %c / 可按 %c 接受的整数或枚举
  String,            ///< C string, string_view, or string / C 字符串、string_view 或 string
  Float,             ///< float or double / float 或 double
  LongDouble,        ///< exact long double / 精确匹配 long double
};

/**
 * @brief Bit flags stored in a value record's field-spec byte.
 * @brief 保存在值记录字段描述字节中的位标志。
 *
 * These bits are not stored in the record-op byte, so their numeric values may
 * overlap with FormatOp values.
 * 这些位不存放在记录操作码字节中，因此数值允许与 FormatOp 重叠。
 */
enum class FormatFlag : uint8_t
{
  LeftAlign = 1U << 0,  ///< - flag; left-align field output / 左对齐输出字段
  ForceSign = 1U << 1,  ///< + flag; always emit sign for signed values / 总是输出符号位
  SpaceSign = 1U << 2,  ///< leading space for positive signed values / 正数前补空格
  Alternate = 1U << 3,  ///< # flag; alternate form such as prefixes / 备用格式，如前缀
  ZeroPad = 1U << 4,    ///< 0 flag; pad field with 0 when allowed / 允许时用 0 填充
  UpperCase = 1U << 5,  ///< uppercase hex / float output / 使用大写十六进制或浮点格式
  CenterAlign = 1U << 6,  ///< centered field output / 居中对齐输出字段
};

/**
 * @brief Runtime bytecode operations consumed by Writer.
 * @brief Writer 消费的运行期字节码操作。
 *
 * Small common cases lower directly to narrow opcodes with only the immediates
 * they actually need. Everything else falls back to GenericField, which keeps
 * the shared "type + flags + fill + width + precision" payload shape.
 * 常见小场景会直接降为只携带必要立即数的窄操作码；其余情况全部回落到
 * GenericField，继续沿用共享的
 * “type + flags + fill + width + precision” 载荷形状。
 */
enum class FormatOp : uint8_t
{
  TextInline = 0x01,       ///< short inline literal text / 直接内嵌在码流中的短字面文本
  TextRef = 0x02,          ///< text span stored in the trailing pool / 引用尾部文本池中的文本片段
  TextSpace = 0x03,        ///< one literal space / 单个字面空格
  U32Dec = 0x10,           ///< raw uint32_t decimal output / 直接输出 uint32_t 十进制
  U32ZeroPadWidth = 0x11,  ///< uint32_t decimal with zero-pad width byte / 带零填充宽度字节的 uint32_t 十进制
  StringRaw = 0x20,        ///< raw string_view output / 直接输出 string_view
  F32FixedPrec = 0x30,     ///< fixed float with one precision byte / 带一个精度字节的定点 float
  F64FixedPrec = 0x31,     ///< fixed double with one precision byte / 带一个精度字节的定点 double
  GenericField = 0xF0,     ///< wide fallback payload: type, flags, fill, width, precision / 宽回退载荷：type、flags、fill、width、precision
  End = 0xFF,              ///< terminates the compiled record stream / 结束整条编译记录流
};

/**
 * @brief Semantic handler categories used by compile-time analysis and runtime dispatch.
 * @brief 编译期分析和运行期分发共用的语义处理类别。
 */
enum class FormatType : uint8_t
{
  End,                   ///< semantic sentinel only; not the byte-stream terminator FormatOp::End / 仅语义哨兵；不等于字节流结束符 FormatOp::End
  TextInline,            ///< short inline text stored in the code stream / 直接内嵌在码流中的短文本
  TextRef,               ///< long text stored in the trailing text pool / 引用尾部文本池中的长文本
  TextSpace,             ///< one literal space / 单个字面空格
  Signed32,              ///< runtime signed decimal stored as int32_t / 运行期按 int32_t 存储的有符号十进制
  Signed64,              ///< runtime signed decimal stored as int64_t / 运行期按 int64_t 存储的有符号十进制
  Unsigned32,            ///< runtime unsigned decimal stored as uint32_t / 运行期按 uint32_t 存储的无符号十进制
  Unsigned64,            ///< runtime unsigned decimal stored as uint64_t / 运行期按 uint64_t 存储的无符号十进制
  Binary32,              ///< runtime binary stored as uint32_t / 运行期按 uint32_t 存储的二进制
  Binary64,              ///< runtime binary stored as uint64_t / 运行期按 uint64_t 存储的二进制
  Octal32,               ///< runtime octal stored as uint32_t / 运行期按 uint32_t 存储的八进制
  Octal64,               ///< runtime octal stored as uint64_t / 运行期按 uint64_t 存储的八进制
  HexLower32,            ///< runtime lowercase hex stored as uint32_t / 运行期按 uint32_t 存储的小写十六进制
  HexLower64,            ///< runtime lowercase hex stored as uint64_t / 运行期按 uint64_t 存储的小写十六进制
  HexUpper32,            ///< runtime uppercase hex stored as uint32_t / 运行期按 uint32_t 存储的大写十六进制
  HexUpper64,            ///< runtime uppercase hex stored as uint64_t / 运行期按 uint64_t 存储的大写十六进制
  Pointer,               ///< pointer value / 指针值
  Character,             ///< character / 单个字符
  String,                ///< string / string view / C string / 字符串、字符串视图或 C 字符串
  FloatFixed,            ///< %f / %F fixed-point float32 / 定点 float32 输出
  FloatScientific,       ///< %e / %E scientific float32 / 科学计数法 float32 输出
  FloatGeneral,          ///< %g / %G general float32 / 通用 float32 输出
  DoubleFixed,           ///< %f / %F fixed-point double / 定点 double 输出
  DoubleScientific,      ///< %e / %E scientific double / 科学计数法 double 输出
  DoubleGeneral,         ///< %g / %G general double / 通用 double 输出
  LongDoubleFixed,       ///< %Lf / %LF fixed-point long double / long double 定点输出
  LongDoubleScientific,  ///< %Le / %LE scientific long double / long double 科学计数法输出
  LongDoubleGeneral,     ///< %Lg / %LG general long double / long double 通用输出
};

/**
 * @brief Packed storage categories for runtime arguments.
 * @brief 运行期参数的打包存储类别。
 *
 * This only answers "how is one argument stored in the packed argument blob".
 * It does not describe how the final text is rendered.
 * 这个枚举只回答“单个参数在运行期参数字节块里如何存储”，不描述最终文本如何渲染。
 */
enum class FormatPackKind : uint8_t
{
  U32,         ///< stored as uint32_t / 按 uint32_t 存储
  U64,         ///< stored as uint64_t / 按 uint64_t 存储
  I32,         ///< stored as int32_t / 按 int32_t 存储
  I64,         ///< stored as int64_t / 按 int64_t 存储
  Pointer,     ///< stored as uintptr_t / 按 uintptr_t 存储
  Character,   ///< stored as char / 按 char 存储
  StringView,  ///< stored as std::string_view / 按 std::string_view 存储
  F32,         ///< stored as float / 按 float 存储
  F64,         ///< stored as double / 按 double 存储
  LongDouble,  ///< stored as long double / 按 long double 存储
};

/**
 * @brief Coarse runtime executor profiles selected at compile time.
 * @brief 编译期选出的粗粒度运行期执行器配置。
 *
 * The low bits describe which narrow fast-path families appear in the bytecode.
 * Generic marks that at least one field still requires the old wide fallback.
 * 低位描述当前字节码里出现了哪些窄快路径族；Generic 表示至少存在一个字段仍需走旧的宽回退路径。
 */
enum class FormatProfile : uint8_t
{
  None = 0,            ///< text-only stream / 只有文本记录的流
  U32 = 1U << 0,       ///< uint32_t decimal fast path / uint32_t 十进制快路径
  TextArg = 1U << 1,   ///< raw string argument fast path / 原始字符串参数快路径
  F32Fixed = 1U << 2,  ///< fixed float fast path / 定点 float 快路径
  F64Fixed = 1U << 3,  ///< fixed double fast path / 定点 double 快路径
  Generic = 1U << 7,   ///< at least one field uses generic fallback / 至少有一个字段使用通用回退
};

/// Combines two profile bit sets. / 合并两组 profile 位
[[nodiscard]] constexpr FormatProfile operator|(FormatProfile left, FormatProfile right)
{
  return static_cast<FormatProfile>(static_cast<uint8_t>(left) |
                                    static_cast<uint8_t>(right));
}

/// Accumulates one profile bit set into another. / 将一组 profile 位累加到另一组
constexpr FormatProfile& operator|=(FormatProfile& left, FormatProfile right)
{
  left = left | right;
  return left;
}

/// Tests whether one profile bit is present. / 判断某个 profile 位是否存在
[[nodiscard]] constexpr bool HasProfile(FormatProfile profile, FormatProfile bit)
{
  return (static_cast<uint8_t>(profile) & static_cast<uint8_t>(bit)) != 0;
}

/**
 * @brief Compiled-format runtime contract consumed by Writer.
 * @brief Writer 消费的编译格式运行期协议。
 *
 * A compiled format source always provides Codes(), one contiguous uint8_t byte
 * block terminated in-band by FormatOp::End; ArgumentList(), one field-ordered
 * FormatArgumentInfo array used for runtime argument packing; and Profile(),
 * one compile-time runtime executor profile. Frontends that allow reordered
 * call-site arguments may additionally expose a separate compile-time
 * ArgumentOrder() list or a source-ordered matching list.
 * 编译格式源总会提供三部分：Codes()，即一段在块内用 FormatOp::End 结束的连续
 * uint8_t 字节块；ArgumentList()，即按字段执行顺序排列、供运行期参数打包使用的
 * FormatArgumentInfo 数组；以及 Profile()，即一个编译期运行期执行器配置。若某个
 * 前端允许调用点参数重排，还可以额外提供独立的编译期 ArgumentOrder() 索引表或
 * 按源参数顺序排列的匹配元信息表。
 */

/**
 * @brief Packed argument metadata shared by compile-time matching and runtime packing.
 * @brief 编译期匹配与运行期打包共用的参数元信息。
 *
 * pack is the runtime storage contract for the packed argument blob. rule is
 * the compile-time type-matching contract and never appears in the runtime
 * bytecode stream.
 * pack 描述运行期参数字节块里的存储契约；rule 描述编译期类型匹配契约，不会出现在
 * 运行期字节码流里。
 */
struct FormatArgumentInfo
{
  FormatPackKind pack{};  ///< packed runtime storage kind / 运行期打包存储类别
  FormatArgumentRule rule =
      FormatArgumentRule::None;  ///< compile-time argument rule / 编译期实参匹配规则
};
static_assert(sizeof(FormatArgumentInfo) == 2,
              "LibXR::Print::FormatArgumentInfo must stay tightly packed");

/**
 * @brief Normalized value-field semantics shared by all formatting frontends.
 * @brief 所有格式前端共用的规范化值字段语义。
 *
 * Each FormatField still describes one source value conversion. The compile-time
 * backend may lower it either to a narrow opcode with only the required
 * immediates, or to GenericField plus the full
 * type/flags/fill/width/precision payload.
 * type chooses the runtime render semantics, pack chooses the packed argument
 * storage, and rule chooses the compile-time argument matching rule.
 * 每个 FormatField 仍然描述一个源级值转换。编译期后端可以把它降为只携带必要
 * 立即数的窄操作码，也可以降为 GenericField 加完整的
 * type/flags/fill/width/precision 载荷。type 决定运行期写出语义，pack 决定参数如何
 * 打包，rule 决定编译期实参匹配规则。
 */
struct FormatField
{
  FormatType type = FormatType::End;  ///< semantic render category / 语义写出类别
  FormatPackKind pack{};              ///< packed runtime storage kind / 运行期打包存储类别
  FormatArgumentRule rule =
      FormatArgumentRule::None;  ///< compile-time argument rule / 编译期实参匹配规则
  uint8_t flags = 0;             ///< FormatFlag bitset / 字段修饰位集合
  char fill = ' ';               ///< field fill character / 字段填充字符
  uint8_t width = 0;             ///< parsed field width / 已解析的字段宽度
  uint8_t precision = 0xFF;      ///< parsed precision, or unspecified / 已解析精度，或未指定
};

/**
 * @brief Returns the packed runtime byte count for one normalized argument type.
 * @brief 返回单个规范化参数类型在运行期打包后的字节数。
 *
 * This function only answers "how many bytes does one packed runtime argument of
 * this semantic type occupy", not "how many bytes does one byte-code record use".
 * 这个函数只回答“某种语义参数在运行期打包后占多少字节”，不回答“某条字节码记录占多少字节”。
 */
[[nodiscard]] constexpr size_t FormatArgumentBytes(FormatPackKind pack)
{
  switch (pack)
  {
    case FormatPackKind::I32:
      return sizeof(int32_t);
    case FormatPackKind::I64:
      return sizeof(int64_t);
    case FormatPackKind::U32:
      return sizeof(uint32_t);
    case FormatPackKind::U64:
      return sizeof(uint64_t);
    case FormatPackKind::Pointer:
      return sizeof(uintptr_t);
    case FormatPackKind::Character:
      return sizeof(char);
    case FormatPackKind::StringView:
      return sizeof(std::string_view);
    case FormatPackKind::F32:
      return sizeof(float);
    case FormatPackKind::F64:
      return sizeof(double);
    case FormatPackKind::LongDouble:
      return sizeof(long double);
  }

  return 0;
}

}  // namespace LibXR::Print
