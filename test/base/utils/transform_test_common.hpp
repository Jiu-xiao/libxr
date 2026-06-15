/**
 * @file transform_test_common.hpp
 * @brief transform 测试共用包含入口。 Shared include entry for transform tests.
 */
#pragma once

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

struct TransformTestState
{
  LibXR::Position<> pos{1., 8., 0.3};
  LibXR::Position<> pos_new{};
  LibXR::EulerAngle<> eulr{LibXR::PI / 12, LibXR::PI / 6, LibXR::PI / 4};
  LibXR::EulerAngle<> eulr_new{};
  LibXR::RotationMatrix<> rot{};
  LibXR::RotationMatrix<> rot_new{};
  LibXR::Quaternion<> quat{};
  LibXR::Quaternion<> quat_new{};
  double quat_wxyz[4] = {0.1, 0.2, 0.3, 0.4};
  double rot_row_major[9] = {1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0};
  double rot_row_major_2d[3][3] = {{1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}};
};

void RunTransformConstructionTests();
void RunTransformRotationInteropTests();
void RunTransformEulerOrderTests();
