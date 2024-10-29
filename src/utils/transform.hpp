#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include <cmath>
#include <iostream>
namespace LibXR {
template <typename Scalar = double> class Position {
public:
  Scalar data_[3];

  Scalar &x_ = data_[0];
  Scalar &y_ = data_[1];
  Scalar &z_ = data_[2];

  Position() : data_{0, 0, 0} {}

  Position(Scalar x, Scalar y, Scalar z) : data_{x, y, z} {}

  Position(const Eigen::Matrix<Scalar, 3, 1> &p)
      : x_(p.x()), y_(p.y()), z_(p.z()) {}

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  Position(const T (&data)[3]) : x_(data[0]), y_(data[1]), z_(data[2]) {}

  operator Eigen::Matrix<Scalar, 3, 1>() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(data_);
  }
};

template <typename Scalar = double> class EulerAngle {
public:
  Scalar data_[3];

  Scalar &roll_ = data_[0];
  Scalar &pitch_ = data_[1];
  Scalar &yaw_ = data_[2];

  EulerAngle() : data_{0, 0, 0} {}

  EulerAngle(Scalar roll, Scalar pitch, Scalar yaw) : data_{roll, pitch, yaw} {}

  EulerAngle(const Eigen::Matrix<Scalar, 3, 1> &p)
      : data_{p.x(), p.y(), p.z()} {}

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  EulerAngle(const T (&data)[3]) : data_{data[0], data[1], data[2]} {}

  operator Eigen::Matrix<Scalar, 3, 1>() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(data_);
  }

  Scalar operator()(int i) const { return data_[i]; }

  const EulerAngle &operator=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    data_[0] = p(0);
    data_[1] = p(1);
    data_[2] = p(2);
    return *this;
  }

  // ZYX Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixZYX() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(pitch_), cc = std::cos(roll_);
    Scalar sa = std::sin(yaw_), sb = std::sin(pitch_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, ca * sb * sc - cc * sa,
            sa * sc + ca * cc * sb, cb * sa, ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, -sb, cb * sc, cb * cc)
        .finished();
  }

  // ZXY Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixZXY() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(roll_), cc = std::cos(pitch_);
    Scalar sa = std::sin(yaw_), sb = std::sin(roll_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc - sa * sb * sc, -cb * sa,
            ca * sc + cc * sa * sb, cc * sa + ca * sb * sc, ca * cb,
            sa * sc - ca * cc * sb, -cb * sc, sb, cb * cc)
        .finished();
  }

  // YXZ Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixYXZ() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(roll_), cc = std::cos(yaw_);
    Scalar sa = std::sin(pitch_), sb = std::sin(roll_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, cb * sa, cb * sc, cb * cc, -sb,
            ca * sb * sc - cc * sa, sa * sc + ca * cc * sb, ca * cb)
        .finished();
  }

  // YZX Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixYZX() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(yaw_), cc = std::cos(roll_);
    Scalar sa = std::sin(pitch_), sb = std::sin(yaw_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, sa * sc - ca * cc * sb,
            cc * sa + ca * sb * sc, sb, cb * cc, -cb * sc, -cb * sa,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc)
        .finished();
  }

  // XYZ Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixXYZ() const {
    Scalar ca = std::cos(roll_), cb = std::cos(pitch_), cc = std::cos(yaw_);
    Scalar sa = std::sin(roll_), sb = std::sin(pitch_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -cb * sc, sb,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc, -cb * sa,
            sa * sc - ca * cc * sb, cc * sa + ca * sb * sc, ca * cb)
        .finished();
  }

  // XZY Order
  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixXZY() const {
    Scalar ca = std::cos(roll_), cb = std::cos(yaw_), cc = std::cos(pitch_);
    Scalar sa = std::sin(roll_), sb = std::sin(yaw_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -sb, cb * sc,
            sa * sc + ca * cc * sb, ca * cb, ca * sb * sc - cc * sa,
            cc * sa * sb - ca * sc, cb * sa, ca * cc + sa * sb * sc)
        .finished();
  }
};

template <typename Scalar = double> class RotationMatrix {
public:
  Scalar data_[9];

  RotationMatrix() : data_{1, 0, 0, 0, 1, 0, 0, 0, 1} {}

  RotationMatrix(Scalar r00, Scalar r01, Scalar r02, Scalar r10, Scalar r11,
                 Scalar r12, Scalar r20, Scalar r21, Scalar r22)
      : data_{r00, r10, r20, r01, r11, r21, r02, r12, r22} {}

  RotationMatrix(const Eigen::Matrix<Scalar, 3, 3> &R) {
    memcpy(data_, R.data(), 9 * sizeof(Scalar));
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  RotationMatrix(const T (&data)[9]) {
    for (int i = 0; i < 9; i++) {
      data_[i] = data[i % 3 * 3 + i / 3];
    }
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  RotationMatrix(const T (&data)[3][3]) {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        data_[i * 3 + j] = data[j][i];
      }
    }
  }

  operator Eigen::Matrix<Scalar, 3, 3>() const {
    return Eigen::Map<Eigen::Matrix<Scalar, 3, 3>>(data_);
  }

  Scalar operator()(int i, int j) const { return data_[i + j * 3]; }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngle() const { return toEulerAngleZYX(); }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleZYX() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar roll = std::atan2(R(2, 1), R(2, 2));
    Scalar yaw = std::atan2(R(1, 0), R(0, 0));
    Scalar pitch = std::asin(-R(2, 0));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleXZY() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar roll = std::atan2(R(2, 1), R(1, 1));
    Scalar yaw = std::asin(-R(0, 1));
    Scalar pitch = std::atan2(R(0, 2), R(0, 0));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleYZX() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar pitch = std::atan2(-R(2, 0), R(0, 0));
    Scalar yaw = std::asin(R(1, 0));
    Scalar roll = std::atan2(-R(1, 2), R(1, 1));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleYXZ() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar pitch = std::atan2(R(0, 2), R(2, 2));
    Scalar roll = std::asin(-R(1, 2));
    Scalar yaw = std::atan2(R(1, 0), R(1, 1));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleZXY() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar roll = std::asin(R(2, 1));
    Scalar yaw = std::atan2(R(1, 1), -R(0, 1));
    Scalar pitch = std::atan2(-R(2, 0), R(2, 2));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleXYZ() const {
    Eigen::Matrix<Scalar, 3, 3> R =
        Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);

    Scalar yaw = std::atan2(-R(0, 1), R(0, 0));
    Scalar pitch = std::asin(R(0, 2));
    Scalar roll = std::atan2(-R(1, 2), R(2, 2));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }
};
} // namespace LibXR