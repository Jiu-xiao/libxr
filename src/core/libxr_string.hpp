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

  String(const char *str) { strncpy(raw_string_, str, MaxLength); }

  String(const char *str, size_t len) {
    strncpy(raw_string_, str, MAX(MaxLength, len));
  }

  const char *Raw() { return &raw_string_[0]; }

  char operator[](unsigned int i) { return raw_string_[i]; }
  const char *operator+(unsigned int i) { return &raw_string_[i]; }

  String &operator+=(const char *str) {
    auto len = strnlen(&raw_string_[0], MaxLength);
    strncat(&raw_string_[len], str, MaxLength - len);
    return *this;
  }

  int Find(const char *str) const {
    const char *result = strstr(raw_string_, str);
    if (result) {
      return result - raw_string_;
    } else {
      return -1;
    }
  }

  int Find(char ch) const {
    const char *result = strchr(raw_string_, ch);
    if (result) {
      return result - raw_string_;
    } else {
      return -1;
    }
  }

  bool operator==(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) == 0;
  }

  bool operator!=(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) != 0;
  }

  bool operator<(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) < 0;
  }

  bool operator>(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) > 0;
  }

  bool operator<=(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) <= 0;
  }

  bool operator>=(const String &other) const {
    return strncmp(raw_string_, other.raw_string_, MaxLength) >= 0;
  }

  size_t Length() const { return strnlen(raw_string_, MaxLength); }

  void Clear() { raw_string_[0] = '\0'; }

  template <unsigned int SubStrLength> String Substr(size_t pos) const {
    ASSERT(pos + SubStrLength < MaxLength);
    return String<SubStrLength>(*this[pos]);
  }

private:
  std::array<char, MaxLength + 1> raw_string_;
};
} // namespace LibXR