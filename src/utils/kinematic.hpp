#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>

#include "Eigen/src/Core/Matrix.h"
#include "Eigen/src/Geometry/Quaternion.h"
#include "inertia.hpp"
#include "list.hpp"
#include "transform.hpp"

namespace LibXR {

using DefaultScalar = LIBXR_DEFAULT_SCALAR;

namespace Kinematic {
template <typename Scalar = DefaultScalar>
class Joint;
template <typename Scalar = DefaultScalar>
class Object;
template <typename Scalar = DefaultScalar>
class EndPoint;
template <typename Scalar = DefaultScalar>
class StartPoint;

template <typename Scalar>
class Joint {
 public:
  typedef struct Param {
    Transform<Scalar> parent2this;
    Transform<Scalar> this2child;
    Axis<Scalar> axis;
    Scalar ik_mult;
  } Param;

  typedef struct Runtime {
    Eigen::AngleAxis<Scalar> state_angle;
    Eigen::AngleAxis<Scalar> target_angle;

    Eigen::Matrix<Scalar, 3, 3> inertia;

    Axis<Scalar> state_axis;
    Axis<Scalar> target_axis;

    Transform<Scalar> state;
    Transform<Scalar> target;
  } Runtime;

  Runtime runtime_;

  Object<Scalar> *parent = nullptr;
  Object<Scalar> *child = nullptr;
  Param param_;

  Joint(Axis<Scalar> axis, Object<Scalar> *parent,
        Transform<Scalar> &parent2this, Object<Scalar> *child,
        Transform<Scalar> &this2child)
      : parent(parent),
        child(child),
        param_({parent2this, this2child, axis, 1.0}) {
    runtime_.inertia = Eigen::Matrix<Scalar, 3, 3>::Zero();
    auto link = new typename Object<Scalar>::Link(this);
    parent->joints.Add(*link);
    child->parent = this;
  }

  void SetState(Scalar state) {
    if (state > M_PI) {
      state -= 2 * M_PI;
    }
    if (state < -M_PI) {
      state += 2 * M_PI;
    }
    runtime_.state_angle.angle() = state;
    runtime_.state_angle.axis() = param_.axis;
  }

  void SetTarget(Scalar target) {
    if (target > M_PI) {
      target -= 2 * M_PI;
    }
    if (target < -M_PI) {
      target += 2 * M_PI;
    }
    runtime_.target_angle.angle() = target;
    runtime_.target_angle.axis() = param_.axis;
  }

  void SetBackwardMult(Scalar mult) { param_.ik_mult = mult; }
};

template <typename Scalar>
class Object {
 public:
  typedef List::Node<Joint<Scalar> *> Link;

  typedef struct {
    Inertia<Scalar> inertia;
  } Param;

  typedef struct {
    Transform<Scalar> state;
    Transform<Scalar> target;
  } Runtime;

  List joints;

  Joint<Scalar> *parent = nullptr;

  Param param_;
  Runtime runtime_;

  Object(Inertia<Scalar> &inertia) : param_({inertia}) {}

  void SetPosition(const Position<Scalar> &pos) { runtime_.state = pos; }

  void SetQuaternion(const Quaternion<Scalar> &quat) { runtime_.state = quat; }

  void CalcBackward() {}
};

template <typename Scalar>
class EndPoint : public Object<Scalar> {
 private:
  Eigen::Matrix<Scalar, 6, Eigen::Dynamic> *jacobian_matrix_ = nullptr;
  Eigen::Matrix<Scalar, Eigen::Dynamic, 1> *delta_theta_ = nullptr;
  Eigen::Matrix<Scalar, 6, 1> err_weight_ =
      Eigen::Matrix<Scalar, 6, 1>::Constant(1);
  int joint_num_ = 0;

  Quaternion<Scalar> target_quat_;
  Position<Scalar> target_pos_;
  Scalar max_angular_velocity_ = -1.0;
  Scalar max_line_velocity_ = -1.0;

 public:
  EndPoint(Inertia<Scalar> &inertia) : Object<Scalar>(inertia) {}

  void SetTargetQuaternion(const Quaternion<Scalar> &quat) {
    target_quat_ = quat;
  }

  void SetTargetPosition(const Position<Scalar> &pos) { target_pos_ = pos; }

  void SetErrorWeight(const Eigen::Matrix<Scalar, 6, 1> &weight) {
    err_weight_ = weight;
  }

  void SetMaxAngularVelocity(Scalar velocity) {
    max_angular_velocity_ = velocity;
  }

  Eigen::Matrix<Scalar, 3, 1> GetPositionError() {
    return target_pos_ - Object<Scalar>::runtime_.target.translation;
  }

  Eigen::Quaternion<Scalar> GetQuaternionError() {
    return Object<Scalar>::runtime_.target.rotation / target_quat_;
  }

  void SetMaxLineVelocity(Scalar velocity) { max_line_velocity_ = velocity; }

