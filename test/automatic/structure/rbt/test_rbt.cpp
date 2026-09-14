/**
 * @file test_rbt.cpp
 * @brief 检查红黑树的插入、查找、有序遍历和删除计数。 /
 * Tests red-black tree insertion, lookup, ordered traversal and deletion counts.
 *
 * 包含 uint32_t 跨有符号整数边界的键值。
 * Includes uint32_t keys on both sides of the signed integer boundary.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_rbt()
{
  LibXR::RBTree<int> rbtree([](const int& a, const int& b) { return (a > b) - (a < b); });

  LibXR::RBTree<int>::Node<int> nodes[100];

  for (int i = 0; i < 100; i++)
  {
    nodes[i] = i;
    rbtree.Insert(nodes[i], i);
  }

  LibXR::RBTree<int>::Node<int>* node_pos = nullptr;
  for (int i = 0; i < 100; i++)
  {
    node_pos = rbtree.ForeachDisc(node_pos);
    TEST_ASSERT(*node_pos == i);
  }

  TEST_ASSERT(rbtree.GetNum() == 100);

  static int rbt_arg = 0;

  rbtree.Foreach<int>(
      [&](LibXR::RBTree<int>::Node<int>& node)
      {
        rbt_arg = rbt_arg + 1;
        TEST_ASSERT(rbt_arg == node + 1);
        return LibXR::ErrorCode::OK;
      });

  for (int i = 0; i < 100; i++)
  {
    rbtree.Delete(nodes[i]);
    TEST_ASSERT(rbtree.GetNum() == 99 - i);
  }

  TEST_ASSERT(rbtree.GetNum() == 0);

  LibXR::RBTree<uint32_t> uint32_tree([](const uint32_t& a, const uint32_t& b)
                                      { return (a > b) - (a < b); });
  constexpr uint32_t keys[] = {0U, 1U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU};
  LibXR::RBTree<uint32_t>::Node<uint32_t> uint32_nodes[std::size(keys)];

  for (size_t i = 0; i < std::size(keys); i++)
  {
    uint32_nodes[i] = keys[i];
    uint32_tree.Insert(uint32_nodes[i], keys[i]);
  }

  LibXR::RBTree<uint32_t>::Node<uint32_t>* uint32_node_pos = nullptr;
  for (size_t i = 0; i < std::size(keys); i++)
  {
    uint32_node_pos = uint32_tree.ForeachDisc(uint32_node_pos);
    TEST_ASSERT(uint32_node_pos != nullptr);
    TEST_ASSERT(*uint32_node_pos == keys[i]);
    TEST_ASSERT(uint32_tree.Search<uint32_t>(keys[i]) == &uint32_nodes[i]);
  }
}
