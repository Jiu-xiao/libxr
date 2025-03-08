#include <cmath>

#include "libxr_def.hpp"

namespace LibXR {

using DefaultScalar = LIBXR_DEFAULT_SCALAR;

template <typename Scalar = DefaultScalar>
class CycleValue {
 public:
  CycleValue &operator=(const CycleValue &) = default;

  static Scalar Calculate(Scalar value) {
    value = std::fmod(value, M_2PI);
    if (value < 0) {
      value += M_2PI;
    }
    return value;
  }

  CycleValue(const Scalar &value) : value_(Calculate(value)) {}

  CycleValue(const CycleValue &value) : value_(value.value_) {
    while (value_ >= M_2PI) {
      value_ -= M_2PI;
    }

    while (value_ < 0) {
      value_ += M_2PI;
    }
  }

  CycleValue() : value_(0.0f) {}

  CycleValue operator+(const Scalar &value) {
    return CycleValue(value + value_);
  }

  CycleValue operator+(const CycleValue &value) {
    return CycleValue(value.value_ + value_);
  }

  CycleValue operator+=(const Scalar &value) {
    value_ = Calculate(value + value_);

    return *this;
  }

  CycleValue operator+=(const CycleValue &value) {
    Scalar ans = value.value_ + value_;
    while (ans >= M_2PI) {
      ans -= M_2PI;
    }

    while (ans < 0) {
      ans += M_2PI;
    }

    value_ = ans;

    return *this;
  }

  Scalar operator-(const Scalar &raw_value) {
    Scalar value = Calculate(raw_value);
    Scalar ans = value_ - value;
    while (ans >= M_PI) {
      ans -= M_2PI;
    }

    while (ans < -M_PI) {
      ans += M_2PI;
    }

    return ans;
  }

  Scalar operator-(const CycleValue &value) {
    Scalar ans = value_ - value.value_;
    while (ans >= M_PI) {
      ans -= M_2PI;
    }

    while (ans < -M_PI) {
      ans += M_2PI;
    }

    return ans;
  }

  CycleValue operator-=(const Scalar &value) {
    value_ = Calculate(value_ - value);

    return *this;
  }

  CycleValue operator-=(const CycleValue &value) {
    Scalar ans = value_ - value.value_;
    while (ans >= M_2PI) {
      ans -= M_2PI;
    }

    while (ans < 0) {
      ans += M_2PI;
    }

    value_ = ans;

    return *this;
  }

  CycleValue operator-() { return CycleValue(M_2PI - value_); }

  operator Scalar() { return this->value_; }

  CycleValue &operator=(const Scalar &value) {
    value_ = Calculate(value);
    return *this;
  }

  Scalar Value() { return value_; }

 private:
  Scalar value_;
};

}  // namespace LibXR