  Eigen::Matrix<Scalar, 6, 1> CalcBackward(Scalar dt, int max_step = 10,
                                           Scalar max_err = 1e-3,
                                           Scalar step_size = 1.0) {
    Eigen::Matrix<Scalar, 6, 1> error;

    /* Initialize */
    if (jacobian_matrix_ == nullptr) {
      Object<Scalar> *tmp = this;
      for (int i = 0;; i++) {
        if (tmp->parent == nullptr) {
          joint_num_ = i;
          break;
        }
        tmp = tmp->parent->parent;
      }

      jacobian_matrix_ =
          new Eigen::Matrix<Scalar, 6, Eigen::Dynamic>(6, joint_num_);

      delta_theta_ = new Eigen::Matrix<Scalar, Eigen::Dynamic, 1>(joint_num_);
    }

    /* Apply Limition */
    Position<Scalar> target_pos = target_pos_;
    Quaternion<Scalar> target_quat = target_quat_;
    if (max_line_velocity_ > 0 && max_angular_velocity_ > 0) {
      Scalar max_pos_delta = max_angular_velocity_ * dt;
      Scalar max_angle_delta = max_line_velocity_ * dt;

      Position<Scalar> pos_err =
          Position<Scalar>(target_pos_ - this->runtime_.target.translation);
      Eigen::AngleAxis<Scalar> angle_err(
          Quaternion<Scalar>(target_quat_ / this->runtime_.target.rotation));

      Scalar pos_err_norm = pos_err.norm();

      if (pos_err_norm > max_pos_delta) {
        pos_err = (max_pos_delta / pos_err_norm) * pos_err;
        target_pos = this->runtime_.target.translation + pos_err;
      } else {
        target_pos = target_pos_;
      }

      if (angle_err.angle() > max_angle_delta) {
        angle_err.angle() = max_angle_delta;
        target_quat = this->runtime_.target.rotation * angle_err;
      } else {
        target_quat = target_quat_;
      }
    }

    for (int step = 0; step < max_step; ++step) {
      /* Calculate Error */
      error.template head<3>() = target_pos - this->runtime_.target.translation;
      error.template tail<3>() =
          (Eigen::Quaternion<Scalar>(this->runtime_.target.rotation)
               .conjugate() *
           target_quat)
              .vec();

      error = err_weight_.array() * error.array();

      auto err_norm = error.norm();

      if (err_norm < max_err) {
        break;
      }

      /* Calculate Jacobian */
      do {
        Joint<Scalar> *joint = this->parent;
        for (int joint_index = 0; joint_index < joint_num_; joint_index++) {
          /* J = [translation[3], rotation[3]] */
          Eigen::Matrix<Scalar, 6, 1> d_transform;
          d_transform.template head<3>() = joint->runtime_.target_axis.cross(
              this->runtime_.target.translation -
              joint->runtime_.target.translation);

          d_transform.template tail<3>() = joint->runtime_.target_axis;

          jacobian_matrix_->col(joint_index) = d_transform;

          joint = joint->parent->parent;
        }
      } while (0);

      /* Calculate delta_theta: delta_theta = J^+ * error */
      *delta_theta_ =
          jacobian_matrix_->completeOrthogonalDecomposition().pseudoInverse() *
          error * step_size / std::sqrt(err_norm);

      /* Update Joint Angle */
      do {
        Joint<Scalar> *joint = this->parent;

        for (int joint_index = 0; joint_index < joint_num_; joint_index++) {
          Eigen::AngleAxis<Scalar> target_angle_axis_delta(
              (*delta_theta_)(joint_index), joint->runtime_.target_axis);

          target_angle_axis_delta =
              Quaternion<Scalar>(
                  Eigen::Quaternion<Scalar>(target_angle_axis_delta)) /
              joint->runtime_.target.rotation;

          joint->SetTarget(joint->runtime_.target_angle.angle() +
                           (*delta_theta_)(joint_index)*joint->param_.ik_mult);

          joint = joint->parent->parent;
        }
      } while (0);

      /* Recalculate Forward Kinematics */
      do {
        Joint<Scalar> *joint = this->parent;

        for (int joint_index = 0; joint_index < joint_num_; joint_index++) {
          if (joint_index == joint_num_ - 1) {
            auto start_point =
                reinterpret_cast<StartPoint<Scalar> *>(joint->parent);
            start_point->CalcTargetForward();
            break;
          }

          joint = joint->parent->parent;
        }
      } while (0);
    }

    return error;
  }
};

template <typename Scalar>
class StartPoint : public Object<Scalar> {
 public:
  CenterOfMass<Scalar> cog;

  StartPoint(Inertia<Scalar> &inertia) : Object<Scalar>(inertia) {}

  void CalcForward() {
    this->runtime_.target = this->runtime_.state;
    auto fun = [&](Joint<Scalar> *&joint) {
      return ForwardForeachFunLoop(joint, *this);
    };
    this->joints.template Foreach<Joint<Scalar> *>(fun);
  }

  void CalcTargetForward() {
    this->runtime_.target = this->runtime_.state;
    auto fun = [&](Joint<Scalar> *&joint) {
      return TargetForwardForeachFunLoop(joint, *this);
    };
    this->joints.template Foreach<Joint<Scalar> *>(fun);
  }

