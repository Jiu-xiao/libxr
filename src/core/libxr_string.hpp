#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "print/print_api.hpp"

namespace LibXR
{
namespace Detail
{

/**
 * @brief Type-list tag used only for exact argument-list comparison.
 * @brief 仅用于精确比较参数类型列表的标签类型。
 */
template <typename... Values>
struct RuntimeStringTypeList
{
};

/**
 * @brief Normalizes enum arguments to their underlying type for capacity probes.
 * @brief 为容量探测把枚举参数归一化到底层整数类型。
 */
template <typename T, bool IsEnum = std::is_enum_v<T>>
struct RuntimeStringNormalized
{
  using Type = T;
};

template <typename T>
struct RuntimeStringNormalized<T, true>
{
  using Type = std::underlying_type_t<T>;
};

/**
 * @brief 只在不可达模板分支中触发 `static_assert` 的延迟 false 值。
 * @brief Dependent false value used by `static_assert` in unreachable template
 *        branches.
 */
template <typename T>
static constexpr bool runtime_string_always_false = false;

inline constexpr size_t runtime_string_unbounded_capacity =
    std::numeric_limits<size_t>::max();
inline constexpr size_t runtime_string_max_field_width =
    std::numeric_limits<uint8_t>::max();

/**
 * @brief 保守估计一个格式化字段可能产生的最大长度。
 * @brief Conservatively bound one formatted field's maximum output length.
 *
 * 这里不读取格式参数值，也不重新实现完整 writer。编译后的 `ArgumentList()`
 * 已经把每个输出字段归一化为存储类别；再叠加协议中的 8-bit width /
 * precision 上限，就能得到稳定的保守上界。
 *
 * This does not inspect runtime values and does not reimplement the full writer.
 * The compiled `ArgumentList()` has already normalized each output field to a
 * storage kind; the protocol's 8-bit width / precision limits provide the rest
 * of a stable conservative upper bound.
 */
[[nodiscard]] constexpr size_t RuntimeStringFieldCapacity(Print::FormatPackKind pack)
{
  constexpr size_t width = runtime_string_max_field_width;
  constexpr size_t precision = runtime_string_max_field_width;

  switch (pack)
  {
    case Print::FormatPackKind::I32:
      return 1U + precision;
    case Print::FormatPackKind::I64:
      return 1U + precision;
    case Print::FormatPackKind::U32:
      return 2U + precision;
    case Print::FormatPackKind::U64:
      return 2U + precision;
    case Print::FormatPackKind::Pointer:
      return 2U + (precision > 2U * sizeof(uintptr_t) ? precision
                                                       : 2U * sizeof(uintptr_t));
    case Print::FormatPackKind::Character:
      return width;
    case Print::FormatPackKind::StringView:
      return runtime_string_unbounded_capacity;
    case Print::FormatPackKind::F32:
      return 1U + static_cast<size_t>(std::numeric_limits<float>::max_exponent10) +
             2U + 1U + precision;
    case Print::FormatPackKind::F64:
      return 1U + static_cast<size_t>(std::numeric_limits<double>::max_exponent10) +
             2U + 1U + precision;
    case Print::FormatPackKind::LongDouble:
      return 1U +
             static_cast<size_t>(std::numeric_limits<long double>::max_exponent10) +
             2U + 1U + precision;
  }

  return runtime_string_unbounded_capacity;
}

[[nodiscard]] constexpr bool RuntimeStringAddCapacity(size_t& total, size_t value)
{
  if (value == runtime_string_unbounded_capacity ||
      runtime_string_unbounded_capacity - total < value)
  {
    return false;
  }
  total += value;
  return true;
}

template <typename Built>
[[nodiscard]] consteval size_t RuntimeStringMaxFormattedSize(size_t source_size)
{
  size_t total = source_size;
  for (const auto& argument : Built::ArgumentList())
  {
    if (!RuntimeStringAddCapacity(total, RuntimeStringFieldCapacity(argument.pack)))
    {
      return runtime_string_unbounded_capacity;
    }
  }
  return total;
}

/**
 * @brief Compile-time traits for formatted retained-string arguments.
 * @brief 格式化保留字符串参数的编译期类型特征。
 *
 * The formatted variant must allocate before seeing future values, so only
 * bounded value-like argument types are accepted. Text-like runtime arguments
 * are intentionally rejected and should be handled by the plain concatenation
 * constructor instead.
 *
 * 格式化版本必须在看到未来值之前完成分配，因此这里只接受长度可界定的
 * 值类型参数。运行期文本参数会被拒绝，应改用普通拼接构造。
 */
template <typename T>
struct RuntimeStringArgumentTraits
{
  using Decayed = std::remove_cvref_t<T>;
  using Normalized = typename RuntimeStringNormalized<Decayed>::Type;

