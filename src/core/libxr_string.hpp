#pragma once

#include <array>
#include <cstddef>
#include <cstring>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @class String
 * @brief A fixed-length string class with safe operations.
 * @tparam MaxLength The maximum length of the string.
 *
 * 具有固定长度的字符串类，提供安全的字符串操作。
 */
template <unsigned int MaxLength>
class String
{
 public:
  /**
   * @brief Default constructor, initializes an empty string.
   *
   * 默认构造函数，初始化为空字符串。
   */
  String() : raw_string_() {}

  /**
   * @brief Constructs a String from a C-style string.
   * @param str The C-style string to copy from.
   *
   * 从 C 风格字符串构造 String。
   */
  String(const char* str)
  {
    std::strncpy(&raw_string_[0], str, MaxLength);
    raw_string_[MaxLength] = '\0';
  }

  /**
   * @brief Constructs a String from a C-style string with a given length.
   * @param str The C-style string to copy from.
   * @param len The maximum number of characters to copy.
   *
   * 从 C 风格字符串构造 String，并指定最大拷贝长度。
   */
  String(const char* str, size_t len)
  {
    size_t copy_len = LibXR::min(MaxLength, len);
    std::strncpy(&raw_string_[0], str, copy_len);
    raw_string_[copy_len] = '\0';  // 确保字符串终止
  }

  /**
   * @brief Gets the raw C-style string.
   * @return A pointer to the raw string.
   *
   * 获取原始 C 风格字符串。
   */
  const char* Raw() const { return &raw_string_[0]; }

  /**
   * @brief Accesses a character at a given index.
   * @param i The index of the character.
   * @return The character at the specified index.
   *
   * 访问指定索引的字符。
   */
  char operator[](unsigned int i)
  {
    ASSERT(i < MaxLength);
    return raw_string_[i];
  }

  /**
   * @brief Accesses a character at a given index (const version).
   * @param i The index of the character.
   * @return The character at the specified index.
   *
   * 访问指定索引的字符（常量版本）。
   */
  char operator[](unsigned int i) const
  {
    ASSERT(i < MaxLength);
    return raw_string_[i];
  }

  /**
   * @brief Returns a pointer to a substring starting from index i.
   * @param i The starting index.
   * @return A pointer to the substring.
   *
   * 返回从索引 i 开始的子字符串的指针。
   */
  const char* operator+(unsigned int i) { return &raw_string_[i]; }

  /**
   * @brief Appends a C-style string to the current string.
   * @param str The string to append.
   * @return Reference to the modified string.
   *
   * 将 C 风格字符串追加到当前字符串。
   */
  String& operator+=(const char* str)
  {
    auto len = strnlen(this->Raw(), MaxLength);
    size_t copy_len = LibXR::min(MaxLength - len, std::strlen(str));
    std::strncat(&raw_string_[0], str, copy_len);
    raw_string_[MaxLength] = '\0';
    return *this;
  }

  /**
   * @brief Finds the first occurrence of a substring.
   * @param str The substring to search for.
   * @return The index of the first occurrence, or -1 if not found.
   *
   * 查找子字符串的首次出现位置。
   */
  int Find(const char* str) const
  {
    if (!str)
    {
      return -1;
    }
    const char* result = std::strstr(this->Raw(), str);
    return result ? static_cast<int>(result - this->Raw()) : -1;
  }

  /**
   * @brief Compares two strings for equality.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if equal, false otherwise.
   *
   * 比较两个字符串是否相等。
   */
  template <unsigned int NextStrLength>
  bool operator==(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) == 0;
  }

  /**
   * @brief Compares two strings for inequality.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if not equal, false otherwise.
   *
   * 比较两个字符串是否不相等。
   */
  template <unsigned int NextStrLength>
  bool operator!=(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) != 0;
  }

  /**
   * @brief Less than comparison between two strings.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if this string is less than the other.
   *
   * 比较两个字符串，判断当前字符串是否小于另一个字符串。
   */
  template <unsigned int NextStrLength>
  bool operator<(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) < 0;
  }

  /**
   * @brief Greater than comparison between two strings.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if this string is greater than the other.
   *
   * 比较两个字符串，判断当前字符串是否大于另一个字符串。
   */
  template <unsigned int NextStrLength>
  bool operator>(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) > 0;
  }

  /**
   * @brief Less than or equal comparison.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if this string is less than or equal to the other.
   *
   * 比较两个字符串，判断当前字符串是否小于或等于另一个字符串。
   */
  template <unsigned int NextStrLength>
  bool operator<=(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) <= 0;
  }

  /**
   * @brief Greater than or equal comparison.
   * @tparam NextStrLength The length of the other string.
   * @param other The other string to compare.
   * @return True if this string is greater than or equal to the other.
   *
   * 比较两个字符串，判断当前字符串是否大于或等于另一个字符串。
   */
  template <unsigned int NextStrLength>
  bool operator>=(const String<NextStrLength>& other) const
  {
    return strncmp(this->Raw(), other.Raw(), MaxLength) >= 0;
  }

  /**
   * @brief Gets the length of the string.
   * @return The length of the string.
   *
   * 获取字符串的长度。
   */
  size_t Length() const { return strnlen(this->Raw(), MaxLength); }

  /**
   * @brief Clears the string, making it empty.
   *
   * 清空字符串，使其变为空字符串。
   */
  void Clear() { raw_string_[0] = '\0'; }

  /**
   * @brief Extracts a substring starting at a given position.
   * @tparam SubStrLength The length of the substring.
   * @param pos The starting position.
   * @return The extracted substring.
   *
   * 提取从指定位置开始的子字符串。
   */
  template <unsigned int SubStrLength>
  String<SubStrLength> Substr(size_t pos) const
  {
    ASSERT(pos < MaxLength);
    return String<SubStrLength>(&raw_string_[pos],
                                LibXR::min(SubStrLength, MaxLength - pos));
  }

 private:
  std::array<char, MaxLength + 1> raw_string_;  ///< The raw character array storing the
                                                ///< string. 原始字符数组存储字符串。
};

}  // namespace LibXR
