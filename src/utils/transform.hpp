#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include <cmath>
#include <iostream>
#include <type_traits>
namespace LibXR {

template <typename Scalar = double> class Position;
template <typename Scalar = double> class RotationMatrix;
template <typename Scalar = double> class EulerAngle;
template <typename Scalar = double> class Quaternion;

template <typename Scalar> class Position {
public:
  Scalar data_[3];

  Scalar &x_ = data_[0];
  Scalar &y_ = data_[1];
  Scalar &z_ = data_[2];

  Position() : data_{0, 0, 0} {}

  Position(Scalar x, Scalar y, Scalar z) : data_{x, y, z} {}

  Position(const Eigen::Matrix<Scalar, 3, 1> &p) : data_{p[0], p[1], p[2]} {}

  Position(const std::array<Scalar, 3> &p) : data_{p[0], p[1], p[2]} {}

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  Position(const T (&data)[3]) : x_(data[0]), y_(data[1]), z_(data[2]) {}

  operator Eigen::Matrix<Scalar, 3, 1>() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(data_);
  }

  const Position &operator=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    memcpy(data_, p.data(), 3 * sizeof(Scalar));
    return *this;
  }

  const Position &operator=(const Position &p) {
    memcpy(data_, p.data_, 3 * sizeof(Scalar));
    return *this;
  }

  Scalar operator()(int i) const { return data_[i]; }

  template <
      typename Rotation,
      std::enable_if_t<
          std::is_same<Rotation, RotationMatrix<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Matrix<Scalar, 3, 3>>::value ||
              std::is_same<Rotation, Quaternion<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Quaternion<Scalar>>::value,
          int> = 0>
  Eigen::Matrix<Scalar, 3, 1> operator*(const Rotation &R) {
    return R * Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
  }

  template <
      typename Rotation,
      std::enable_if_t<
          std::is_same<Rotation, RotationMatrix<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Matrix<Scalar, 3, 3>>::value ||
              std::is_same<Rotation, Quaternion<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Quaternion<Scalar>>::value,
          int> = 0>
  const Position &operator*=(const Rotation &R) {
    *this = R * Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
    return *this;
  }

  Eigen::Matrix<Scalar, 3, 1> operator/(const RotationMatrix<Scalar> &R) {
    return (-R) * Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
  }

  const Position &operator/=(const Quaternion<Scalar> &q) {
    *this = (-q) * Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
    return *this;
  }

  const Position &operator/=(const Eigen::Matrix<Scalar, 3, 3> &R) {
    *this = R.transpose() *
            Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
    return *this;
  }

  const Position &operator/=(const Eigen::Quaternion<Scalar> &q) {
    *this = q.conjugate() *
            Eigen::Map<const Eigen::Matrix<Scalar, 3, 1>>(this->data_);
    return *this;
  }

  Position operator-() { return Position(-x_, -y_, -z_); }

  Position operator/(Scalar s) { return Position(x_ / s, y_ / s, z_ / s); }

  const Position &operator/=(Scalar s) {
    x_ /= s;
    y_ /= s;
    z_ /= s;
    return *this;
  }

  Position operator*(Scalar s) { return Position(x_ * s, y_ * s, z_ * s); }

  const Position &operator*=(Scalar s) {
    x_ *= s;
    y_ *= s;
    z_ *= s;
    return *this;
  }

  Position operator+(const Position &p) {
    return Position(x_ + p.x_, y_ + p.y_, z_ + p.z_);
  }

  Position operator+(const Eigen::Matrix<Scalar, 3, 1> &p) {
    return Position(x_ + p(0), y_ + p(1), z_ + p(2));
  }

  const Position &operator+=(const Position &p) {
    x_ += p.x_;
    y_ += p.y_;
    z_ += p.z_;
    return *this;
  }

  const Position &operator+=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    x_ += p(0);
    y_ += p(1);
    z_ += p(2);
    return *this;
  }

  Position operator-(const Position &p) {
    return Position(x_ - p.x_, y_ - p.y_, z_ - p.z_);
  }

  Position operator-(const Eigen::Matrix<Scalar, 3, 1> &p) {
    return Position(x_ - p(0), y_ - p(1), z_ - p(2));
  }

  const Position &operator-=(const Position &p) {
    x_ -= p.x_;
    y_ -= p.y_;
    z_ -= p.z_;
    return *this;
  }

  const Position &operator-=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    x_ -= p(0);
    y_ -= p(1);
    z_ -= p(2);
    return *this;
  }
};

