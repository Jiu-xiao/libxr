/**
 * @file test_stack.cpp
 * @brief `Stack` push/pop and indexed edit tests.
 *
 * Test items:
 * 1. Capacity behavior: verify push fills the stack, the extra push reports `FULL`, and pop returns LIFO order.
 * 2. In-place editing helpers: verify indexed `Insert()` and `Delete()` shift the expected elements and update size.
 *
 * Test principle:
 * 1. Check both top-of-stack order and indexed edit side effects, because this container combines stack semantics with random-position maintenance helpers.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_stack()
{
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