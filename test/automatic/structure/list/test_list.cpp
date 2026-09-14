/**
 * @file test_list.cpp
 * @brief 检查链表添加、遍历数量、删除和重复删除结果。 /
 * Tests list insertion, traversal counts, deletion and repeated deletion.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_list()
{
  uint32_t counter = 0;
  LibXR::List::Node<int> node1(10);
  LibXR::List::Node<int> node2(20);
  LibXR::List::Node<int> node3(30);

  LibXR::List list;

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  TEST_ASSERT(list.Size() == 3);

  auto node_foreach_fn = [&](int& node)
  {
    UNUSED(node);

    counter++;
    return LibXR::ErrorCode::OK;
  };

  TEST_ASSERT(list.Foreach<int>(node_foreach_fn) == LibXR::ErrorCode::OK);

  TEST_ASSERT(counter == 3);

  TEST_ASSERT(list.Delete(node2) == LibXR::ErrorCode::OK);
  TEST_ASSERT(list.Size() == 2);

  TEST_ASSERT(list.Delete(node1) == LibXR::ErrorCode::OK);
  TEST_ASSERT(list.Size() == 1);

  TEST_ASSERT(list.Delete(node3) == LibXR::ErrorCode::OK);
  TEST_ASSERT(list.Size() == 0);

  TEST_ASSERT(list.Delete(node1) == LibXR::ErrorCode::NOT_FOUND);
}