template <typename Scalar> class EulerAngle {
public:
  Scalar data_[3];

  Scalar &roll_ = data_[0];
  Scalar &pitch_ = data_[1];
  Scalar &yaw_ = data_[2];

  EulerAngle() : data_{0, 0, 0} {}

  EulerAngle(Scalar roll, Scalar pitch, Scalar yaw) : data_{roll, pitch, yaw} {}

  EulerAngle(const Eigen::Matrix<Scalar, 3, 1> &p)
      : data_{p.x(), p.y(), p.z()} {}

  EulerAngle(const EulerAngle &p) : data_{p.roll_, p.pitch_, p.yaw_} {}

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

  const EulerAngle &operator=(const EulerAngle &p) {
    memcpy(data_, p.data_, 3 * sizeof(Scalar));
    return *this;
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrix() const {
    return toRotationMatrixZYX();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixZYX() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(pitch_), cc = std::cos(roll_);
    Scalar sa = std::sin(yaw_), sb = std::sin(pitch_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, ca * sb * sc - cc * sa,
            sa * sc + ca * cc * sb, cb * sa, ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, -sb, cb * sc, cb * cc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixZXY() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(roll_), cc = std::cos(pitch_);
    Scalar sa = std::sin(yaw_), sb = std::sin(roll_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc - sa * sb * sc, -cb * sa,
            ca * sc + cc * sa * sb, cc * sa + ca * sb * sc, ca * cb,
            sa * sc - ca * cc * sb, -cb * sc, sb, cb * cc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixYXZ() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(roll_), cc = std::cos(yaw_);
    Scalar sa = std::sin(pitch_), sb = std::sin(roll_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, cb * sa, cb * sc, cb * cc, -sb,
            ca * sb * sc - cc * sa, sa * sc + ca * cc * sb, ca * cb)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixYZX() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(yaw_), cc = std::cos(roll_);
    Scalar sa = std::sin(pitch_), sb = std::sin(yaw_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, sa * sc - ca * cc * sb,
            cc * sa + ca * sb * sc, sb, cb * cc, -cb * sc, -cb * sa,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixXYZ() const {
    Scalar ca = std::cos(roll_), cb = std::cos(pitch_), cc = std::cos(yaw_);
    Scalar sa = std::sin(roll_), sb = std::sin(pitch_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -cb * sc, sb,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc, -cb * sa,
            sa * sc - ca * cc * sb, cc * sa + ca * sb * sc, ca * cb)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrixXZY() const {
    Scalar ca = std::cos(roll_), cb = std::cos(yaw_), cc = std::cos(pitch_);
    Scalar sa = std::sin(roll_), sb = std::sin(yaw_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -sb, cb * sc,
            sa * sc + ca * cc * sb, ca * cb, ca * sb * sc - cc * sa,
            cc * sa * sb - ca * sc, cb * sa, ca * cc + sa * sb * sc)
        .finished();
  }

  Eigen::Quaternion<Scalar> toQuaternion() const { return toQuaternionZYX(); }

#if 0
  Eigen::Quaternion<Scalar> toQuaternionXYZ() const {
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    return rollAngle * pitchAngle * yawAngle;
  }

  Eigen::Quaternion<Scalar> toQuaternionXZY() const {
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    return rollAngle * yawAngle * pitchAngle;
  }

  Eigen::Quaternion<Scalar> toQuaternionYXZ() const {
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    return pitchAngle * rollAngle * yawAngle;
  }

  Eigen::Quaternion<Scalar> toQuaternionYZX() const {
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    return pitchAngle * yawAngle * rollAngle;
  }

  Eigen::Quaternion<Scalar> toQuaternionZXY() const {
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    return yawAngle * rollAngle * pitchAngle;
  }

  Eigen::Quaternion<Scalar> toQuaternionZYX() const {
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    return yawAngle * pitchAngle * rollAngle;
  }
#endif

  Eigen::Quaternion<Scalar> toQuaternionXYZ() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixXYZ());
  }

  Eigen::Quaternion<Scalar> toQuaternionXZY() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixXZY());
  }

  Eigen::Quaternion<Scalar> toQuaternionYXZ() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixYXZ());
  }

  Eigen::Quaternion<Scalar> toQuaternionYZX() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixYZX());
  }

  Eigen::Quaternion<Scalar> toQuaternionZXY() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixZXY());
  }

  Eigen::Quaternion<Scalar> toQuaternionZYX() const {
    return Eigen::Quaternion<Scalar>(toRotationMatrixZYX());
  }
};

