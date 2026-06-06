/**
 * @file test_list.cpp
 * @brief 普通链表与 lock-free 链表遍历测试。 Ordered list and lock-free list traversal tests.
 *
 * 测试项目 / Test items:
 * 1. 普通 `List` 的增删遍历行为。 Ordinary `List`: verify add, foreach, delete and not-found delete behavior.
 * 2. `LockFreeList` 的基本遍历覆盖。 `LockFreeList` basic traversal: verify append count and foreach coverage on the lock-free variant.
 *
 * 测试原理 / Test principles:
 * 1. 复用同一个回调计数器，直接比较两类链表对外可见的遍历契约。 Reuse the same callback counter across both list types so the test compares the observable traversal contract directly.
 * 2. 每次删除后都检查返回值和 size，因为这是普通链表最核心的调用方语义。 Check deletion results after each mutation, because size/accounting is the key caller-visible contract of the ordinary list.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

static uint32_t counter = 0;

void test_list()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::List::Node<int> node1(10);
  LibXR::List::Node<int> node2(20);
  LibXR::List::Node<int> node3(30);

  LibXR::List list;

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  ASSERT(list.Size() == 3);

  auto node_foreach_fn = [](int& node)
  {
    UNUSED(node);

    counter++;
    return LibXR::ErrorCode::OK;
  };

  list.Foreach<int>(node_foreach_fn);

  ASSERT(counter == 3);

  ASSERT(list.Delete(node2) == LibXR::ErrorCode::OK);
  ASSERT(list.Size() == 2);

  ASSERT(list.Delete(node1) == LibXR::ErrorCode::OK);
  ASSERT(list.Size() == 1);

  ASSERT(list.Delete(node3) == LibXR::ErrorCode::OK);
  ASSERT(list.Size() == 0);

  ASSERT(list.Delete(node1) == LibXR::ErrorCode::NOT_FOUND);

  LibXR::LockFreeList::Node<int> node4(10);
  LibXR::LockFreeList::Node<int> node5(20);
  LibXR::LockFreeList::Node<int> node6(30);

  LibXR::LockFreeList list_lock_free;

  list_lock_free.Add(node4);
  list_lock_free.Add(node5);
  list_lock_free.Add(node6);

  ASSERT(list_lock_free.Size() == 3);

  list_lock_free.Foreach<int>(node_foreach_fn);

  ASSERT(counter == 6);
}
