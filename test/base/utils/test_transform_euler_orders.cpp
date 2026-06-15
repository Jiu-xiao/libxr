/**
 * @file test_transform_euler_orders.cpp
 * @brief transform 欧拉顺序 round-trip 子测试。 Split test unit for transform Euler-order round trips.
 */
#include "transform_test_common.hpp"

/**
 * @brief 测试项函数 `RunTransformEulerOrderTests`。 Test-item function `RunTransformEulerOrderTests`.
 * @details 测试内容：执行当前分组里的 transform 子场景。 Execute the grouped transform sub-scenarios for this split file.
 *          测试原理：把构造/互操作/欧拉顺序三个语义维度拆开，降低单文件阅读压力。 Split construction/interoperability/Euler-order semantics into separate files to reduce single-file reading load.
 */
void RunTransformEulerOrderTests()
{
  TransformTestState state{};
  auto& eulr = state.eulr;
  auto& eulr_new = state.eulr_new;
  auto& rot = state.rot;
  auto& rot_new = state.rot_new;
  auto& quat = state.quat;
  auto& quat_new = state.quat_new;

  /* ZYX Order */
  rot = eulr.ToRotationMatrixZYX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.5915064) &&
         equal(rot(0, 2), 0.5245190) && equal(rot(1, 0), 0.6123725) &&
         equal(rot(1, 1), 0.7745190) && equal(rot(1, 2), 0.1584937) &&
         equal(rot(2, 0), -0.5000000) && equal(rot(2, 1), 0.2241439) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionZYX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleZYX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* ZXY Order */
  rot = eulr.ToRotationMatrixZXY();
  ASSERT(equal(rot(0, 0), 0.5208661) && equal(rot(0, 1), -0.6830127) &&
         equal(rot(0, 2), 0.5120471) && equal(rot(1, 0), 0.7038788) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1950597) &&
         equal(rot(2, 0), -0.4829629) && equal(rot(2, 1), 0.2588190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionZXY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleZXY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YXZ Order */
  rot = eulr.ToRotationMatrixYXZ();
  ASSERT(equal(rot(0, 0), 0.7038788) && equal(rot(0, 1), -0.5208661) &&
         equal(rot(0, 2), 0.4829629) && equal(rot(1, 0), 0.6830127) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.2588190) &&
         equal(rot(2, 0), -0.1950597) && equal(rot(2, 1), 0.5120471) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionYXZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleYXZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XYZ Order */
  rot = eulr.ToRotationMatrixXYZ();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.6123725) &&
         equal(rot(0, 2), 0.5000000) && equal(rot(1, 0), 0.7745190) &&
         equal(rot(1, 1), 0.5915064) && equal(rot(1, 2), -0.2241439) &&
         equal(rot(2, 0), -0.1584937) && equal(rot(2, 1), 0.5245190) &&
         equal(rot(2, 2), 0.8365163));

  eulr_new = rot.ToEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionXYZ();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleXYZ();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* XZY Order */
  rot = eulr.ToRotationMatrixXZY();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.7071068) &&
         equal(rot(0, 2), 0.3535534) && equal(rot(1, 0), 0.7209159) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), 0.1173625) &&
         equal(rot(2, 0), -0.3244693) && equal(rot(2, 1), 0.1830127) &&
         equal(rot(2, 2), 0.9280227));

  eulr_new = rot.ToEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionXZY();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleXZY();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  /* YZX Order */
  rot = eulr.ToRotationMatrixYZX();
  ASSERT(equal(rot(0, 0), 0.6123725) && equal(rot(0, 1), -0.4620968) &&
         equal(rot(0, 2), 0.6414565) && equal(rot(1, 0), 0.7071068) &&
         equal(rot(1, 1), 0.6830127) && equal(rot(1, 2), -0.1830127) &&
         equal(rot(2, 0), -0.3535534) && equal(rot(2, 1), 0.5656502) &&
         equal(rot(2, 2), 0.7450100));

  eulr_new = rot.ToEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

  quat = LibXR::Quaternion(rot);
  quat_new = eulr.ToQuaternionYZX();
  ASSERT(equal(quat_new.w(), quat.w()) && equal(quat_new.x(), quat.x()) &&
         equal(quat_new.y(), quat.y()) && equal(quat_new.z(), quat.z()));

  rot_new = quat.ToRotationMatrix();
  ASSERT(equal(rot_new(0, 0), rot(0, 0)) && equal(rot_new(0, 1), rot(0, 1)) &&
         equal(rot_new(0, 2), rot(0, 2)) && equal(rot_new(1, 0), rot(1, 0)) &&
         equal(rot_new(1, 1), rot(1, 1)) && equal(rot_new(1, 2), rot(1, 2)) &&
         equal(rot_new(2, 0), rot(2, 0)) && equal(rot_new(2, 1), rot(2, 1)) &&
         equal(rot_new(2, 2), rot(2, 2)));

  eulr_new = quat.ToEulerAngleYZX();
  ASSERT(equal(eulr_new(0), eulr(0)) && equal(eulr_new(1), eulr(1)) &&
         equal(eulr_new(2), eulr(2)));

}
