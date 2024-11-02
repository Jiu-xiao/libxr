#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <type_traits>

#include "transform.hpp"

namespace LibXR {

template <typename Scalar = LIBXR_DEFAULT_SCALAR> class Inertia {
public:
  Scalar raw_data_[9];

  Scalar m_;

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[9]) : m_(m) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        raw_data_[i * 3 + j] = static_cast<Scalar>(data[i + j + 3]);
      }
    }
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[3][3]) : m_(m) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        raw_data_[i * 3 + j] = static_cast<Scalar>(data[j][i]);
      }
    }
  }

  Inertia() : m_(0) { memset(raw_data_, 0, sizeof(raw_data_)); }

  template <typename T, std::enable_if_t<std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[6])
      : raw_data_{data[0],  -data[3], -data[5], -data[3], data[2],
                  -data[4], -data[5], -data[4], data[2]},
        m_(m) {}

  Inertia(Scalar m, Scalar xx, Scalar yy, Scalar zz, Scalar xy, Scalar yz,
          Scalar xz)
      : raw_data_{xx, -xy, -xz, -xy, yy, -yz, -xz, -yz, zz}, m_(m) {}

  Inertia(Scalar m, const Eigen::Matrix<Scalar, 3, 3> &R) : m_(m) {
    memcpy(raw_data_, R.data(), 9 * sizeof(Scalar));
  }

  operator Eigen::Matrix<Scalar, 3, 3>() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(raw_data_);
  }

  Scalar operator()(int i, int j) const { return raw_data_[i + j * 3]; }

  Eigen::Matrix<Scalar, 3, 3>
  operator+(const Eigen::Matrix<Scalar, 3, 3> &R) const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(raw_data_) + R;
  }

  Inertia Translate(const Eigen::Matrix<Scalar, 3, 1> &p) const {
    Scalar dx = p(0), dy = p(1), dz = p(2);
    Eigen::Matrix<Scalar, 3, 3> translationMatrix;
    translationMatrix << dy * dy + dz * dz, -dx * dy, -dx * dz, -dx * dy,
        dx * dx + dz * dz, -dy * dz, -dx * dz, -dy * dz, dx * dx + dy * dy;

    return Inertia(m_,
                   Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(raw_data_) +
                       m_ * translationMatrix);
  }

  Inertia Rotate(const Eigen::Matrix<Scalar, 3, 3> &R) const {
    return Inertia(
        m_, R * Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(raw_data_) *
                R.transpose());
  }

  Inertia Rotate(const RotationMatrix<Scalar> &R) const {
    return Rotate(Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(R.data_));
  }

  Inertia Rotate(const Eigen::Quaternion<Scalar> &q) const {
    return Inertia(
        m_, q * Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(raw_data_) *
                q.conjugate());
  }

  static Eigen::Matrix<Scalar, 3, 3>
  Rotate(const Eigen::Matrix<Scalar, 3, 3> &R,
         const Eigen::Quaternion<Scalar> &q) {
    return q * R * q.conjugate();
  }

  Inertia Rotate(const Quaternion<Scalar> &q) const {
    return Rotate(Eigen::Quaternion<Scalar>(q));
  }
};

} // namespace LibXR
