/**
 * @file test_transform_rotation.cpp
 * @brief transform 旋转互操作子测试。 Split test unit for transform rotation interoperability.
 */
#include "transform_test_common.hpp"

/**
 * @brief 测试项函数 `RunTransformRotationInteropTests`。 Test-item function `RunTransformRotationInteropTests`.
 * @details 测试内容：执行当前分组里的 transform 子场景。 Execute the grouped transform sub-scenarios for this split file.
 *          测试原理：把构造/互操作/欧拉顺序三个语义维度拆开，降低单文件阅读压力。 Split construction/interoperability/Euler-order semantics into separate files to reduce single-file reading load.
 */
void RunTransformRotationInteropTests()
{
  TransformTestState state{};
  auto& quat = state.quat;
  auto& quat_new = state.quat_new;
  auto& rot_new = state.rot_new;
  quat = state.eulr.ToQuaternion();

  /* Rotation */
  quat_new = quat;
  quat_new = quat - quat_new;
  quat_new = quat + quat_new;
  ASSERT(equal(quat_new(0), quat(0)) && equal(quat_new(1), quat(1)) &&
         equal(quat_new(2), quat(2)) && equal(quat_new(3), quat(3)));

  Eigen::Quaternion<double> eigen_quat =
      LibXR::EulerAngle<double>(LibXR::PI / 8, -LibXR::PI / 9, LibXR::PI / 7)
          .ToQuaternion();
  LibXR::RotationMatrix rot_from_eigen_quat(eigen_quat);
  rot_new = eigen_quat;
  const Eigen::Matrix3d eigen_rot = eigen_quat.toRotationMatrix();
  ASSERT(equal(rot_from_eigen_quat(0, 0), eigen_rot(0, 0)) &&
         equal(rot_from_eigen_quat(0, 1), eigen_rot(0, 1)) &&
         equal(rot_from_eigen_quat(0, 2), eigen_rot(0, 2)) &&
         equal(rot_from_eigen_quat(1, 0), eigen_rot(1, 0)) &&
         equal(rot_from_eigen_quat(1, 1), eigen_rot(1, 1)) &&
         equal(rot_from_eigen_quat(1, 2), eigen_rot(1, 2)) &&
         equal(rot_from_eigen_quat(2, 0), eigen_rot(2, 0)) &&
         equal(rot_from_eigen_quat(2, 1), eigen_rot(2, 1)) &&
         equal(rot_from_eigen_quat(2, 2), eigen_rot(2, 2)));
  ASSERT(equal(rot_new(0, 0), eigen_rot(0, 0)) && equal(rot_new(0, 1), eigen_rot(0, 1)) &&
         equal(rot_new(0, 2), eigen_rot(0, 2)) && equal(rot_new(1, 0), eigen_rot(1, 0)) &&
         equal(rot_new(1, 1), eigen_rot(1, 1)) && equal(rot_new(1, 2), eigen_rot(1, 2)) &&
         equal(rot_new(2, 0), eigen_rot(2, 0)) && equal(rot_new(2, 1), eigen_rot(2, 1)) &&
         equal(rot_new(2, 2), eigen_rot(2, 2)));
  quat_new = quat / eigen_quat;
  Eigen::Quaternion<double> eigen_div =
      Eigen::Quaternion<double>(quat) * eigen_quat.conjugate();
  ASSERT(equal(quat_new.w(), eigen_div.w()) && equal(quat_new.x(), eigen_div.x()) &&
         equal(quat_new.y(), eigen_div.y()) && equal(quat_new.z(), eigen_div.z()));


}
