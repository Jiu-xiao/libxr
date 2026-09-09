/**
 * @file transform_test_common.hpp
 * @brief 坐标变换测试共用数据 / Shared data for transform tests.
 *
 * 提供固定位置、欧拉角和数组输入，以及各组变换测试的入口声明。
 * Provide fixed positions, angles, array inputs and declarations for the transform test
 * groups.
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
