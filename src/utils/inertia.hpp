#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <type_traits>

namespace LibXR {

template <typename Scalar = double>
class Inertia {
 public:
  Scalar raw_data_[9];

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(const T (&data)[9]) {
    for (int i = 0; i < 9; i++) {
      raw_data_[i] = static_cast<Scalar>(data[i]);
    }
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(const T (&data)[3][3]) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        raw_data_[i * 3 + j] = static_cast<Scalar>(data[i][j]);
      }
    }
  }

  Inertia() { memset(raw_data_, 0, sizeof(raw_data_)); }

  template <typename T, std::enable_if_t<std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(const T (&data)[6])
      : raw_data_({data[0], -data[3], -data[5], -data[3], data[2], -data[4],
                   -data[5], -data[4], data[2]}) {}

  Inertia(Scalar xx, Scalar yy, Scalar zz, Scalar xy, Scalar yz, Scalar xz)
      : raw_data_({xx, -xy, -xz, -xy, yy, -yz, -xz, -yz, zz}) {}

  operator Eigen::Matrix<Scalar, 3, 3>() const {
    return Eigen::Map<Eigen::Matrix<Scalar, 3, 3>>(raw_data_);
  }

  Scalar operator()(int i, int j) const { return raw_data_[i * 3 + j]; }
};

}  // namespace LibXR