  static constexpr bool is_text =
      std::is_array_v<Decayed> || std::is_same_v<Decayed, char*> ||
      std::is_same_v<Decayed, const char*> || std::is_same_v<Decayed, std::string> ||
      std::is_same_v<Decayed, std::string_view>;

  static constexpr bool is_pointer =
      std::is_pointer_v<Decayed> &&
      !std::is_function_v<std::remove_pointer_t<Decayed>>;

  static constexpr bool has_static_capacity =
      !is_text && (std::is_same_v<Decayed, std::nullptr_t> || is_pointer ||
                   std::is_integral_v<Normalized> ||
                   std::is_floating_point_v<Normalized>);
};

/**
 * @brief Checks that a rewrite call uses exactly the argument types bound in
 *        `RuntimeStringView<Source, Args...>`.
 * @brief 检查刷新调用是否精确使用 `RuntimeStringView<Source, Args...>`
 *        绑定的参数类型。
 */
template <typename... Args>
struct RuntimeStringArgumentTypes
{
  template <typename... CallArgs>
  static constexpr bool matches =
      std::is_same_v<RuntimeStringTypeList<
                         typename RuntimeStringArgumentTraits<CallArgs>::Decayed...>,
                     RuntimeStringTypeList<Args...>>;
};

/**
 * @brief Normalized text part used by the copy/concatenation path.
 * @brief 拷贝/拼接路径使用的归一化文本片段。
 */
struct RuntimeStringTextPart
{
  std::string_view view;
  ErrorCode status = ErrorCode::OK;
};

/**
 * @brief Sink used by the second formatting pass to fill retained storage.
 * @brief 格式化第二遍使用的保留存储写入端。
 */
struct RuntimeStringBufferSink
{
  char* data = nullptr;
  size_t capacity = 0;
  size_t size = 0;

  [[nodiscard]] ErrorCode Write(std::string_view chunk)
  {
    if (capacity - size < chunk.size())
    {
      return ErrorCode::OUT_OF_RANGE;
    }
    if (!chunk.empty())
    {
      std::memcpy(data + size, chunk.data(), chunk.size());
      size += chunk.size();
    }
    return ErrorCode::OK;
  }
};

}  // namespace Detail

/**
 * @class RuntimeStringView
 * @brief 运行期构造、长期保留的 NUL 结尾字符串视图。
 * @brief Runtime-built retained NUL-terminated string view.
 *
 * 文本构造用于模块名、topic 名、后缀拼接等只构造一次的名字。
 * 格式化构造绑定字面量和参数类型，第一次 `Reformat()` / `Reprintf()`
 * 前从编译后的格式元数据计算最大容量，之后只复用该容量。对象析构时
 * 不释放已分配存储。
 *
 * Text construction is for retained names such as module names and topic
 * suffixes. Formatted instances bind the literal and argument types, compute
 * maximum capacity from compiled format metadata before the first rewrite, then
 * reuse that capacity. Allocated storage is intentionally not released by the
 * object destructor.
 */
template <Print::Text Source = "", typename... Args>
class RuntimeStringView
{
 public:
  static_assert((std::is_same_v<
                    Args, typename Detail::RuntimeStringArgumentTraits<Args>::Decayed> &&
                 ...),
                "LibXR::RuntimeStringView argument types must be unqualified "
                "value types");
  static_assert((Detail::RuntimeStringArgumentTraits<Args>::has_static_capacity && ...),
                "LibXR::RuntimeStringView formatted arguments cannot contain "
                "runtime strings; use RuntimeStringView<> for text concatenation");

