#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <type_traits>

#include "transform.hpp"

namespace LibXR {

template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class Inertia {
 public:
  Scalar data[9];

  Scalar mass;

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[9]) : mass(m) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        data[i * 3 + j] = static_cast<Scalar>(data[i + j + 3]);
      }
    }
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[3][3]) : mass(m) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        data[i * 3 + j] = static_cast<Scalar>(data[j][i]);
      }
    }
  }

  Inertia() : mass(0) { memset(data, 0, sizeof(data)); }

  template <typename T, std::enable_if_t<std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  explicit Inertia(Scalar m, const T (&data)[6])
      : data{data[0],  -data[3], -data[5], -data[3], data[2],
             -data[4], -data[5], -data[4], data[2]},
        mass(m) {}

  Inertia(Scalar m, Scalar xx, Scalar yy, Scalar zz, Scalar xy, Scalar yz,
          Scalar xz)
      : data{xx, -xy, -xz, -xy, yy, -yz, -xz, -yz, zz}, mass(m) {}

  Inertia(Scalar m, const Eigen::Matrix<Scalar, 3, 3> &R) : mass(m) {
    memcpy(data, R.data(), 9 * sizeof(Scalar));
  }

  operator Eigen::Matrix<Scalar, 3, 3>() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data);
  }

  Scalar operator()(int i, int j) const { return data[i + j * 3]; }

  Eigen::Matrix<Scalar, 3, 3> operator+(
      const Eigen::Matrix<Scalar, 3, 3> &R) const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data) + R;
  }

  Inertia Translate(const Eigen::Matrix<Scalar, 3, 1> &p) const {
    Scalar dx = p(0), dy = p(1), dz = p(2);
    Eigen::Matrix<Scalar, 3, 3> translation_matrix;
    translation_matrix << dy * dy + dz * dz, -dx * dy, -dx * dz, -dx * dy,
        dx * dx + dz * dz, -dy * dz, -dx * dz, -dy * dz, dx * dx + dy * dy;

    return Inertia(mass, Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data) +
                             mass * translation_matrix);
  }

  Inertia Rotate(const Eigen::Matrix<Scalar, 3, 3> &R) const {
    return Inertia(mass,
                   R * Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data) *
                       R.transpose());
  }

  Inertia Rotate(const RotationMatrix<Scalar> &R) const {
    return Rotate(Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(R.data_));
  }

  Inertia Rotate(const Eigen::Quaternion<Scalar> &q) const {
    return Inertia(mass,
                   q * Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data) *
                       q.conjugate());
  }

  static Eigen::Matrix<Scalar, 3, 3> Rotate(
      const Eigen::Matrix<Scalar, 3, 3> &R,
      const Eigen::Quaternion<Scalar> &q) {
    return q * R * q.conjugate();
  }

  Inertia Rotate(const Quaternion<Scalar> &q) const {
    return Rotate(Eigen::Quaternion<Scalar>(q));
  }
};

template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class CenterOfMass {
 public:
  Eigen::Matrix<Scalar, 3, 1> position;
  Scalar mass;

  CenterOfMass() : position(0., 0., 0.), mass(0.) {}

  CenterOfMass(Scalar m, const LibXR::Position<Scalar> &p)
      : position(p), mass(m) {}

  CenterOfMass(Scalar m, const Eigen::Matrix<Scalar, 3, 1> &p)
      : position(p), mass(m) {}

  CenterOfMass(const Inertia<Scalar> &m, const Transform<Scalar> &p)
      : position(p.translation), mass(m.mass) {}

  CenterOfMass operator+(const CenterOfMass &m) const {
    Scalar new_mass = mass + m.mass;
    return CenterOfMass(
        new_mass,
        Position<Scalar>(
            (position(0) * mass + m.position(0) * m.mass) / new_mass,
            (position(1) * mass + m.position(1) * m.mass) / new_mass,
            (position(2) * mass + m.position(2) * m.mass) / new_mass));
  }

  CenterOfMass &operator+=(const CenterOfMass &m) {
    *this = *this + m;
    return *this;
  }
};

}  // namespace LibXR
