/**
 * @file test_lockfree_list.cpp
 * @brief 检查头插遍历顺序，以及回调出错时停止遍历并返回该错误。 /
 * Tests head-insertion order, early traversal exit and error propagation.
 */

#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_lockfree_list()
{
  LibXR::LockFreeList::Node<int> node1(10);
  LibXR::LockFreeList::Node<int> node2(20);
  LibXR::LockFreeList::Node<int> node3(30);
  LibXR::LockFreeList list;

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  TEST_ASSERT(list.Size() == 3);

  // Add 从头部插入，因此遍历顺序与插入顺序相反。
  // Add inserts at the head, so traversal reverses insertion order.
  const int expected[] = {30, 20, 10};
  uint32_t index = 0;
  TEST_ASSERT(list.Foreach<int>(
                  [&](int& value)
                  {
                    TEST_ASSERT(index < 3);
                    TEST_ASSERT(value == expected[index]);
                    ++index;
                    return LibXR::ErrorCode::OK;
                  }) == LibXR::ErrorCode::OK);
  TEST_ASSERT(index == 3);

  index = 0;
  // 回调在第二个节点返回 BUSY，遍历应立即停止并把同一个错误返回给调用者。
  // The second node returns BUSY; traversal must stop and return that same error.
  const auto stop_result = list.Foreach<int>(
      [&](int& value)
      {
        ++index;
        return value == 20 ? LibXR::ErrorCode::BUSY : LibXR::ErrorCode::OK;
      });
  TEST_ASSERT(stop_result == LibXR::ErrorCode::BUSY);
  TEST_ASSERT(index == 2);
  std::fprintf(stderr, "lockfree_list: traversal=3, early_stop=2, result=BUSY\n");
}
