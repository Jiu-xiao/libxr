/**
 * @file test_rbt.cpp
 * @brief Red-black tree insertion, traversal and deletion tests.
 *
 * Test items:
 * 1. Ordered traversal: verify inserted keys are returned in ascending order through discrete and callback traversal.
 * 2. Node counting and deletion: verify the tree count grows to the inserted size and shrinks monotonically during deletion.
 *
 * Test principle:
 * 1. Use sequential integer keys so ordering mistakes become obvious without additional decoding.
 * 2. Check count after every delete so balancing-side regressions still surface as observable cardinality errors.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_rbt()
{
  LibXR::RBTree<int> rbtree([](const int& a, const int& b) { return a - b; });

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
    ASSERT(*node_pos == i);
  }

  ASSERT(rbtree.GetNum() == 100);

  static int rbt_arg = 0;

  rbtree.Foreach<int>(
      [&](LibXR::RBTree<int>::Node<int>& node)
      {
        rbt_arg = rbt_arg + 1;
        ASSERT(rbt_arg == node + 1);
        return LibXR::ErrorCode::OK;
      });

  for (int i = 0; i < 100; i++)
  {
    rbtree.Delete(nodes[i]);
    ASSERT(rbtree.GetNum() == 99 - i);
  }

  ASSERT(rbtree.GetNum() == 0);
}