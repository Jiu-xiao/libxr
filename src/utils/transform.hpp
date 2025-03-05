#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <cmath>

namespace LibXR {

template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class Position;
template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class Axis;
template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class RotationMatrix;
template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class EulerAngle;
template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class Quaternion;

template <typename Scalar>
class Position : public Eigen::Matrix<Scalar, 3, 1> {
 public:
  Position() : Eigen::Matrix<Scalar, 3, 1>(0, 0, 0) {}

  Position(Scalar x, Scalar y, Scalar z)
      : Eigen::Matrix<Scalar, 3, 1>(x, y, z) {}

  Position(const Eigen::Matrix<Scalar, 3, 1> &p)
      : Eigen::Matrix<Scalar, 3, 1>(p) {}

  Position(const Position &p) : Eigen::Matrix<Scalar, 3, 1>(p) {}

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  Position(const T (&data)[3]) : Eigen::Matrix<Scalar, 3, 1>(data) {}

  Position &operator=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    memcpy(this->data(), p.data(), 3 * sizeof(Scalar));
    return *this;
  }

  Position &operator=(const Position &p) {
    if (this != &p) {
      memcpy(this->data(), p.data(), 3 * sizeof(Scalar));
    }

    return *this;
  }

  template <
      typename Rotation,
      std::enable_if_t<
          std::is_same<Rotation, RotationMatrix<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Matrix<Scalar, 3, 3>>::value ||
              std::is_same<Rotation, Quaternion<Scalar>>::value ||
              std::is_same<Rotation, Eigen::Quaternion<Scalar>>::value,
          int> = 0>
  Eigen::Matrix<Scalar, 3, 1> operator*(const Rotation &R) {
    return R * (*this);
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
    *this = R * (*this);
    return *this;
  }

  Eigen::Matrix<Scalar, 3, 1> operator/(const RotationMatrix<Scalar> &R) {
    return (-R) * (*this);
  }

  const Position &operator/=(const Quaternion<Scalar> &q) {
    *this = (-q) * (*this);
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

  const Position &operator*=(Scalar s) {
    *this = s * (*this);
    return *this;
  }

  const Position &operator/=(Scalar s) {
    (*this)[0] /= s;
    (*this)[1] /= s;
    (*this)[2] /= s;
    return *this;
  }

  Eigen::Quaternion<Scalar> operator/(const Position<> &p) {
    return Eigen::Quaternion<Scalar>::FromTwoVectors(p, *this);
  }
};

template <typename Scalar>
class Axis : public Eigen::Matrix<Scalar, 3, 1> {
 public:
  Axis() : Eigen::Matrix<Scalar, 3, 1>(0, 0, 0) {}

  Axis(Scalar x, Scalar y, Scalar z) : Eigen::Matrix<Scalar, 3, 1>(x, y, z) {}

  Axis(const Axis &p) : Eigen::Matrix<Scalar, 3, 1>(p) {}

  static Axis X() { return Axis(1., 0., 0.); }
  static Axis Y() { return Axis(0., 1., 0.); }
  static Axis Z() { return Axis(0., 0., 1.); }

  Axis<Scalar> &operator=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    memcpy(this->data(), p.data(), 3 * sizeof(Scalar));
    return *this;
  }

  Axis<Scalar> &operator=(const Axis &p) {
    if (this != &p) {
      memcpy(this->data(), p.data(), 3 * sizeof(Scalar));
    }
    return *this;
  }
};

template <typename Scalar>
class EulerAngle {
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

  EulerAngle &operator=(const Eigen::Matrix<Scalar, 3, 1> &p) {
    data_[0] = p(0);
    data_[1] = p(1);
    data_[2] = p(2);
    return *this;
  }

