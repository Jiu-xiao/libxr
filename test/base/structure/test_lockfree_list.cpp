/**
 * @file test_lockfree_list.cpp
 * @brief `LockFreeList` 聚焦遍历与提前终止测试。 Focused `LockFreeList` traversal and early-stop tests.
 *
 * 测试项目 / Test items:
 * 1. 插入后头插遍历顺序。 Insertion order observation: verify the lock-free list exposes the expected head-first traversal order after repeated `Add()` calls.
 * 2. `Foreach()` 非 `OK` 提前停止。 Early termination: verify `Foreach()` stops and returns the callback's first non-`OK` result.
 *
 * 测试原理 / Test principles:
 * 1. 只观察公开遍历 API，因为 lock-free list 的契约是可见迭代行为。 Observe only the public traversal API, because the lock-free list contract is about visible iteration behavior rather than internal link layout.
 * 2. 用显式非 `OK` 返回击中取消分支。 Use a non-`OK` callback return to drive the cancellation branch explicitly.
 */
#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_lockfree_list`。 Test entry function `test_lockfree_list`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_lockfree_list()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::LockFreeList list;
  LibXR::LockFreeList::Node<int> node1(10);
  LibXR::LockFreeList::Node<int> node2(20);
  LibXR::LockFreeList::Node<int> node3(30);

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  ASSERT(list.Size() == 3);

  const int expected[] = {30, 20, 10};
  uint32_t index = 0;
  ASSERT(list.Foreach<int>(
             [&](int& value)
             {
               ASSERT(value == expected[index]);
               ++index;
               return LibXR::ErrorCode::OK;
             }) == LibXR::ErrorCode::OK);
  ASSERT(index == 3);

  index = 0;
  const auto stop_result = list.Foreach<int>(
      [&](int& value)
      {
        ++index;
        return value == 20 ? LibXR::ErrorCode::BUSY : LibXR::ErrorCode::OK;
      });
  ASSERT(stop_result == LibXR::ErrorCode::BUSY);
  ASSERT(index == 2);
}
