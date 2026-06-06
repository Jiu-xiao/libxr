/**
 * @file test_cb.cpp
 * @brief `LibXR::Callback` 绑定与重入行为测试。 `LibXR::Callback` binding and reentry behavior tests.
 *
 * 测试项目 / Test items:
 * 1. 空回调默认行为。 Empty callback: verify the default-constructed callback reports empty and accepts no-op dispatch.
 * 2. 直接回调的递归重入路径。 Direct callback recursion: verify unguarded callbacks reenter immediately and preserve the runtime ISR flag.
 * 3. Guarded 回调的递归压平行为。 Guarded callback flattening: verify `CreateGuarded()` serializes self-recursive dispatch into one-depth replay.
 * 4. Lambda 绑定与上下文透传。 Lambda binding: verify callable-object binding still forwards the bound context and runtime ISR flag correctly.
 *
 * 测试原理 / Test principles:
 * 1. 直接驱动公开回调包装器，而不是借用别的子系统间接验证回调。 Drive the public callback wrapper directly instead of going through another subsystem, so the test isolates callback semantics themselves.
 * 2. 同时记录值顺序和调用深度，因为这个模块的风险点就在重入语义。 Record both payload order and call depth, because this module's correctness depends on reentry behavior as much as value forwarding.
 */
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
      if (self->runtime_in_isr)
      {
        self->cb.Run(true, 2);
      }
      else
      {
        self->cb.Run(false, 2);
      }
    }

    self->depth--;
  }
};

struct GuardedCreationProbe
{
  LibXR::Callback<int> cb;
  bool runtime_in_isr = true;
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

/**
 * @brief 测试入口函数 `test_cb`。 Test entry function `test_cb`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_cb()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  {
    LibXR::Callback<int> empty_cb;
    ASSERT(empty_cb.Empty());
    empty_cb.Run(false, 1);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.cb.Run(probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(probe.runtime_in_isr, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = true;
    probe.cb.Run(true, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == true);
    ASSERT(probe.seen_in_isr[1] == true);
    ASSERT(probe.max_depth == 2);
  }

  {
    DirectCallbackProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(false, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 2);
  }

  {
    GuardedCreationProbe probe;
    probe.runtime_in_isr = false;
    probe.cb.Run(false, 1);
    ASSERT(probe.seen_count == 2);
    ASSERT(probe.seen[0] == 1);
    ASSERT(probe.seen[1] == 2);
    ASSERT(probe.seen_in_isr[0] == false);
    ASSERT(probe.seen_in_isr[1] == false);
    ASSERT(probe.max_depth == 1);
  }

  {
    LambdaCreationProbe probe;
    probe.cb.Run(true, 7);
    ASSERT(probe.seen_value == 7);
    ASSERT(probe.seen_in_isr == true);
  }
}