  EulerAngle &operator=(const EulerAngle &p) {
    if (this != &p) {
      memcpy(data_, p.data_, 3 * sizeof(Scalar));
    }
    return *this;
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrix() const {
    return ToRotationMatrixZYX();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixZYX() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(pitch_), cc = std::cos(roll_);
    Scalar sa = std::sin(yaw_), sb = std::sin(pitch_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, ca * sb * sc - cc * sa,
            sa * sc + ca * cc * sb, cb * sa, ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, -sb, cb * sc, cb * cc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixZXY() const {
    Scalar ca = std::cos(yaw_), cb = std::cos(roll_), cc = std::cos(pitch_);
    Scalar sa = std::sin(yaw_), sb = std::sin(roll_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc - sa * sb * sc, -cb * sa,
            ca * sc + cc * sa * sb, cc * sa + ca * sb * sc, ca * cb,
            sa * sc - ca * cc * sb, -cb * sc, sb, cb * cc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixYXZ() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(roll_), cc = std::cos(yaw_);
    Scalar sa = std::sin(pitch_), sb = std::sin(roll_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cc + sa * sb * sc,
            cc * sa * sb - ca * sc, cb * sa, cb * sc, cb * cc, -sb,
            ca * sb * sc - cc * sa, sa * sc + ca * cc * sb, ca * cb)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixYZX() const {
    Scalar ca = std::cos(pitch_), cb = std::cos(yaw_), cc = std::cos(roll_);
    Scalar sa = std::sin(pitch_), sb = std::sin(yaw_), sc = std::sin(roll_);

    return (Eigen::Matrix<Scalar, 3, 3>() << ca * cb, sa * sc - ca * cc * sb,
            cc * sa + ca * sb * sc, sb, cb * cc, -cb * sc, -cb * sa,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixXYZ() const {
    Scalar ca = std::cos(roll_), cb = std::cos(pitch_), cc = std::cos(yaw_);
    Scalar sa = std::sin(roll_), sb = std::sin(pitch_), sc = std::sin(yaw_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -cb * sc, sb,
            ca * sc + cc * sa * sb, ca * cc - sa * sb * sc, -cb * sa,
            sa * sc - ca * cc * sb, cc * sa + ca * sb * sc, ca * cb)
        .finished();
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrixXZY() const {
    Scalar ca = std::cos(roll_), cb = std::cos(yaw_), cc = std::cos(pitch_);
    Scalar sa = std::sin(roll_), sb = std::sin(yaw_), sc = std::sin(pitch_);

    return (Eigen::Matrix<Scalar, 3, 3>() << cb * cc, -sb, cb * sc,
            sa * sc + ca * cc * sb, ca * cb, ca * sb * sc - cc * sa,
            cc * sa * sb - ca * sc, cb * sa, ca * cc + sa * sb * sc)
        .finished();
  }

  Eigen::Quaternion<Scalar> ToQuaternion() const { return ToQuaternionZYX(); }

#if 0
  Eigen::Quaternion<Scalar> ToQuaternionXYZ() const {
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    return rollAngle * pitchAngle * yawAngle;
  }

  Eigen::Quaternion<Scalar> ToQuaternionXZY() const {
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    return rollAngle * yawAngle * pitchAngle;
  }

  Eigen::Quaternion<Scalar> ToQuaternionYXZ() const {
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    return pitchAngle * rollAngle * yawAngle;
  }

  Eigen::Quaternion<Scalar> ToQuaternionYZX() const {
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    return pitchAngle * yawAngle * rollAngle;
  }

  Eigen::Quaternion<Scalar> ToQuaternionZXY() const {
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    return yawAngle * rollAngle * pitchAngle;
  }

  Eigen::Quaternion<Scalar> ToQuaternionZYX() const {
    Eigen::AngleAxisd yawAngle(yaw_, Eigen::Vector3d::UnitZ());
    Eigen::AngleAxisd pitchAngle(pitch_, Eigen::Vector3d::UnitY());
    Eigen::AngleAxisd rollAngle(roll_, Eigen::Vector3d::UnitX());
    return yawAngle * pitchAngle * rollAngle;
  }
#endif

  Eigen::Quaternion<Scalar> ToQuaternionXYZ() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixXYZ());
  }

  Eigen::Quaternion<Scalar> ToQuaternionXZY() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixXZY());
  }

  Eigen::Quaternion<Scalar> ToQuaternionYXZ() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixYXZ());
  }

  Eigen::Quaternion<Scalar> ToQuaternionYZX() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixYZX());
  }

