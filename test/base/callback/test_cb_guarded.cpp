/**
 * @file test_cb_guarded.cpp
 * @brief `LibXR::Callback` guarded 与 lambda 场景子测试。 Split test unit for guarded-callback and lambda-binding scenarios.
 * @details 测试项目：
 *          1. `CreateGuarded()` 会把自递归压平成单层回放。
 *          2. lambda 绑定仍会透传值和 ISR 标记。
 *          Test items:
 *          1. `CreateGuarded()` flattens self-recursion into one-depth replay.
 *          2. Lambda binding still forwards the payload value and ISR flag.
 */
#include "cb_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestGuardedAndLambdaCallbacks`。 Test-item function `TestGuardedAndLambdaCallbacks`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestGuardedAndLambdaCallbacks()
{
  // 测试内容：验证 guarded 压平语义和 lambda 绑定上下文转发。
  // Test coverage: verify guarded flattening semantics and lambda-binding context forwarding.
  {
    GuardedCreationProbe probe;
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

}  // namespace

/**
 * @brief 测试项函数 `RunGuardedCallbackTests`。 Test-item function `RunGuardedCallbackTests`.
 * @details 测试内容：执行 guarded 与 lambda 绑定子场景。 Execute guarded-callback and lambda-binding sub-scenarios.
 *          测试原理：把递归压平语义和绑定转发语义单独成组，便于定位回归。 Group flattening and binding-forwarding semantics for easier regression localization.
 */
void RunGuardedCallbackTests()
{
  TestGuardedAndLambdaCallbacks();
}
