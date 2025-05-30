#pragma once

#include <cstring>
#include <string>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @brief 原始数据封装类。
 *        A class for encapsulating raw data.
 *
 * 该类提供了一种通用的数据存储方式，可以存储指针和数据大小，
 * 以支持各种数据类型的传输和存储。
 * This class provides a generic way to store pointers and data sizes
 * to support the transmission and storage of various data types.
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
  RawData(void *addr, size_t size) : addr_(addr), size_(size) {}

  /**
   * @brief 默认构造函数，初始化为空数据。
   *        Default constructor initializing to empty data.
   */
  RawData() : addr_(nullptr), size_(0) {}

  /**
   * @brief 使用任意数据类型构造 `RawData`，数据地址指向该对象，大小为该类型的字节大小。
   *        Constructs `RawData` using any data type, pointing to the object and
   *        setting the size to the type's byte size.
   *
   * @tparam DataType 数据类型。
   *                  The data type.
   * @param data 需要存储的具体数据。
   *             The actual data to be stored.
   */
  template <typename DataType>
  RawData(const DataType &data)
      : addr_(const_cast<DataType *>(&data)), size_(sizeof(DataType))
  {
  }

  /**
   * @brief 拷贝构造函数。
   *        Copy constructor.
   *
   * @param data 另一个 `RawData` 对象。
   *             Another `RawData` object.
   */
  RawData(const RawData &data) = default;

  /**
   * @brief 从 `char *` 指针构造 `RawData`，数据大小为字符串长度（不含 `\0`）。
   *        Constructs `RawData` from a `char *` pointer,
   *        with size set to the string length (excluding `\0`).
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  RawData(char *data) : addr_(data), size_(data ? strlen(data) : 0) {}

  /**
   * @brief 从字符数组构造 `RawData`，数据大小为数组长度减 1（不含 `\0`）。
   *        Constructs `RawData` from a character array,
   *        with size set to array length minus 1 (excluding `\0`).
   *
   * @tparam N 数组大小。
   *           The array size.
   * @param data 需要存储的字符数组。
   *             The character array to be stored.
   */
  template <size_t N>
  RawData(const char (&data)[N]) : addr_(&data), size_(N - 1)
  {
  }

  /**
   * @brief 从 `std::string` 构造 `RawData`，数据地址指向字符串内容，大小为字符串长度。
   *        Constructs `RawData` from `std::string`,
   *        with data pointing to the string content and size set to its length.
   *
   * @param data `std::string` 类型数据。
   *             A `std::string` object.
   */
  RawData(const std::string &data)
      : addr_(const_cast<char *>(data.data())), size_(data.size())
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
  RawData &operator=(const RawData &data) = default;

  void *addr_;   ///< 数据存储地址。 The storage address of the data.
  size_t size_;  ///< 数据大小（字节）。 The size of the data (in bytes).
};

/**
 * @brief 常量原始数据封装类。
 *        A class for encapsulating constant raw data.
 *
 * 该类与 `RawData` 类似，但存储的数据地址是 `const` 类型，
 * 以确保数据不可修改。
 * This class is similar to `RawData`, but the stored data address is `const`,
 * ensuring the data remains immutable.
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
  ConstRawData(const void *addr, size_t size) : addr_(addr), size_(size) {}

  /**
   * @brief 默认构造函数，初始化为空数据。
   *        Default constructor initializing to empty data.
   */
  ConstRawData() : addr_(nullptr), size_(0) {}

  /**
   * @brief 使用任意数据类型构造
   * `ConstRawData`，数据地址指向该对象，大小为该类型的字节大小。 Constructs
   * `ConstRawData` using any data type, pointing to the object and setting the size to
   * the type's byte size.
   *
   * @tparam DataType 数据类型。
   *                  The data type.
   * @param data 需要存储的具体数据。
   *             The actual data to be stored.
   */
  template <typename DataType>
  ConstRawData(const DataType &data)
      : addr_(const_cast<DataType *>(&data)), size_(sizeof(DataType))
  {
  }

  /**
   * @brief 拷贝构造函数。
   *        Copy constructor.
   *
   * @param data 另一个 `ConstRawData` 对象。
   *             Another `ConstRawData` object.
   */
  ConstRawData(const ConstRawData &data) = default;

  /**
   * @brief 从 `RawData` 构造 `ConstRawData`，数据地址和大小保持不变。
   *        Constructs `ConstRawData` from `RawData`,
   *        keeping the same data address and size.
   *
   * @param data `RawData` 对象。
   *             A `RawData` object.
   */
  ConstRawData(const RawData &data) : addr_(data.addr_), size_(data.size_) {}

  /**
   * @brief 从 `char *` 指针构造 `ConstRawData`，数据大小为字符串长度（不含 `\0`）。
   *        Constructs `ConstRawData` from a `char *` pointer,
   *        with size set to the string length (excluding `\0`).
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  ConstRawData(char *data) : addr_(data), size_(data ? strlen(data) : 0) {}

  /**
   * @brief 从 `char *` 指针构造 `ConstRawData`（常量版本）。
   *        Constructs `ConstRawData` from a `const char *` pointer.
   *
   * @param data C 风格字符串指针。
   *             A C-style string pointer.
   */
  ConstRawData(const char *data) : addr_(data), size_(data ? strlen(data) : 0) {}

  /**
   * @brief 赋值运算符重载。
   *        Overloaded assignment operator.
   *
   * @param data 另一个 `ConstRawData` 对象。
   *             Another `ConstRawData` object.
   * @return 返回赋值后的 `ConstRawData` 对象引用。
   *         Returns a reference to the assigned `ConstRawData` object.
   */
  ConstRawData &operator=(const ConstRawData &data) = default;

  const void
      *addr_;    ///< 数据存储地址（常量）。 The storage address of the data (constant).
  size_t size_;  ///< 数据大小（字节）。 The size of the data (in bytes).
};

/**
 * @brief 类型标识符生成器，替代 typeid
 * @brief Type identifier generator (RTTI-free)
 */
class TypeID
{
 public:
  using ID = const void *;
  /**
   * @brief 获取类型的唯一标识符
   * @brief Get unique identifier for type T
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
