#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

namespace LibXR {

template <unsigned int MaxLength> class String {
public:
  String() : raw_string_() {}

  String(const char *str) { strncpy(&raw_string_[0], str, MaxLength); }

  String(const char *str, size_t len) {
    strncpy(&raw_string_[0], str, MAX(MaxLength, len));
  }

  const char *Raw() const { return &raw_string_[0]; }

  char operator[](unsigned int i) { return raw_string_[i]; }
  const char *operator+(unsigned int i) { return &raw_string_[i]; }

  String &operator+=(const char *str) {
    auto len = strnlen(this->Raw(), MaxLength);
    strncat(&raw_string_[len], str, MaxLength - len);
    return *this;
  }

  int Find(const char *str) const {
    const char *result = strstr(this->Raw(), str);
    if (result) {
      return result - this->Raw();
    } else {
      return -1;
    }
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
    ASSERT(pos + SubStrLength < MaxLength);
    return String<SubStrLength>(
        reinterpret_cast<const char *>(&raw_string_[pos]));
  }

private:
  std::array<char, MaxLength + 1> raw_string_;
};
} // namespace LibXR