template <typename Scalar> class RotationMatrix {
public:
  Scalar data_[9];

  RotationMatrix() : data_{1, 0, 0, 0, 1, 0, 0, 0, 1} {}

  RotationMatrix(Scalar r00, Scalar r01, Scalar r02, Scalar r10, Scalar r11,
                 Scalar r12, Scalar r20, Scalar r21, Scalar r22)
      : data_{r00, r10, r20, r01, r11, r21, r02, r12, r22} {}

  RotationMatrix(const Eigen::Matrix<Scalar, 3, 3> &R) {
    memcpy(data_, R.data(), 9 * sizeof(Scalar));
  }

  RotationMatrix(const RotationMatrix &R) {
    memcpy(data_, R.data(), 9 * sizeof(Scalar));
  }

  RotationMatrix(const Eigen::Quaternion<Scalar> &q) { *this = q; }

  RotationMatrix(const Quaternion<Scalar> &q) { *this = q.toRotationMatrix(); }

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
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_);
  }

  Scalar operator()(int i, int j) const { return data_[i + j * 3]; }

  Eigen::Matrix<Scalar, 3, 3> operator-() const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(this->data_)
        .transpose();
  }

  const RotationMatrix &operator=(const RotationMatrix &R) {
    memcpy(data_, R.data(), 9 * sizeof(Scalar));
    return *this;
  }

  const RotationMatrix &operator=(const Eigen::Matrix<Scalar, 3, 3> &R) {
    memcpy(data_, R.data(), 9 * sizeof(Scalar));
    return *this;
  }

  const RotationMatrix &operator=(const Eigen::Quaternion<Scalar> &q) {
    *this = q.toRotationMatrix();
    return *this;
  }

  const RotationMatrix &operator=(const Quaternion<Scalar> &q) {
    *this = q.toRotationMatrix();
    return *this;
  }

  Position<Scalar> operator*(const Position<Scalar> &p) const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_) *
           Eigen::Matrix<Scalar, 3, 1>(p);
  }

  Eigen::Matrix<Scalar, 3, 1>
  operator*(const Eigen::Matrix<Scalar, 3, 1> &p) const {
    return Eigen::Map<const Eigen::Matrix<Scalar, 3, 3>>(data_) * p;
  }

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

