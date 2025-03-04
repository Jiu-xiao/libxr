#pragma once

#include <array>
#include <cstddef>
#include <cstring>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

namespace LibXR {

template <unsigned int MaxLength> class String {
public:
  String() : raw_string_() {}

  String(const char *str) {
    std::strncpy(&raw_string_[0], str, MaxLength);
    raw_string_[MaxLength] = '\0';
  }

  String(const char *str, size_t len) {
    size_t copy_len = LibXR::MIN(MaxLength, len);
    std::strncpy(&raw_string_[0], str, copy_len);
    raw_string_[copy_len] = '\0'; // 确保字符串终止
  }

  const char *Raw() const { return &raw_string_[0]; }

  char operator[](unsigned int i) {
    ASSERT(i < MaxLength);
    return raw_string_[i];
  }

  const char operator[](unsigned int i) const {
    ASSERT(i < MaxLength);
    return raw_string_[i];
  }

  const char *operator+(unsigned int i) { return &raw_string_[i]; }

  String &operator+=(const char *str) {
    auto len = strnlen(this->Raw(), MaxLength);
    size_t copy_len = std::min(MaxLength - len, std::strlen(str));
    std::strncat(&raw_string_[0], str, copy_len);
    raw_string_[MaxLength] = '\0';
    return *this;
  }

  int Find(const char *str) const {
    if (!str)
      return -1;
    const char *result = std::strstr(this->Raw(), str);
    return result ? static_cast<int>(result - this->Raw()) : -1;
  }

  template <unsigned int NextStrLength>
  bool operator==(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) == 0;
  }

  template <unsigned int NextStrLength>
  bool operator!=(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) != 0;
  }

  template <unsigned int NextStrLength>
  bool operator<(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) < 0;
  }

  template <unsigned int NextStrLength>
  bool operator>(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) > 0;
  }

  template <unsigned int NextStrLength>
  bool operator<=(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) <= 0;
  }

  template <unsigned int NextStrLength>
  bool operator>=(const String<NextStrLength> &other) const {
    return strncmp(this->Raw(), other.Raw(), MaxLength) >= 0;
  }

  size_t Length() const { return strnlen(this->Raw(), MaxLength); }

  void Clear() { raw_string_[0] = '\0'; }

  template <unsigned int SubStrLength>
  String<SubStrLength> Substr(size_t pos) const {
    ASSERT(pos < MaxLength);
    return String<SubStrLength>(&raw_string_[pos],
                                LibXR::MIN(SubStrLength, MaxLength - pos));
  }

private:
  std::array<char, MaxLength + 1> raw_string_;
};
} // namespace LibXR