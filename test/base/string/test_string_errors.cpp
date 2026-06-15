/**
 * @file test_string_errors.cpp
 * @brief 运行时字符串空指针错误路径子测试。 Split test unit for runtime-string null-pointer error paths.
 * @details 测试项目：
 *          1. 复制构造和后缀构造遇到空指针时返回 `PTR_NULL`。
 *          2. 错误对象退化为空字符串视图并保留安全的 `CStr()` 语义。
 *          Test items:
 *          1. Copy and suffix construction return `PTR_NULL` when given null pointers.
 *          2. Error objects collapse to empty string views while keeping safe `CStr()` semantics.
 */
#include "string_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestRuntimeStringErrors`。 Test-item function `TestRuntimeStringErrors`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestRuntimeStringErrors()
{
  // 测试内容：验证空指针输入的错误码、空视图和安全存储语义。
  // Test coverage: verify error code, empty-view, and safe-storage semantics for null-pointer inputs.
  LibXR::RuntimeStringView<> null_part("camera", static_cast<const char*>(nullptr));
  ASSERT(null_part.Empty());
  ASSERT(null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_part.Size() == 0);

  LibXR::RuntimeStringView<> null_copy(static_cast<const char*>(nullptr));
  ASSERT(null_copy.Empty());
  ASSERT(null_copy.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_copy.Size() == 0);
  ASSERT(null_copy.View().empty());
  ASSERT(null_copy.CStr()[0] == '\0');

  LibXR::RuntimeStringView<> bare_null(nullptr);
  ASSERT(bare_null.Empty());
  ASSERT(bare_null.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(bare_null.View().empty());

  LibXR::RuntimeStringView<> bare_null_part("camera", nullptr);
  ASSERT(bare_null_part.Empty());
  ASSERT(bare_null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(bare_null_part.View().empty());
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeStringErrorTests`。 Test-item function `RunRuntimeStringErrorTests`.
 * @details 测试内容：执行运行时字符串错误路径子场景。 Execute runtime-string error-path sub-scenarios.
 *          测试原理：把错误路径单独成组，集中验证空指针输入的退化契约。 Group error paths around the null-pointer degradation contract.
 */
void RunRuntimeStringErrorTests()
{
  TestRuntimeStringErrors();
}
