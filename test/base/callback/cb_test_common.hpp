/**
 * @file cb_test_common.hpp
 * @brief `LibXR::Callback` 测试共用探针。 Shared probes for `LibXR::Callback` tests.
 * @details 测试项目：
 *          1. 提供直接回调、guarded 回调和 lambda 绑定的探针结构。
 *          2. 统一记录值顺序、ISR 标记和重入深度。
 *          Test items:
 *          1. Provide probes for direct callbacks, guarded callbacks, and lambda bindings.
 *          2. Record value order, ISR flags, and reentry depth consistently.
 */
#pragma once

#include <array>

#include "libxr.hpp"
#include "test.hpp"

namespace
{
struct DirectCallbackProbe
{
  LibXR::Callback<int> cb;
  bool runtime_in_isr = true;
  bool trigger_reentry = true;
  std::array<int, 4> seen = {};
  std::array<bool, 4> seen_in_isr = {};
  int seen_count = 0;
  int depth = 0;
  int max_depth = 0;

  DirectCallbackProbe() : cb(LibXR::Callback<int>::Create(OnCallback, this)) {}

  /**
   * @brief 辅助函数 `OnCallback`。 Helper function `OnCallback`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  static void OnCallback(bool in_isr, DirectCallbackProbe* self, int value)
  {
    if (self->seen_count < static_cast<int>(self->seen.size()))
    {
      self->seen[static_cast<size_t>(self->seen_count++)] = value;
      self->seen_in_isr[static_cast<size_t>(self->seen_count - 1)] = in_isr;
    }

    self->depth++;
    if (self->depth > self->max_depth)
    {
      self->max_depth = self->depth;
    }

    if (self->trigger_reentry && value == 1)
    {
      self->trigger_reentry = false;
      self->cb.Run(self->runtime_in_isr, 2);
    }

    self->depth--;
  }
};

struct GuardedCreationProbe
{
  LibXR::Callback<int> cb;
  bool trigger_reentry = true;
  std::array<int, 4> seen = {};
  std::array<bool, 4> seen_in_isr = {};
  int seen_count = 0;
  int depth = 0;
  int max_depth = 0;

  GuardedCreationProbe() : cb(LibXR::Callback<int>::CreateGuarded(OnCallback, this)) {}

  /**
   * @brief 辅助函数 `OnCallback`。 Helper function `OnCallback`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  static void OnCallback(bool in_isr, GuardedCreationProbe* self, int value)
  {
    if (self->seen_count < static_cast<int>(self->seen.size()))
    {
      self->seen[static_cast<size_t>(self->seen_count++)] = value;
      self->seen_in_isr[static_cast<size_t>(self->seen_count - 1)] = in_isr;
    }

    self->depth++;
    if (self->depth > self->max_depth)
    {
      self->max_depth = self->depth;
    }

    if (self->trigger_reentry && value == 1)
    {
      self->trigger_reentry = false;
      self->cb.Run(in_isr, 2);
    }

    self->depth--;
  }
};

struct LambdaCreationProbe
{
  LibXR::Callback<int> cb;
  int seen_value = 0;
  bool seen_in_isr = false;

  LambdaCreationProbe()
      : cb(LibXR::Callback<int>::Create(
            [](bool in_isr, LambdaCreationProbe* self, int value)
            {
              self->seen_value = value;
              self->seen_in_isr = in_isr;
            },
            this))
  {
  }
};

}  // namespace
