/**
 * @file test_transform.cpp
 * @brief 坐标与旋转类型测试 / Position and rotation type tests.
 *
 * 检查数组构造、位置运算和旋转往返，以及四元数与 Eigen 类型的互操作。
 * Check array construction, position arithmetic, rotation round trips and Eigen
 * quaternion interoperability.
 */

#include "test_assert.hpp"
#include "transform_test_common.hpp"

void RunTransformConstructionTests()
{
  TransformTestState state{};
  auto& pos = state.pos;
  auto& pos_new = state.pos_new;
  auto& eulr = state.eulr;
  auto& rot = state.rot;
  auto& rot_new = state.rot_new;
  auto& quat = state.quat;
  auto& quat_new = state.quat_new;
  LibXR::Quaternion quat_from_array(state.quat_wxyz);
  TEST_ASSERT(equal(quat_from_array.w(), state.quat_wxyz[0]) &&
              equal(quat_from_array.x(), state.quat_wxyz[1]) &&
              equal(quat_from_array.y(), state.quat_wxyz[2]) &&
              equal(quat_from_array.z(), state.quat_wxyz[3]));
  TEST_ASSERT(equal(quat_from_array(0), state.quat_wxyz[1]) &&
              equal(quat_from_array(1), state.quat_wxyz[2]) &&
              equal(quat_from_array(2), state.quat_wxyz[3]) &&
              equal(quat_from_array(3), state.quat_wxyz[0]));

  LibXR::RotationMatrix rot_from_array(state.rot_row_major);
  LibXR::RotationMatrix rot_from_2d_array(state.rot_row_major_2d);
  TEST_ASSERT(equal(rot_from_array(0, 0), 1.0) && equal(rot_from_array(0, 1), 0.0) &&
              equal(rot_from_array(0, 2), 0.0) && equal(rot_from_array(1, 0), 0.0) &&
              equal(rot_from_array(1, 1), 0.0) && equal(rot_from_array(1, 2), -1.0) &&
              equal(rot_from_array(2, 0), 0.0) && equal(rot_from_array(2, 1), 1.0) &&
              equal(rot_from_array(2, 2), 0.0));
  TEST_ASSERT(equal(rot_from_2d_array(0, 0), rot_from_array(0, 0)) &&
              equal(rot_from_2d_array(0, 1), rot_from_array(0, 1)) &&
              equal(rot_from_2d_array(0, 2), rot_from_array(0, 2)) &&
              equal(rot_from_2d_array(1, 0), rot_from_array(1, 0)) &&
              equal(rot_from_2d_array(1, 1), rot_from_array(1, 1)) &&
              equal(rot_from_2d_array(1, 2), rot_from_array(1, 2)) &&
              equal(rot_from_2d_array(2, 0), rot_from_array(2, 0)) &&
              equal(rot_from_2d_array(2, 1), rot_from_array(2, 1)) &&
              equal(rot_from_2d_array(2, 2), rot_from_array(2, 2)));

  /* Position */
  rot = eulr.ToRotationMatrix();
  quat = LibXR::Quaternion(rot);

  pos_new = pos * quat;
  quat_new = pos_new / pos;
  rot_new = quat_new.ToRotationMatrix();
  pos_new = pos_new / rot_new;
  TEST_ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
              equal(pos_new(2), pos(2)));

  pos_new /= quat;
  pos_new *= rot;
  TEST_ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
              equal(pos_new(2), pos(2)));

  pos_new = (pos - pos_new) * 2.;
  pos_new *= 2;
  pos_new /= 4;
  TEST_ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new = pos + pos_new;
  TEST_ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
              equal(pos_new(2), pos(2)));

  pos_new -= pos;

  TEST_ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new += pos;
  TEST_ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
              equal(pos_new(2), pos(2)));
}

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
  TEST_ASSERT(equal(quat_new(0), quat(0)) && equal(quat_new(1), quat(1)) &&
              equal(quat_new(2), quat(2)) && equal(quat_new(3), quat(3)));

  Eigen::Quaternion<double> eigen_quat =
      LibXR::EulerAngle<double>(LibXR::PI / 8, -LibXR::PI / 9, LibXR::PI / 7)
          .ToQuaternion();
  LibXR::RotationMatrix rot_from_eigen_quat(eigen_quat);
  rot_new = eigen_quat;
  const Eigen::Matrix3d eigen_rot = eigen_quat.toRotationMatrix();
  TEST_ASSERT(equal(rot_from_eigen_quat(0, 0), eigen_rot(0, 0)) &&
              equal(rot_from_eigen_quat(0, 1), eigen_rot(0, 1)) &&
              equal(rot_from_eigen_quat(0, 2), eigen_rot(0, 2)) &&
              equal(rot_from_eigen_quat(1, 0), eigen_rot(1, 0)) &&
              equal(rot_from_eigen_quat(1, 1), eigen_rot(1, 1)) &&
              equal(rot_from_eigen_quat(1, 2), eigen_rot(1, 2)) &&
              equal(rot_from_eigen_quat(2, 0), eigen_rot(2, 0)) &&
              equal(rot_from_eigen_quat(2, 1), eigen_rot(2, 1)) &&
              equal(rot_from_eigen_quat(2, 2), eigen_rot(2, 2)));
  TEST_ASSERT(
      equal(rot_new(0, 0), eigen_rot(0, 0)) && equal(rot_new(0, 1), eigen_rot(0, 1)) &&
      equal(rot_new(0, 2), eigen_rot(0, 2)) && equal(rot_new(1, 0), eigen_rot(1, 0)) &&
      equal(rot_new(1, 1), eigen_rot(1, 1)) && equal(rot_new(1, 2), eigen_rot(1, 2)) &&
      equal(rot_new(2, 0), eigen_rot(2, 0)) && equal(rot_new(2, 1), eigen_rot(2, 1)) &&
      equal(rot_new(2, 2), eigen_rot(2, 2)));
  quat_new = quat / eigen_quat;
  Eigen::Quaternion<double> eigen_div =
      Eigen::Quaternion<double>(quat) * eigen_quat.conjugate();
  TEST_ASSERT(equal(quat_new.w(), eigen_div.w()) && equal(quat_new.x(), eigen_div.x()) &&
              equal(quat_new.y(), eigen_div.y()) && equal(quat_new.z(), eigen_div.z()));
}

void test_transform()
{
  RunTransformConstructionTests();
  RunTransformRotationInteropTests();
  RunTransformEulerOrderTests();
}
