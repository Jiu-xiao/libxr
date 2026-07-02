#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

/**
 * @brief 供直接包含头文件的用户使用的打印功能默认值。 / Print feature defaults for direct header consumers.
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

#ifndef LIBXR_PRINT_FLOAT_MAX_PRECISION
#define LIBXR_PRINT_FLOAT_MAX_PRECISION 32
#endif

#ifndef LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS
#define LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS 32
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
/**
 * @brief 启用有符号与无符号十进制整数转换 / Enables signed and unsigned decimal integer conversions.
 */
inline constexpr bool enable_integer = LIBXR_PRINT_ENABLE_INTEGER;
/**
 * @brief 启用字符与字符串转换 / Enables character and string conversions.
 */
inline constexpr bool enable_text = LIBXR_PRINT_ENABLE_TEXT;
/**
 * @brief 启用指针转换 / Enables pointer conversions.
 */
inline constexpr bool enable_pointer = LIBXR_PRINT_ENABLE_POINTER;
/**
 * @brief 所有浮点转换的总开关 / Master switch for all floating-point conversions.
 */
inline constexpr bool enable_float = LIBXR_PRINT_ENABLE_FLOAT;

/**
 * @brief 在整数功能开启时启用二进制、八进制和十六进制转换 / Enables binary, octal, and hexadecimal integer conversions when integers are enabled.
 */
inline constexpr bool enable_integer_base8_16 =
    enable_integer && LIBXR_PRINT_INTEGER_ENABLE_BASE8_16;
/**
 * @brief 在整数功能开启时启用 64 位整数格式化族 / Enables 64-bit integer formatting families when integers are enabled.
 */
inline constexpr bool enable_integer_64bit =
    enable_integer && LIBXR_PRINT_INTEGER_ENABLE_64BIT;

/**
 * @brief 在浮点功能开启时启用 %f / %F / Enables %f / %F when floating-point support is enabled.
 */
inline constexpr bool enable_float_fixed =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_FIXED;
/**
 * @brief 在浮点功能开启时启用基于 double 的默认浮点格式化 / Enables double-backed default float formatting when floating-point support is enabled.
 */
inline constexpr bool enable_float_double =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_DOUBLE;
/**
 * @brief 在浮点功能开启时启用 %e / %E / Enables %e / %E when floating-point support is enabled.
 */
inline constexpr bool enable_float_scientific =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC;
/**
 * @brief 在浮点功能开启时启用 %g / %G / Enables %g / %G when floating-point support is enabled.
 */
inline constexpr bool enable_float_general =
    enable_float && LIBXR_PRINT_FLOAT_ENABLE_GENERAL;
/**
 * @brief 在浮点功能开启时启用 L 长度修饰 / Enables the L floating-point length modifier when floating-point support is enabled.
 */
inline constexpr bool enable_float_long_double =
    enable_float_double && LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE;
static_assert(LIBXR_PRINT_FLOAT_MAX_PRECISION <=
                  static_cast<int>(std::numeric_limits<uint8_t>::max() - 1),
              "LibXR::Print: LIBXR_PRINT_FLOAT_MAX_PRECISION must fit in uint8_t "
              "and stay below the unspecified-precision sentinel");
static_assert(LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS > 0,
              "LibXR::Print: LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS must be positive");
/**
 * @brief 前端接受的浮点显式精度上限 / Maximum explicit float precision accepted by the frontend.
 */
inline constexpr uint8_t max_float_precision =
    static_cast<uint8_t>(LIBXR_PRINT_FLOAT_MAX_PRECISION);
/**
 * @brief 定点浮点格式接受的整数位数上限 / Maximum integer-digit count accepted by fixed float formatting.
 */
inline constexpr size_t max_float_integer_digits =
    static_cast<size_t>(LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS);

/**
 * @brief 启用常量字段宽度解析 / Enables constant field width parsing.
 */
inline constexpr bool enable_width = LIBXR_PRINT_ENABLE_WIDTH;
/**
 * @brief 启用常量精度解析 / Enables constant precision parsing.
 */