  /// Constructs an empty valid view. / 构造一个空的有效视图。
  constexpr RuntimeStringView() = default;

  RuntimeStringView(const RuntimeStringView&) = delete;
  RuntimeStringView& operator=(const RuntimeStringView&) = delete;

  /// Move-constructs by taking the retained storage handle. / 移动构造并接管保留存储句柄。
  RuntimeStringView(RuntimeStringView&& other) noexcept
      : data_(other.data_),
        size_(other.size_),
        capacity_(other.capacity_),
        status_(other.status_)
  {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.status_ = ErrorCode::OK;
  }

  /// Move-assigns by taking the retained storage handle. / 移动赋值并接管保留存储句柄。
  RuntimeStringView& operator=(RuntimeStringView&& other) noexcept
  {
    if (this != &other)
    {
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      status_ = other.status_;
      other.data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
      other.status_ = ErrorCode::OK;
    }
    return *this;
  }

  /// Copies text into retained storage. / 拷贝文本到保留存储。
  explicit RuntimeStringView(std::string_view text)
    requires(Source.Size() == 0 && sizeof...(Args) == 0)
  {
    static_cast<void>(AssignCopy(text));
  }

  /// Copies a C string into retained storage. / 拷贝 C 字符串到保留存储。
  explicit RuntimeStringView(const char* text)
    requires(Source.Size() == 0 && sizeof...(Args) == 0)
  {
    static_cast<void>(AssignCopy(text));
  }

  /// Concatenates text parts into retained storage. / 拼接多个文本片段到保留存储。
  template <typename First, typename Second, typename... Rest>
  explicit RuntimeStringView(First&& first, Second&& second, Rest&&... rest)
    requires(Source.Size() == 0 && sizeof...(Args) == 0)
  {
    static_cast<void>(AssignConcat(std::forward<First>(first),
                                   std::forward<Second>(second),
                                   std::forward<Rest>(rest)...));
  }

  /**
   * @brief 使用绑定的 brace-style 格式重写当前内容。
   * @brief Rewrite current content with the bound brace-style format.
   */
  template <typename... CallArgs>
  [[nodiscard]] ErrorCode Reformat(CallArgs&&... args)
    requires((Source.Size() != 0 || sizeof...(Args) != 0) &&
             Detail::RuntimeStringArgumentTypes<Args...>::template matches<CallArgs...>)
  {
    static_assert(LibXR::Format<Source>::template Matches<Args...>(),
                  "LibXR::RuntimeStringView format argument types do not match "
                  "the format");
    using Built = typename LibXR::Format<Source>::template Compiled<Args...>;
    static constexpr size_t max_size =
        Detail::RuntimeStringMaxFormattedSize<Built>(Source.Size());
    static_assert(max_size != Detail::runtime_string_unbounded_capacity,
                  "LibXR::RuntimeStringView cannot retain unbounded formatted "
                  "runtime strings");

    return AssignFormatted(max_size, [&](auto& sink) {
      return Print::FormatTo<Source>(sink, std::forward<CallArgs>(args)...);
    });
  }

  /**
   * @brief 使用绑定的 printf-style 格式重写当前内容。
   * @brief Rewrite current content with the bound printf-style format.
   */
  template <typename... CallArgs>
  [[nodiscard]] ErrorCode Reprintf(CallArgs&&... args)
    requires((Source.Size() != 0 || sizeof...(Args) != 0) &&
             Detail::RuntimeStringArgumentTypes<Args...>::template matches<CallArgs...>)
  {
    static_assert(Print::Printf::Matches<Source, Args...>(),
                  "LibXR::RuntimeStringView printf argument types do not match "
                  "the format");
    using Built = Print::Printf::Compiled<Source>;
    static constexpr size_t max_size =
        Detail::RuntimeStringMaxFormattedSize<Built>(Source.Size());
    static_assert(max_size != Detail::runtime_string_unbounded_capacity,
                  "LibXR::RuntimeStringView cannot retain unbounded formatted "
                  "runtime strings");

    return AssignFormatted(max_size, [&](auto& sink) {
      return Print::PrintfTo<Source>(sink, std::forward<CallArgs>(args)...);
    });
  }

