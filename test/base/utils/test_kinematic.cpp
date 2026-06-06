/**
 * @file test_kinematic.cpp
 * @brief 前向/反向运动链求解测试。 Forward/backward kinematic chain solve test.
 *
 * 测试项目 / Test items:
 * 1. 两关节链的前向传播。 Forward propagation: build a start-point, intermediate object and end-point chain with two joints.
 * 2. 端点目标位姿的反向求解残差。 Backward solve: verify the endpoint target position/quaternion can be reached with small residual error after backward computation.
 *
 * 测试原理 / Test principles:
 * 1. 用具体两关节链而不是孤立公式，覆盖对象/关节在真实 API 上的组合行为。 Use a concrete two-joint chain instead of isolated algebra checks so the test covers object/joint composition on the real API surface.
 * 2. 以端点残差为准，而不是强行断言内部中间状态。 Assert residual error rather than exact internal state, because the public contract is geometric convergence at the endpoint.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_kinematic()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Inertia inertia_endpoint(1.0, 0.1, 0.1, 0.1, 0., 0., 0.);
  LibXR::Inertia inertia_midpoint(1.0, 0.1, 0.1, 0.1, 0., 0., 0.);
  LibXR::Inertia inertia_startpoint(1000., 100., 100., 100., 0., 0., 0.);

  LibXR::Position pos_startpoint(0., 0., 1.);
  LibXR::Quaternion quat_startpoint = LibXR::EulerAngle(0., 0., 0.).ToQuaternion();

  LibXR::Position pos_startpoint2joint(0., 0.0, 0.5);
  LibXR::Position pos_joint2midpoint(1.0, 0.0, 0.0);
  LibXR::Position pos_midpoint2joint(1.0, 0.0, 0.0);
  LibXR::Position pos_joint2endpoint(0.5, 0.0, 0.0);

  LibXR::Quaternion quat_startpoint2joint(1., 0., 0., 0.);
  LibXR::Quaternion quat_joint2midpoint(1., 0., 0., 0.);
  LibXR::Quaternion quat_midpoint2joint(1., 0., 0., 0.);
  LibXR::Quaternion quat_joint2endpoint(1., 0., 0., 0.);

  LibXR::Transform t_startpoint2joint(quat_startpoint2joint, pos_startpoint2joint);
  LibXR::Transform t_joint2midpoint(quat_joint2midpoint, pos_joint2midpoint);
  LibXR::Transform t_midpoint2joint(quat_midpoint2joint, pos_midpoint2joint);
  LibXR::Transform t_joint2endpoint(quat_joint2endpoint, pos_joint2endpoint);

  LibXR::Kinematic::EndPoint object_endpoint(inertia_endpoint);
  LibXR::Kinematic::Object object_midpoint(inertia_midpoint);
  LibXR::Kinematic::StartPoint object_startpoint(inertia_startpoint);

  object_startpoint.SetPosition(pos_startpoint);
  object_startpoint.SetQuaternion(quat_startpoint);

  LibXR::Kinematic::Joint joint_midpoint(LibXR::Axis<>::Y(), &object_startpoint,
                                         t_startpoint2joint, &object_midpoint,
                                         t_joint2midpoint);

  LibXR::Kinematic::Joint joint_endpoint(LibXR::Axis<>::Y(), &object_midpoint,
                                         t_midpoint2joint, &object_endpoint,
                                         t_joint2endpoint);

  joint_endpoint.SetState(0.);
  joint_midpoint.SetState(LibXR::PI / 2);

  LibXR::Quaternion target_quat(0.7071068, 0., -0.7071068, 0.);
  LibXR::Position target_pos(0., 0., 4.);

  object_endpoint.SetTargetQuaternion(target_quat);
  object_endpoint.SetTargetPosition(target_pos);

  object_startpoint.CalcForward();
  object_startpoint.CalcInertia();

  object_endpoint.CalcBackward(0, 1000, 0.01, 0.1);

  auto error_pos = object_endpoint.GetPositionError();
  auto error_quat = object_endpoint.GetQuaternionError();

  ASSERT(error_pos.norm() < 1e-3);
  ASSERT(std::abs(error_quat.x()) < 1e-2 && std::abs(error_quat.y()) < 1e-2 &&
         std::abs(error_quat.z()) < 1e-2 && std::abs(error_quat.w() - 1.0) < 1e-2);
}
