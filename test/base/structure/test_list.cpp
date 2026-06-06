/**
 * @file test_list.cpp
 * @brief Ordered list and lock-free list traversal tests.
 *
 * Test items:
 * 1. Ordinary `List`: verify add, foreach, delete and not-found delete behavior.
 * 2. `LockFreeList` basic traversal: verify append count and foreach coverage on the lock-free variant.
 *
 * Test principle:
 * 1. Reuse the same callback counter across both list types so the test compares the observable traversal contract directly.
 * 2. Check deletion results after each mutation, because size/accounting is the key caller-visible contract of the ordinary list.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

static uint32_t counter = 0;

void test_list()
{
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