inline constexpr bool enable_precision = LIBXR_PRINT_ENABLE_PRECISION;
/**
 * @brief 启用备用格式语法，例如用于整数前缀和浮点保留小数点的 # / Enables alternate-form syntax such as # for integer prefixes and float decimal-point retention.
 */
inline constexpr bool enable_alternate = LIBXR_PRINT_ENABLE_ALTERNATE;
/**
 * @brief 启用源级显式参数索引，例如 printf 的 n$ 和 format 的 {1} / Enables source-level explicit argument indexing such as printf n$ and format {1}.
 */
inline constexpr bool enable_explicit_argument_indexing =
    LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING;
}  // namespace LibXR::Print::Config

namespace LibXR::Print
{
/**
 * @brief 编译期解析层与运行期写出层之间共用的打印格式协议。 / Shared print-format protocol used between compile-time parsing and runtime writing.
 *
 * The frontends first decide:
 * - which C++ argument types are allowed
 * - how each accepted argument is packed for runtime use
 * - which text-writing path the runtime writer should take later
 *
 * The runtime writer then reads that protocol directly instead of reparsing the
 * original format string.
 * 前端会先决定：
 * - 允许哪些 C++ 实参类型
 * - 每个实参在运行期该怎样打包
 * - 运行期最终该走哪条文本写出路径
 *
 * 运行期 writer 随后直接读取这套协议，不再重新解析原始格式串。
 */
/**
 * @brief 每个运行期参数附带的编译期匹配规则。 / Compile-time argument matching rules attached to each runtime argument.
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
 * @brief 保存在值记录字段描述字节中的位标志。 / Bit flags stored in a value record's field-spec byte.
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
 * @brief Writer 消费的运行期字节码操作。 / Runtime bytecode operations consumed by Writer.
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
  Signed32Dec = 0x12,      ///< raw int32_t decimal output / 直接输出 int32_t 十进制
  U32Binary = 0x13,        ///< raw uint32_t binary output / 直接输出 uint32_t 二进制
  U32Octal = 0x14,         ///< raw uint32_t octal output / 直接输出 uint32_t 八进制
  U32HexLower = 0x15,      ///< raw uint32_t lowercase hex output / 直接输出 uint32_t 小写十六进制
  U32HexUpper = 0x16,      ///< raw uint32_t uppercase hex output / 直接输出 uint32_t 大写十六进制
  StringRaw = 0x20,        ///< raw string_view output / 直接输出 string_view
  CharacterRaw = 0x21,     ///< raw character output / 直接输出字符
  GenericField = 0xF0,     ///< wide fallback payload: type, flags, fill, width, precision / 宽回退载荷：type、flags、fill、width、precision
  End = 0xFF,              ///< terminates the compiled record stream / 结束整条编译记录流
};

/**
 * @brief 编码后的单个操作码后面跟随的载荷字节数 / Number of payload bytes that follow one encoded opcode.
 */
[[nodiscard]] constexpr size_t FormatOpPayloadBytes(FormatOp op)
{
  switch (op)
  {
    case FormatOp::TextInline:
      return 0;
    case FormatOp::TextRef:
      return 2 * sizeof(uint16_t);
    case FormatOp::U32ZeroPadWidth:
      return 1;
    case FormatOp::GenericField:
      return 5;
    case FormatOp::TextSpace:
    case FormatOp::U32Dec:
    case FormatOp::Signed32Dec:
    case FormatOp::U32Binary:
    case FormatOp::U32Octal:
    case FormatOp::U32HexLower:
    case FormatOp::U32HexUpper:
    case FormatOp::StringRaw:
    case FormatOp::CharacterRaw:
    case FormatOp::End:
      return 0;
  }

  // Unreachable for valid streams: every FormatOp value is enumerated above.
  // A corrupt/unknown opcode falls through here and yields 0 payload bytes.
  return 0;
}

/**
 * @brief 编译期分析和运行期分发共用的语义处理类别。 / Semantic handler categories used by compile-time analysis and runtime dispatch.
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
 * @brief 运行期参数的打包存储类别。 / Packed storage categories for runtime arguments.
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
 * @brief 编译期选出的粗粒度运行期执行器配置。 / Coarse runtime executor profiles selected at compile time.
 *
 * The low bits describe which narrow fast-path families appear in the bytecode.
 * Generic marks that at least one field still requires the old wide fallback.
 * 低位描述当前字节码里出现了哪些窄快路径族；Generic 表示至少存在一个字段仍需走旧的宽回退路径。
 */
enum class FormatProfile : uint8_t
{
  None = 0,            ///< text-only stream / 只有文本记录的流
  NarrowInt = 1U << 0, ///< signed/unsigned narrow integer fast path family / 有符号/无符号窄整数快路径族
  TextArg = 1U << 1,   ///< raw text argument fast path / 原始文本参数快路径
  F32Fixed = 1U << 2,  ///< fixed float fast path / 定点 float 快路径
  F64Fixed = 1U << 3,  ///< fixed double fast path / 定点 double 快路径
  Generic = 1U << 7,   ///< at least one field uses generic fallback / 至少有一个字段使用通用回退
};

/**
 * @brief 合并两组 profile 位 / Combine two profile bit sets
 * @param left 左侧 profile 位集合 / Left profile bit set
 * @param right 右侧 profile 位集合 / Right profile bit set
 * @return 合并后的 profile 位集合 / Returns the merged profile bit set
 */
[[nodiscard]] constexpr FormatProfile operator|(FormatProfile left, FormatProfile right)
{
  return static_cast<FormatProfile>(static_cast<uint8_t>(left) |
                                    static_cast<uint8_t>(right));
}

/**
 * @brief 将一组 profile 位累加到另一组 / Accumulate one profile bit set into another
 * @param left 累加目标 / Accumulation target
 * @param right 待并入的 profile 位 / Profile bits to merge in
 * @return 并入 `right` 后的 `left` / Returns `left` after merging `right`
 */
constexpr FormatProfile& operator|=(FormatProfile& left, FormatProfile right)
{
  left = left | right;
  return left;
}

/**
 * @brief 判断某个 profile 位是否存在 / Test whether one profile bit is present
 * @param profile 待检查的 profile 位集合 / Profile bit set to inspect
 * @param bit 待测试的单个 profile 位 / Single profile bit to test
 * @return `profile` 中存在 `bit` 时返回 `true`，否则返回 `false` / Returns `true` when `bit` is present in `profile`, otherwise `false`
 */
[[nodiscard]] constexpr bool HasProfile(FormatProfile profile, FormatProfile bit)
{
  return (static_cast<uint8_t>(profile) & static_cast<uint8_t>(bit)) != 0;
}

/**
 * @brief Writer 消费的编译格式运行期协议。 / Compiled-format runtime contract consumed by Writer.
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
 * @brief 每个参数对应的元信息，同时用于编译期类型检查和运行期打包。 / Per-argument metadata used both for compile-time type checking and runtime packing.
 *
 * `pack` says how the runtime argument blob stores this argument.
 * `rule` says which C++ types are accepted at compile time.
 * `pack` 描述运行期参数字节块里如何存这个参数。
 * `rule` 描述编译期允许哪些 C++ 类型匹配这个参数。
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
 * @brief 一条已经决定完毕、运行期 writer 知道如何打印的值字段。 / One fully-decided value field that the runtime writer knows how to print.
 *
 * It says:
 * - which printing path to use
 * - how the argument was packed
 * - which compile-time rule accepted that argument
 * - which width/fill/precision flags still matter at runtime
 * 它描述的是：
 * - 该走哪条打印路径
 * - 参数当时是怎么打包的
 * - 编译期是按哪条规则接受这个参数的
 * - 以及运行期仍然需要的宽度/填充/精度信息
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
 * @brief 返回一个运行期已打包参数会占多少字节。 / Returns how many bytes one packed runtime argument occupies.
 *
 * This answers "how big is one packed argument", not "how long is one opcode".
 * 这里回答的是“一个参数打包后有多大”，不是“某条操作码记录有多长”。
 * @param pack Packed storage kind to inspect. / 待检查的打包存储类型。
 * @return Returns the byte size of one packed argument in this storage kind. /
 *         返回该存储类型下单个已打包参数的字节数。
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
