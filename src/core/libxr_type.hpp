#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @brief 可写原始数据视图 / Mutable raw data view
 *
 * @note 该类型不拥有数据，仅保存起始地址和字节数。调用方需要保证被引用对象在视图使用期间
 *       保持有效且可写。
 *       / This type does not own the data. It only stores the start address and
 *       byte count. The caller must keep the referenced object alive and writable
 *       while the view is in use.
 */
class RawData
{
 public:
  /**
   * @brief 使用指定地址和大小构造 `RawData` 对象。
   *        Constructs a `RawData` object with the specified address and size.
   *
   * @param addr 数据的起始地址。
   *             The starting address of the data.
   * @param size 数据的大小（字节）。
   *             The size of the data (in bytes).
   */
  RawData(void* addr, size_t size) : addr_(addr), size_(size) {}

  /**
   * @brief 默认构造函数，初始化为空数据。
   *        Default constructor initializing to empty data.
   */
  RawData() = default;

  /**
   * @brief 从可写对象构造视图 / Construct a view from a writable object
   * @tparam DataType 对象类型 / Object type
   * @param data 被引用对象 / Referenced object
   */
  template <typename DataType>
    requires(!std::is_const_v<DataType> &&
             !std::is_same_v<std::remove_cvref_t<DataType>, RawData>)
  RawData(DataType& data) : addr_(&data), size_(sizeof(DataType))
  {
  }

  /**
   * @brief 拷贝构造函数。
   *        Copy constructor.
   *
   * @param data 另一个 `RawData` 对象。
   *             Another `RawData` object.
   */
  RawData(const RawData& data) = default;

  /**
   * @brief 从 `char *` 指针构造 `RawData`，数据大小为字符串长度（不含 `\0`）。
   *        Constructs `RawData` from a `char *` pointer,
   *        with size set to the string length (excluding `\0`).
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  RawData(char* data) : addr_(data), size_(data != nullptr ? std::strlen(data) : 0) {}

  /**
   * @brief 从可写字符数组构造文本视图 / Construct a text view from a writable char array
   * @tparam N 数组长度 / Array length
   * @param data 被引用字符数组 / Referenced char array
   *
   * @note 返回长度为 `N - 1`，默认忽略结尾的 `\\0`。
   *       / The resulting size is `N - 1`, so the trailing `\\0` is ignored by
   *       default.
   */
  template <size_t N>
  RawData(char (&data)[N]) : addr_(data), size_(N - 1)
  {
  }

  /**
   * @brief 从可写字符串构造文本视图 / Construct a text view from a writable string
   * @param data 被引用字符串 / Referenced string
   */
  explicit RawData(std::string& data)
      : addr_(data.empty() ? nullptr : data.data()), size_(data.size())
  {
  }

  /**
   * @brief 赋值运算符重载。
   *        Overloaded assignment operator.
   *
   * @param data 另一个 `RawData` 对象。
   *             Another `RawData` object.
   * @return 返回赋值后的 `RawData` 对象引用。
   *         Returns a reference to the assigned `RawData` object.
   */
  RawData& operator=(const RawData& data) = default;

  void* addr_ = nullptr;   ///< 数据起始地址 / Data start address
  size_t size_ = 0;        ///< 数据字节数 / Data size in bytes
};

/**
 * @brief 只读原始数据视图 / Immutable raw data view
 *
 * @note 该类型不拥有数据，仅保存起始地址和字节数。调用方需要保证被引用对象在视图使用期间
 *       保持有效。
 *       / This type does not own the data. It only stores the start address and
 *       byte count. The caller must keep the referenced object alive while the
 *       view is in use.
 */
class ConstRawData
{
 public:
  /**
   * @brief 使用指定地址和大小构造 `ConstRawData` 对象。
   *        Constructs a `ConstRawData` object with the specified address and size.
   *
   * @param addr 数据的起始地址。
   *             The starting address of the data.
   * @param size 数据的大小（字节）。
   *             The size of the data (in bytes).
   */
  ConstRawData(const void* addr, size_t size) : addr_(addr), size_(size) {}