  /// Returns the current read-only view. / 返回当前只读视图。
  [[nodiscard]] constexpr std::string_view View() const
  {
    return data_ == nullptr ? std::string_view{} : std::string_view(data_, size_);
  }

  /// Returns a NUL-terminated C string. / 返回 NUL 结尾 C 字符串。
  [[nodiscard]] const char* CStr() const { return data_ == nullptr ? "" : data_; }
  /// Returns the text size excluding the trailing NUL. / 返回不含结尾 NUL 的文本长度。
  [[nodiscard]] constexpr size_t Size() const { return size_; }
  /// Returns whether the latest operation succeeded. / 返回最近一次操作是否成功。
  [[nodiscard]] constexpr bool Ok() const { return status_ == ErrorCode::OK; }
  /// Returns the latest operation status. / 返回最近一次操作状态。
  [[nodiscard]] constexpr ErrorCode Status() const { return status_; }
  /// Converts to a read-only string view. / 转换为只读字符串视图。
  [[nodiscard]] constexpr operator std::string_view() const { return View(); }
  /// Converts to a NUL-terminated C string. / 转换为 NUL 结尾 C 字符串。
  [[nodiscard]] operator const char*() const { return CStr(); }

 private:
  /**
   * @brief 记录最近一次失败，并在已有存储上保留一个有效的空 C 字符串。
   * @brief Record the latest failure and keep a valid empty C string when
   *        storage already exists.
   */
  [[nodiscard]] ErrorCode SetFailure(ErrorCode status)
  {
    if (data_ != nullptr)
    {
      data_[0] = '\0';
    }
    size_ = 0;
    status_ = status;
    return status_;
  }

  /**
   * @brief 为首个非空结果分配保留存储，已有存储不会再次扩容。
   * @brief Allocate retained storage for the first non-empty result; existing
   *        storage is never grown.
   *
   * RuntimeStringView 的容量策略是一次分配后复用。格式化路径会先从编译格式
   * 元数据计算最大容量，因此已有存储不足表示设计边界被突破，不能在这里静默
   * new 第二块内存。
   *
   * RuntimeStringView reuses one allocation. The formatted path computes maximum
   * capacity from compiled format metadata first, so an oversized later write is
   * a boundary error rather than a reason to allocate again.
   */
  [[nodiscard]] ErrorCode EnsureCapacity(size_t payload_size)
  {
    if (payload_size == std::numeric_limits<size_t>::max())
    {
      return ErrorCode::OUT_OF_RANGE;
    }
    if (payload_size <= capacity_ && data_ != nullptr)
    {
      return ErrorCode::OK;
    }
    if (data_ != nullptr)
    {
      return ErrorCode::OUT_OF_RANGE;
    }
    if (payload_size == 0)
    {
      return ErrorCode::OK;
    }

    data_ = new (std::nothrow) char[payload_size + 1U];
    if (data_ == nullptr)
    {
      return ErrorCode::NO_MEM;
    }
    capacity_ = payload_size;
    return ErrorCode::OK;
  }

  /**
   * @brief 构造期文本拷贝入口；空字符串保持零分配。
   * @brief Construction-time text copy entry; empty text remains allocation-free.
   */
  [[nodiscard]] ErrorCode AssignCopy(std::string_view text)
  {
    ErrorCode status = EnsureCapacity(text.size());
    if (status != ErrorCode::OK)
    {
      return SetFailure(status);
    }
    if (!text.empty())
    {
      std::memcpy(data_, text.data(), text.size());
    }
    if (data_ != nullptr)
    {
      data_[text.size()] = '\0';
    }
    size_ = text.size();
    status_ = ErrorCode::OK;
    return status_;
  }