  void CalcInertia() {
    Joint<Scalar> *res = nullptr;
    auto fun = [&](Joint<Scalar> *&joint) {
      return InertiaForeachFunLoopStart(joint, res);
    };
    this->joints.template Foreach<Joint<Scalar> *>(fun);
  }

  void CalcCenterOfMass() {
    this->cog =
        CenterOfMass<Scalar>(this->param_.inertia, this->runtime_.state);
    this->joints.Foreach(CenterOfMassForeachFunLoop, *this);
  }

  static ErrorCode InertiaForeachFunLoopStart(Joint<Scalar> *&joint,
                                              Joint<Scalar> *&parent) {
    UNUSED(parent);
    joint->runtime_.inertia =
        joint->child->param_.inertia
            .Translate(joint->runtime_.state.translation -
                       joint->child->runtime_.state.translation)
            .Rotate(joint->child->runtime_.state.rotation /
                    joint->runtime_.state.rotation);

    joint->runtime_.inertia = Inertia<Scalar>::Rotate(
        joint->runtime_.inertia,
        Eigen::Quaternion<Scalar>(joint->runtime_.state_angle));

    auto fun_loop = [&](Joint<Scalar> *&child_joint) {
      return InertiaForeachFunLoop(child_joint, joint);
    };

    auto fun_start = [&](Joint<Scalar> *&child_joint) {
      return InertiaForeachFunLoopStart(child_joint, joint);
    };

    joint->child->joints.template Foreach<Joint<Scalar> *>(fun_loop);
    joint->child->joints.template Foreach<Joint<Scalar> *>(fun_start);

    return ErrorCode::OK;
  }

  static ErrorCode InertiaForeachFunLoop(Joint<Scalar> *&joint,
                                         Joint<Scalar> *&parent) {
    auto new_inertia = joint->child->param_.inertia
                           .Translate(parent->runtime_.state.translation -
                                      joint->child->runtime_.state.translation)
                           .Rotate(parent->runtime_.state.rotation /
                                   joint->runtime_.state.rotation);

    parent->runtime_.inertia = new_inertia + parent->runtime_.inertia;

    auto fun_loop = [&](Joint<Scalar> *&child_joint) {
      return InertiaForeachFunLoop(child_joint, joint);
    };

    joint->child->joints.template Foreach<Joint<Scalar> *>(fun_loop);

    return ErrorCode::OK;
  }

  static ErrorCode ForwardForeachFunLoop(Joint<Scalar> *&joint,
                                         StartPoint<Scalar> &start) {
    Transform<Scalar> t_joint(joint->parent->runtime_.state +
                              joint->param_.parent2this);

    joint->runtime_.state = t_joint;

    Transform<Scalar> t_child(
        joint->runtime_.state.rotation * joint->runtime_.state_angle,
        joint->runtime_.state.translation);

    t_child = t_child + joint->param_.this2child;

    joint->child->runtime_.state = t_child;
    joint->runtime_.state_axis =
        joint->runtime_.state.rotation * joint->param_.axis;
    joint->runtime_.target_axis = joint->runtime_.state_axis;

    joint->runtime_.target = joint->runtime_.state;
    joint->runtime_.target_angle = joint->runtime_.state_angle;
    joint->child->runtime_.target = joint->child->runtime_.state;

    auto fun = [&](Joint<Scalar> *&child_joint) {
      return ForwardForeachFunLoop(child_joint, start);
    };

    joint->child->joints.template Foreach<Joint<Scalar> *>(fun);

    return ErrorCode::OK;
  }

  static ErrorCode TargetForwardForeachFunLoop(Joint<Scalar> *&joint,
                                               StartPoint<Scalar> &start) {
    Transform<Scalar> t_joint(joint->parent->runtime_.target +
                              joint->param_.parent2this);

    joint->runtime_.target = t_joint;

    Transform<Scalar> t_child(
        joint->runtime_.target.rotation * joint->runtime_.target_angle,
        joint->runtime_.target.translation);

    t_child = t_child + joint->param_.this2child;
    joint->child->runtime_.target = t_child;

    joint->runtime_.target_axis =
        joint->runtime_.target.rotation * joint->param_.axis;

    auto fun = [&](Joint<Scalar> *&child_joint) {
      return TargetForwardForeachFunLoop(child_joint, start);
    };

    joint->child->joints.template Foreach<Joint<Scalar> *>(fun);

    return ErrorCode::OK;
  }

  static ErrorCode CenterOfMassForeachFunLoop(Joint<Scalar> *&joint,
                                              StartPoint<Scalar> &start) {
    CenterOfMass<Scalar> child_cog(joint->child->param_.inertia,
                                   joint->child->runtime_.state);

    start.cog += child_cog;

    joint->child->joints.Foreach(TargetForwardForeachFunLoop, start);

    return ErrorCode::OK;
  }
};

}  // namespace Kinematic

}  // namespace LibXR