  Eigen::Quaternion<Scalar> ToQuaternionZXY() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixZXY());
  }

  Eigen::Quaternion<Scalar> ToQuaternionZYX() const {
    return Eigen::Quaternion<Scalar>(ToRotationMatrixZYX());
  }
};

template <typename Scalar>
class RotationMatrix : public Eigen::Matrix<Scalar, 3, 3> {
 public:
  RotationMatrix() : Eigen::Matrix<Scalar, 3, 3>() {
    (*this) << 1, 0, 0, 0, 1, 0, 0, 0, 1;
  }

  RotationMatrix(Scalar r00, Scalar r01, Scalar r02, Scalar r10, Scalar r11,
                 Scalar r12, Scalar r20, Scalar r21, Scalar r22)
      : Eigen::Matrix<Scalar, 3, 3>() {
    (*this) << r00, r01, r02, r10, r11, r12, r20, r21, r22;
  }

  RotationMatrix(const Eigen::Matrix<Scalar, 3, 3> &R)
      : Eigen::Matrix<Scalar, 3, 3>{R} {}

  RotationMatrix(const Eigen::Quaternion<Scalar> &q)
      : Eigen::Matrix<Scalar, 3, 3>{q.ToRotationMatrix()} {}

  RotationMatrix(const Quaternion<Scalar> &q) { *this = q.ToRotationMatrix(); }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  RotationMatrix(const T (&data)[9]) : Eigen::Matrix<Scalar, 3, 3>() {
    (*this) << data[0], data[1], data[2], data[3], data[4], data[5], data[6],
        data[7], data[8];
  }

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  RotationMatrix(const T (&data)[3][3]) : Eigen::Matrix<Scalar, 3, 3>() {
    (*this) << data[0][0], data[0][1], data[0][2], data[1][0], data[1][1],
        data[1][2], data[2][0], data[2][1], data[2][2];
  }

  Eigen::Matrix<Scalar, 3, 3> operator-() const { return this->transpose(); }

  RotationMatrix &operator=(const RotationMatrix &R) {
    if (this != &R) {
      memcpy(this->data(), R.data(), 9 * sizeof(Scalar));
    }
    return *this;
  }

  RotationMatrix &operator=(const Eigen::Matrix<Scalar, 3, 3> &R) {
    memcpy(this->data(), R.data(), 9 * sizeof(Scalar));
    return *this;
  }

  RotationMatrix &operator=(const Eigen::Quaternion<Scalar> &q) {
    *this = q.ToRotationMatrix();
    return *this;
  }

  RotationMatrix &operator=(const Quaternion<Scalar> &q) {
    *this = q.ToRotationMatrix();
    return *this;
  }

  Position<Scalar> operator*(const Position<Scalar> &p) const {
    return Position<Scalar>((*this) * Eigen::Matrix<Scalar, 3, 1>(p));
  }

  Eigen::Matrix<Scalar, 3, 1> operator*(
      const Eigen::Matrix<Scalar, 3, 1> &p) const {
    return static_cast<Eigen::Matrix<Scalar, 3, 3>>(*this) * p;
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngle() const { return ToEulerAngleZYX(); }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleZYX() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar roll = std::atan2(r(2, 1), r(2, 2));
    Scalar yaw = std::atan2(r(1, 0), r(0, 0));
    Scalar pitch = std::asin(-r(2, 0));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleXZY() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar roll = std::atan2(r(2, 1), r(1, 1));
    Scalar yaw = std::asin(-r(0, 1));
    Scalar pitch = std::atan2(r(0, 2), r(0, 0));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleYZX() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar pitch = std::atan2(-r(2, 0), r(0, 0));
    Scalar yaw = std::asin(r(1, 0));
    Scalar roll = std::atan2(-r(1, 2), r(1, 1));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleYXZ() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar pitch = std::atan2(r(0, 2), r(2, 2));
    Scalar roll = std::asin(-r(1, 2));
    Scalar yaw = std::atan2(r(1, 0), r(1, 1));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleZXY() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar roll = std::asin(r(2, 1));
    Scalar yaw = std::atan2(r(1, 1), -r(0, 1));
    Scalar pitch = std::atan2(-r(2, 0), r(2, 2));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleXYZ() const {
    const Eigen::Matrix<Scalar, 3, 3> &r = (*this);

    Scalar yaw = std::atan2(-r(0, 1), r(0, 0));
    Scalar pitch = std::asin(r(0, 2));
    Scalar roll = std::atan2(-r(1, 2), r(2, 2));

    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }
};

template <typename Scalar>
class Quaternion : public Eigen::Quaternion<Scalar> {
 public:
  Quaternion() : Eigen::Quaternion<Scalar>(1, 0, 0, 0) {}

