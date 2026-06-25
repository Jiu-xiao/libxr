/**
 * @file test_transform_construction.cpp
 * @brief transform 构造与基础代数子测试。 Split test unit for transform construction and basic algebra.
 */
#include "transform_test_common.hpp"

/**
 * @brief 测试项函数 `RunTransformConstructionTests`。 Test-item function `RunTransformConstructionTests`.
 * @details 测试内容：执行当前分组里的 transform 子场景。 Execute the grouped transform sub-scenarios for this split file.
 *          测试原理：把构造/互操作/欧拉顺序三个语义维度拆开，降低单文件阅读压力。 Split construction/interoperability/Euler-order semantics into separate files to reduce single-file reading load.
 */
void RunTransformConstructionTests()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TransformTestState state{};
  auto& pos = state.pos;
  auto& pos_new = state.pos_new;
  auto& eulr = state.eulr;
  auto& rot = state.rot;
  auto& rot_new = state.rot_new;
  auto& quat = state.quat;
  auto& quat_new = state.quat_new;
  LibXR::Quaternion quat_from_array(state.quat_wxyz);
  ASSERT(equal(quat_from_array.w(), state.quat_wxyz[0]) &&
         equal(quat_from_array.x(), state.quat_wxyz[1]) &&
         equal(quat_from_array.y(), state.quat_wxyz[2]) &&
         equal(quat_from_array.z(), state.quat_wxyz[3]));
  ASSERT(equal(quat_from_array(0), state.quat_wxyz[1]) &&
         equal(quat_from_array(1), state.quat_wxyz[2]) &&
         equal(quat_from_array(2), state.quat_wxyz[3]) &&
         equal(quat_from_array(3), state.quat_wxyz[0]));

  LibXR::RotationMatrix rot_from_array(state.rot_row_major);
  LibXR::RotationMatrix rot_from_2d_array(state.rot_row_major_2d);
  ASSERT(equal(rot_from_array(0, 0), 1.0) && equal(rot_from_array(0, 1), 0.0) &&
         equal(rot_from_array(0, 2), 0.0) && equal(rot_from_array(1, 0), 0.0) &&
         equal(rot_from_array(1, 1), 0.0) && equal(rot_from_array(1, 2), -1.0) &&
         equal(rot_from_array(2, 0), 0.0) && equal(rot_from_array(2, 1), 1.0) &&
         equal(rot_from_array(2, 2), 0.0));
  ASSERT(equal(rot_from_2d_array(0, 0), rot_from_array(0, 0)) &&
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
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new /= quat;
  pos_new *= rot;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new = (pos - pos_new) * 2.;
  pos_new *= 2;
  pos_new /= 4;
  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new = pos + pos_new;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));

  pos_new -= pos;

  ASSERT(equal(pos_new(0), 0.) && equal(pos_new(1), 0.) && equal(pos_new(2), 0.));

  pos_new += pos;
  ASSERT(equal(pos_new(0), pos(0)) && equal(pos_new(1), pos(1)) &&
         equal(pos_new(2), pos(2)));


}
