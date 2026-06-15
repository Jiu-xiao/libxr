/**
 * @file test_cb_direct.cpp
 * @brief `LibXR::Callback` 空回调与直接重入场景子测试。 Split test unit for empty-callback and direct-reentry scenarios.
 * @details 测试项目：
 *          1. 默认空回调允许 no-op 运行。
 *          2. 非 guarded 直接回调在 ISR 与非 ISR 路径都会立即重入。
 *          Test items:
 *          1. Default empty callbacks allow no-op execution.
 *          2. Non-guarded direct callbacks reenter immediately on both ISR and non-ISR paths.
 */
#include "cb_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestEmptyAndDirectCallbacks`。 Test-item function `TestEmptyAndDirectCallbacks`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestEmptyAndDirectCallbacks()
{
  // 测试内容：验证空回调默认行为，以及直接回调的即刻重入和 ISR 标记传递。
  // Test coverage: verify default empty-callback behavior and immediate direct reentry with ISR-flag propagation.
  {
    LibXR::Callback<int> empty_cb;
    ASSERT(empty_cb.Empty());
    empty_cb.Run(false, 1);
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
}

}  // namespace

/**
 * @brief 测试项函数 `RunDirectCallbackTests`。 Test-item function `RunDirectCallbackTests`.
 * @details 测试内容：执行空回调与直接重入子场景。 Execute empty-callback and direct-reentry sub-scenarios.
 *          测试原理：把直接回调语义单独成组，避免和 guarded/lambda 场景混在一起。 Group direct-callback semantics away from guarded/lambda scenarios.
 */
void RunDirectCallbackTests()
{
  TestEmptyAndDirectCallbacks();
}