  /**
   * @brief 默认构造函数，初始化为空数据。
   *        Default constructor initializing to empty data.
   */
  ConstRawData() = default;

  /**
   * @brief 从任意对象构造只读视图 / Construct a read-only view from any object
   * @tparam DataType 对象类型 / Object type
   * @param data 被引用对象 / Referenced object
   */
  template <typename DataType>
  ConstRawData(const DataType& data)
      : addr_(reinterpret_cast<const DataType*>(&data)), size_(sizeof(DataType))
  {
  }

  /**
   * @brief 拷贝构造函数。
   *        Copy constructor.
   *
   * @param data 另一个 `ConstRawData` 对象。
   *             Another `ConstRawData` object.
   */
  ConstRawData(const ConstRawData& data) = default;

  /**
   * @brief 从 `RawData` 构造 `ConstRawData`，数据地址和大小保持不变。
   *        Constructs `ConstRawData` from `RawData`,
   *        keeping the same data address and size.
   *
   * @param data `RawData` 对象。
   *             A `RawData` object.
   */
  ConstRawData(const RawData& data) : addr_(data.addr_), size_(data.size_) {}

  /**
   * @brief 从 `char *` 指针构造 `ConstRawData`，数据大小为字符串长度（不含 `\0`）。
   *        Constructs `ConstRawData` from a `char *` pointer,
   *        with size set to the string length (excluding `\0`).
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  ConstRawData(char* data) : addr_(data), size_(data != nullptr ? std::strlen(data) : 0) {}

  /**
   * @brief 从 `char *` 指针构造 `ConstRawData`（常量版本）。
   *        Constructs `ConstRawData` from a `const char *` pointer.
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  ConstRawData(const char* data)
      : addr_(data), size_(data != nullptr ? std::strlen(data) : 0)
  {
  }

  /**
   * @brief 从只读字符串构造文本视图 / Construct a text view from a read-only string
   * @param data 被引用字符串 / Referenced string
   */
  explicit ConstRawData(const std::string& data)
      : addr_(data.empty() ? nullptr : data.data()), size_(data.size())
  {
  }

  /**
   * @brief 从字符串视图构造文本视图 / Construct a text view from a string view
   * @param data 被引用字符串视图 / Referenced string view
   */
  explicit ConstRawData(std::string_view data)
      : addr_(data.empty() ? nullptr : data.data()), size_(data.size())
  {
  }

  /**
   * @brief 从字符数组构造 `ConstRawData`，数据大小为数组长度减 1（不含 `\0`）。
   *        Constructs `ConstRawData` from a character array,
   *        with size set to array length minus 1 (excluding `\0`).
   *
   * @tparam N 数组大小。
   *           The array size.
   * @param data 需要存储的字符数组。
   *             The character array to be stored.
   */
  template <size_t N>
  ConstRawData(const char (&data)[N])
      : addr_(reinterpret_cast<const void*>(data)), size_(N - 1)
  {
  }

  /**
   * @brief 赋值运算符重载。
   *        Overloaded assignment operator.
   *
   * @param data 另一个 `ConstRawData` 对象。
   *             Another `ConstRawData` object.
   * @return 返回赋值后的 `ConstRawData` 对象引用。
   *         Returns a reference to the assigned `ConstRawData` object.
   */
  ConstRawData& operator=(const ConstRawData& data) = default;

  const void* addr_ = nullptr;  ///< 数据起始地址 / Data start address
  size_t size_ = 0;             ///< 数据字节数 / Data size in bytes
};

/**
 * @brief 类型标识符生成器 / RTTI-free type identifier generator
 */
class TypeID
{
 public:
  using ID = const void*;

  /**
   * @brief 获取类型的唯一标识符 / Get a unique identifier for type `T`
   * @tparam T 目标类型 / Target type
   * @return 类型唯一标识符指针 / Unique type identifier pointer
   */
  template <typename T>
  static ID GetID()
  {
    static char id;  // NOLINT
    return &id;
  }
};

}  // namespace LibXR