template <typename Scalar> class Quaternion {
public:
  Scalar data_[4];

  Scalar &w_ = data_[0];
  Scalar &x_ = data_[1];
  Scalar &y_ = data_[2];
  Scalar &z_ = data_[3];

  Quaternion() : data_{1, 0, 0, 0} {}

  Quaternion(Scalar w, Scalar x, Scalar y, Scalar z) : data_{w, x, y, z} {}

  Quaternion(const Eigen::Quaternion<Scalar> &q) {
    w_ = q.w();
    x_ = q.x();
    y_ = q.y();
    z_ = q.z();
  }

  Quaternion(const RotationMatrix<Scalar> &R) {
    *this = Eigen::Quaternion<Scalar>((Eigen::Matrix<Scalar, 3, 3>)(R));
  }

  Quaternion(const Eigen::Matrix<Scalar, 3, 3> R) {
    *this = Eigen::Quaternion<Scalar>(R);
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  Quaternion(const T (&data)[4]) : data_{data[0], data[1], data[2], data[3]} {}

  operator Eigen::Quaternion<Scalar>() const {
    return Eigen::Quaternion<Scalar>(w_, x_, y_, z_);
  }

  Quaternion normalized() const {
    Eigen::Quaternion<Scalar> q(w_, x_, y_, z_);
    return Quaternion(q.normalized());
  }

  Eigen::Matrix<Scalar, 3, 3> toRotationMatrix() const {
    Eigen::Quaternion<Scalar> q(w_, x_, y_, z_);
    return q.toRotationMatrix();
  }

  Scalar operator()(int i) const { return data_[i]; }

  const Quaternion &operator=(const Eigen::Quaternion<Scalar> &q) {
    w_ = q.w();
    x_ = q.x();
    y_ = q.y();
    z_ = q.z();
    return *this;
  }

  const Quaternion &operator=(const Quaternion &q) {
    memcpy(data_, q.data_, sizeof(Scalar) * 4);
    return *this;
  }

  Quaternion operator-() const {
    return Eigen::Quaternion<Scalar>(*this).conjugate();
  }

  Quaternion operator+(const Quaternion &q) const {
    return Eigen::Quaternion<Scalar>(w_ + q.w_, x_ + q.x_, y_ + q.y_,
                                     z_ + q.z_);
  }

  Quaternion operator+(const Eigen::Quaternion<Scalar> &q) const {
    return Eigen::Quaternion<Scalar>(*this) + q;
  }

  Quaternion operator-(const Quaternion &q) const {
    return Eigen::Quaternion<Scalar>(w_ - q.w_, x_ - q.x_, y_ - q.y_,
                                     z_ - q.z_);
  }

  Quaternion operator-(const Eigen::Quaternion<Scalar> &q) const {
    return Eigen::Quaternion<Scalar>(*this) - q;
  }

  template <
      typename Q,
      std::enable_if_t<std::is_same<Q, Quaternion>::value ||
                           std::is_same<Q, Eigen::Quaternion<Scalar>>::value,
                       int> = 0>
  const Quaternion &operator+=(const Q &q) {
    *this = *this + q;
    return *this;
  }

  template <
      typename Q,
      std::enable_if_t<std::is_same<Q, Quaternion>::value ||
                           std::is_same<Q, Eigen::Quaternion<Scalar>>::value,
                       int> = 0>
  const Quaternion &operator-=(const Q &q) {
    *this = *this - q;
    return *this;
  }

  Position<Scalar> operator*(const Position<Scalar> &p) const {
    return Eigen::Quaternion<Scalar>(*this) * Eigen::Matrix<Scalar, 3, 1>(p);
  }

  Eigen::Matrix<Scalar, 3, 1>
  operator*(const Eigen::Matrix<Scalar, 3, 1> &p) const {
    return Eigen::Quaternion<Scalar>(*this) * p;
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngle() const { return toEulerAngleZYX(); }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleYZX() const {
    Scalar roll = atan2(2 * w_ * x_ - 2 * y_ * z_, 1 - 2 * (x_ * x_ + z_ * z_));
    Scalar pitch =
        atan2(2 * w_ * y_ - 2 * x_ * z_, 1 - 2 * (y_ * y_ + z_ * z_));
    Scalar yaw = asin(2 * (w_ * z_ + x_ * y_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleZYX() const {
    Scalar yaw = atan2(2 * (w_ * z_ + x_ * y_), 1 - 2 * (z_ * z_ + y_ * y_));
    Scalar pitch = asin(2 * (w_ * y_ - x_ * z_));
    Scalar roll = atan2(2 * (w_ * x_ + y_ * z_), 1 - 2 * (y_ * y_ + x_ * x_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleYXZ() const {
    Scalar roll = asin(2 * (w_ * x_ - y_ * z_));
    Scalar yaw = atan2(2 * (w_ * z_ + x_ * y_), 1 - 2 * (z_ * z_ + x_ * x_));
    Scalar pitch = atan2(2 * (x_ * z_ + w_ * y_), 1 - 2 * (y_ * y_ + x_ * x_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleZXY() const {
    Scalar pitch = atan2(-2 * (x_ * z_ - w_ * y_), 1 - 2 * (y_ * y_ + x_ * x_));
    Scalar roll = asin(2 * (w_ * x_ + y_ * z_));
    Scalar yaw = atan2(-2 * (x_ * y_ - w_ * z_), 1 - 2 * (z_ * z_ + x_ * x_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleXZY() const {
    Scalar pitch = atan2(2 * (w_ * y_ + x_ * z_), 1 - 2 * (z_ * z_ + y_ * y_));
    Scalar yaw = asin(2 * (w_ * z_ - x_ * y_));
    Scalar roll = atan2(2 * (w_ * x_ + y_ * z_), 1 - 2 * (z_ * z_ + x_ * x_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> toEulerAngleXYZ() const {
    Scalar yaw = atan2(-2 * (x_ * y_ - w_ * z_), 1 - 2 * (z_ * z_ + y_ * y_));
    Scalar pitch = asin(2 * (w_ * y_ + x_ * z_));
    Scalar roll = atan2(-2 * (y_ * z_ - w_ * x_), 1 - 2 * (y_ * y_ + x_ * x_));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }
};
} // namespace LibXR