  /**
   * @brief C 字符串入口负责空指针检查，再进入统一的文本拷贝路径。
   * @brief C-string entry checks null pointers before using the common text copy
   *        path.
   */
  [[nodiscard]] ErrorCode AssignCopy(const char* text)
  {
    return text == nullptr ? SetFailure(ErrorCode::PTR_NULL)
                           : AssignCopy(std::string_view(text, std::strlen(text)));
  }

  /**
   * @brief 拼接构造的两遍流程：先校验并统计所有片段，再一次性写入。
   * @brief Two-pass concatenation constructor: validate/count all parts before
   *        one retained write.
   *
   * 第一遍会传播空指针或非法片段错误，避免已经写了一半才发现参数不可用。
   * The first pass propagates null-pointer or invalid-part errors before any
   * partial payload is written.
   */
  template <typename... Parts>
  [[nodiscard]] ErrorCode AssignConcat(Parts&&... parts)
  {
    size_t total_size = 0;
    ErrorCode status = ErrorCode::OK;

    auto count = [&](auto&& part)
    {
      if (status != ErrorCode::OK)
      {
        return;
      }

      Detail::RuntimeStringTextPart text =
          ToTextPart(std::forward<decltype(part)>(part));
      if (text.status != ErrorCode::OK)
      {
        status = text.status;
      }
      else if (std::numeric_limits<size_t>::max() - total_size < text.view.size())
      {
        status = ErrorCode::OUT_OF_RANGE;
      }
      else
      {
        total_size += text.view.size();
      }
    };
    (count(std::forward<Parts>(parts)), ...);

    if (status != ErrorCode::OK)
    {
      return SetFailure(status);
    }
    status = EnsureCapacity(total_size);
    if (status != ErrorCode::OK)
    {
      return SetFailure(status);
    }

    size_t offset = 0;
    auto copy = [&](auto&& part)
    {
      Detail::RuntimeStringTextPart text =
          ToTextPart(std::forward<decltype(part)>(part));
      if (!text.view.empty())
      {
        std::memcpy(data_ + offset, text.view.data(), text.view.size());
        offset += text.view.size();
      }
    };
    (copy(std::forward<Parts>(parts)), ...);

    if (data_ != nullptr)
    {
      data_[total_size] = '\0';
    }
    size_ = total_size;
    status_ = ErrorCode::OK;
    return status_;
  }

  /**
   * @brief 格式化重写入口；首次调用按编译期上界分配，后续调用只覆盖已有存储。
   * @brief Formatted rewrite entry; the first call allocates the compile-time
   *        upper bound, later calls only overwrite retained storage.
   *
   * `max_size` 来自已编译格式元数据和字段类型上界；`write` 使用本次真实参数写入
   * buffer sink。
   * `max_size` comes from compiled format metadata and per-field type bounds;
   * `write` uses the current call arguments with the buffer sink.
   */
  template <typename WriteFn>
  [[nodiscard]] ErrorCode AssignFormatted(size_t max_size, WriteFn&& write)
  {
    if (data_ == nullptr)
    {
      ErrorCode status = EnsureCapacity(max_size);
      if (status != ErrorCode::OK)
      {
        return SetFailure(status);
      }
    }

    Detail::RuntimeStringBufferSink sink{.data = data_, .capacity = capacity_};
    ErrorCode status = write(sink);
    if (status != ErrorCode::OK)
    {
      return SetFailure(status);
    }
    if (data_ != nullptr)
    {
      data_[sink.size] = '\0';
    }
    size_ = sink.size;
    status_ = ErrorCode::OK;
    return status_;
  }

  /**
   * @brief 将 `string_view` 归一化为拼接片段，不接管其所有权。
   * @brief Normalize `string_view` as a concatenation part without taking
   *        ownership.
   */
  [[nodiscard]] static constexpr Detail::RuntimeStringTextPart ToTextPart(
      std::string_view text)
  {
    return {.view = text};
  }