  Quaternion(Scalar w, Scalar x, Scalar y, Scalar z)
      : Eigen::Quaternion<Scalar>(w, x, y, z) {}

  Quaternion(const Eigen::Quaternion<Scalar> &q)
      : Eigen::Quaternion<Scalar>(q) {}

  Quaternion(const RotationMatrix<Scalar> &R)
      : Eigen::Quaternion<Scalar>(Eigen::Quaternion<Scalar>(
            static_cast<Eigen::Matrix<Scalar, 3, 3>>(R))) {}

  Quaternion(const Eigen::Matrix<Scalar, 3, 3> R)
      : Eigen::Quaternion<Scalar>(Eigen::Quaternion<Scalar>(
            static_cast<Eigen::Matrix<Scalar, 3, 3>>(R))) {}

  template <typename T, std::enable_if_t<std::is_same<T, Scalar>::value ||
                                             std::is_same<T, float>::value ||
                                             std::is_same<T, double>::value,
                                         int> = 0>
  Quaternion(const T (&data)[4]) : Eigen::Quaternion<Scalar>(data) {}

  Scalar operator()(int i) const {
    switch (i) {
      case 3:
        return this->w();
      case 0:
        return this->x();
      case 1:
        return this->y();
      case 2:
        return this->z();
      default:
        return 0;
    }
  }

  Quaternion &operator=(const Eigen::Quaternion<Scalar> &q) {
    *this = Quaternion(q);
    return *this;
  }

  Quaternion operator-() const { return Quaternion((*this).conjugate()); }

  Quaternion operator+(const Quaternion &q) const {
    return Quaternion(this->w() + q.w(), this->x() + q.x(), this->y() + q.y(),
                      this->z() + q.z());
  }

  Quaternion operator+(const Eigen::Quaternion<Scalar> &q) const {
    return Quaternion(this->w() + q.w(), this->x() + q.x(), this->y() + q.y(),
                      this->z() + q.z());
  }

  Quaternion operator-(const Quaternion &q) const {
    return Quaternion(this->w() - q.w(), this->x() - q.x(), this->y() - q.y(),
                      this->z() - q.z());
  }

  Quaternion operator-(const Eigen::Quaternion<Scalar> &q) const {
    return Quaternion(this->w() - q.w(), this->x() - q.x(), this->y() - q.y(),
                      this->z() - q.z());
  }

  Quaternion operator*(const Quaternion &q) const {
    return Eigen::Quaternion<Scalar>(*this) * Eigen::Quaternion<Scalar>(q);
  }

  Quaternion operator*(const Eigen::Quaternion<Scalar> &q) const {
    return Eigen::Quaternion<Scalar>(*this) * q;
  }

  Quaternion operator/(const Quaternion &q) const { return (*this) * (-q); }

