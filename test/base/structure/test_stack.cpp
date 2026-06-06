/**
 * @file test_stack.cpp
 * @brief `Stack` push/pop 与索引编辑测试。 `Stack` push/pop and indexed edit tests.
 *
 * 测试项目 / Test items:
 * 1. 容量与 LIFO 出栈行为。 Capacity behavior: verify push fills the stack, the extra push reports `FULL`, and pop returns LIFO order.
 * 2. `Insert()` / `Delete()` 索引位移行为。 In-place editing helpers: verify indexed `Insert()` and `Delete()` shift the expected elements and update size.
 *
 * 测试原理 / Test principles:
 * 1. 同时验证栈顶顺序和随机位置编辑副作用，因为这个容器同时承担两类语义。 Check both top-of-stack order and indexed edit side effects, because this container combines stack semantics with random-position maintenance helpers.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_stack()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Stack<int> stack(10);
  for (int i = 0; i < 10; i++)
  {
    stack.Push(i);
  }

  ASSERT(stack.Push(1) == LibXR::ErrorCode::FULL);

  for (int i = 0; i <= 9; i++)
  {
    int tmp = -1;
    stack.Pop(tmp);
    ASSERT(tmp == 9 - i);
  }

  ASSERT(stack.Pop() == LibXR::ErrorCode::EMPTY);

  for (int i = 0; i <= 5; i++)
  {
    stack.Push(i);
  }

  stack.Insert(10, 2);
  ASSERT(stack[2] == 10);
  ASSERT(stack[3] == 2);
  ASSERT(stack.Size() == 7);
  stack.Delete(2);
  ASSERT(stack[2] == 2);
  ASSERT(stack[3] == 3);
  ASSERT(stack.Size() == 6);
}