  /**
   * @brief 将 `std::string` 归一化为只读片段，真实拷贝由调用方统一完成。
   * @brief Normalize `std::string` as a read-only part; the caller performs the
   *        actual copy.
   */
  [[nodiscard]] static Detail::RuntimeStringTextPart ToTextPart(
      const std::string& text)
  {
    return {.view = std::string_view(text.data(), text.size())};
  }

  /**
   * @brief 将 C 字符串归一化为拼接片段，并把空指针转换为 `PTR_NULL`。
   * @brief Normalize a C string as a concatenation part and convert null pointers
   *        to `PTR_NULL`.
   */
  [[nodiscard]] static Detail::RuntimeStringTextPart ToTextPart(const char* text)
  {
    return text == nullptr
               ? Detail::RuntimeStringTextPart{.status = ErrorCode::PTR_NULL}
               : Detail::RuntimeStringTextPart{
                     .view = std::string_view(text, std::strlen(text))};
  }

  /**
   * @brief 可变 C 字符串与 const C 字符串使用同一套长度和空指针规则。
   * @brief Mutable C strings share the same length and null-pointer rules as
   *        const C strings.
   */
  [[nodiscard]] static Detail::RuntimeStringTextPart ToTextPart(char* text)
  {
    return ToTextPart(static_cast<const char*>(text));
  }

  /**
   * @brief 字符串字面量直接使用数组长度，避免运行期 `strlen()`。
   * @brief String literals use their array length directly and avoid runtime
   *        `strlen()`.
   */
  template <size_t N>
  [[nodiscard]] static constexpr Detail::RuntimeStringTextPart ToTextPart(
      const char (&text)[N])
  {
    return {.view = std::string_view(text, N > 0 ? N - 1U : 0U)};
  }

  /**
   * @brief 允许已保留字符串参与拼接，同时传播其失败状态。
   * @brief Allow another retained string to join concatenation while propagating
   *        its failure status.
   */
  template <Print::Text OtherSource, typename... OtherArgs>
  [[nodiscard]] static constexpr Detail::RuntimeStringTextPart ToTextPart(
      const RuntimeStringView<OtherSource, OtherArgs...>& text)
  {
    return text.Ok() ? Detail::RuntimeStringTextPart{.view = text.View()}
                     : Detail::RuntimeStringTextPart{.status = text.Status()};
  }

  /**
   * @brief 普通拼接只接受文本片段，数值格式化必须显式走 `Reformat()` 或 `Reprintf()`。
   * @brief Plain concatenation accepts text parts only; numeric formatting must
   *        go through `Reformat()` or `Reprintf()`.
   */
  template <typename T>
  [[nodiscard]] static constexpr Detail::RuntimeStringTextPart ToTextPart(const T&)
  {
    static_assert(Detail::runtime_string_always_false<T>,
                  "RuntimeStringView constructor only accepts text-like parts. "
                  "Use Reformat() or Reprintf() for numeric formatting.");
    return {.status = ErrorCode::ARG_ERR};
  }

  /// 长期保留的 NUL 结尾存储；对象析构时不释放。 / Retained NUL-terminated storage; not released by the destructor.
  char* data_ = nullptr;
  /// 不含结尾 NUL 的当前可见文本长度。 / Current visible payload size excluding the trailing NUL.
  size_t size_ = 0;
  /// 不含结尾 NUL 的已探测/分配容量。 / Probed/allocated payload capacity excluding the trailing NUL.
  size_t capacity_ = 0;
  /// 最近一次构造或重写状态。 / Status of the latest construction or rewrite.
  ErrorCode status_ = ErrorCode::OK;
};

/// 单参数文本构造推导为普通保留字符串。 / Single text argument deduces a plain retained string.
RuntimeStringView(std::string_view) -> RuntimeStringView<>;
/// C 字符串构造推导为普通保留字符串。 / C-string construction deduces a plain retained string.
RuntimeStringView(const char*) -> RuntimeStringView<>;

/// 多片段构造推导为普通拼接字符串。 / Multi-part construction deduces a plain concatenated string.
template <typename First, typename Second, typename... Rest>
RuntimeStringView(First&&, Second&&, Rest&&...) -> RuntimeStringView<>;

}  // namespace LibXR