  Quaternion operator/(const Eigen::Quaternion<Scalar> &q) const {
    return (*this) * (-q);
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

  Eigen::Matrix<Scalar, 3, 1> operator*(
      const Eigen::Matrix<Scalar, 3, 1> &p) const {
    return Eigen::Quaternion<Scalar>(*this) * p;
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngle() const { return ToEulerAngleZYX(); }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleYZX() const {
    Scalar roll =
        std::atan2(2 * this->w() * this->x() - 2 * this->y() * this->z(),
                   1 - 2 * (this->x() * this->x() + this->z() * this->z()));
    Scalar pitch =
        std::atan2(2 * this->w() * this->y() - 2 * this->x() * this->z(),
                   1 - 2 * (this->y() * this->y() + this->z() * this->z()));
    Scalar yaw = std::asin(2 * (this->w() * this->z() + this->x() * this->y()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleZYX() const {
    Scalar yaw =
        std::atan2(2 * (this->w() * this->z() + this->x() * this->y()),
                   1 - 2 * (this->z() * this->z() + this->y() * this->y()));
    Scalar pitch =
        std::asin(2 * (this->w() * this->y() - this->x() * this->z()));
    Scalar roll =
        std::atan2(2 * (this->w() * this->x() + this->y() * this->z()),
                   1 - 2 * (this->y() * this->y() + this->x() * this->x()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleYXZ() const {
    Scalar roll =
        std::asin(2 * (this->w() * this->x() - this->y() * this->z()));
    Scalar yaw =
        std::atan2(2 * (this->w() * this->z() + this->x() * this->y()),
                   1 - 2 * (this->z() * this->z() + this->x() * this->x()));
    Scalar pitch =
        std::atan2(2 * (this->x() * this->z() + this->w() * this->y()),
                   1 - 2 * (this->y() * this->y() + this->x() * this->x()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleZXY() const {
    Scalar pitch =
        std::atan2(-2 * (this->x() * this->z() - this->w() * this->y()),
                   1 - 2 * (this->y() * this->y() + this->x() * this->x()));
    Scalar roll =
        std::asin(2 * (this->w() * this->x() + this->y() * this->z()));
    Scalar yaw =
        std::atan2(-2 * (this->x() * this->y() - this->w() * this->z()),
                   1 - 2 * (this->z() * this->z() + this->x() * this->x()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleXZY() const {
    Scalar pitch =
        std::atan2(2 * (this->w() * this->y() + this->x() * this->z()),
                   1 - 2 * (this->z() * this->z() + this->y() * this->y()));
    Scalar yaw = std::asin(2 * (this->w() * this->z() - this->x() * this->y()));
    Scalar roll =
        std::atan2(2 * (this->w() * this->x() + this->y() * this->z()),
                   1 - 2 * (this->z() * this->z() + this->x() * this->x()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 1> ToEulerAngleXYZ() const {
    Scalar yaw =
        std::atan2(-2 * (this->x() * this->y() - this->w() * this->z()),
                   1 - 2 * (this->z() * this->z() + this->y() * this->y()));
    Scalar pitch =
        std::asin(2 * (this->w() * this->y() + this->x() * this->z()));
    Scalar roll =
        std::atan2(-2 * (this->y() * this->z() - this->w() * this->x()),
                   1 - 2 * (this->y() * this->y() + this->x() * this->x()));
    return Eigen::Matrix<Scalar, 3, 1>(roll, pitch, yaw);
  }

  Eigen::Matrix<Scalar, 3, 3> ToRotationMatrix() const {
    return Eigen::Matrix<Scalar, 3, 3>(*this);
  }
};

template <typename Scalar = LIBXR_DEFAULT_SCALAR>
class Transform {
 public:
  Quaternion<Scalar> rotation;
  Position<Scalar> translation;

  Transform() = default;

  Transform(const Quaternion<Scalar> &rotation,
            const Position<Scalar> &translation)
      : rotation(rotation), translation(translation) {}

  Transform &operator=(const Quaternion<Scalar> &q) {
    rotation = q;
    return *this;
  }

  Transform &operator=(const Eigen::AngleAxis<Scalar> &a) {
    rotation = a;
    return *this;
  }

  Transform &operator=(const Position<Scalar> &p) {
    translation = p;
    return *this;
  }

  Transform operator+(const Transform &t) const {
    return Transform(
        Eigen::Quaternion<Scalar>(rotation * t.rotation),
        Eigen::Matrix<Scalar, 3, 1>(translation + rotation * t.translation));
  }

  Transform operator-(const Transform &t) const {
    return Transform(rotation / t.rotation, translation - t.translation);
  }
};
}  // namespace LibXR