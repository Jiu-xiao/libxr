/**
 * @file test.hpp
 * @brief 测试用浮点比较辅助 / Floating-point comparison helper for tests.
 */

#pragma once

#include <cmath>

inline bool equal(double left, double right) { return std::abs(left - right) < 1e-6; }
