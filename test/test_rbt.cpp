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
        return ErrorCode::OK;
      });

  for (int i = 0; i < 100; i++)
  {
    rbtree.Delete(nodes[i]);
    ASSERT(rbtree.GetNum() == 99 - i);
  }

  ASSERT(rbtree.GetNum() == 0);
}