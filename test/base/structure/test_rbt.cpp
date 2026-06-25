/**
 * @file test_rbt.cpp
 * @brief 红黑树插入、遍历与删除测试。 Red-black tree insertion, traversal and deletion tests.
 *
 * 测试项目 / Test items:
 * 1. 有序遍历顺序。 Ordered traversal: verify inserted keys are returned in ascending order through discrete and callback traversal.
 * 2. 节点数量与删除收缩行为。 Node counting and deletion: verify the tree count grows to the inserted size and shrinks monotonically during deletion.
 *
 * 测试原理 / Test principles:
 * 1. 用顺序整数键让排序错误立即可见。 Use sequential integer keys so ordering mistakes become obvious without additional decoding.
 * 2. 每次删除后都检查数量，确保平衡和删除路径的可见结果正确。 Check count after every delete so balancing-side regressions still surface as observable cardinality errors.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_rbt`。 Test entry function `test_rbt`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rbt()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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