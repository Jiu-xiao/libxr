/**
 * @file test_stack.cpp
 * @brief 检查栈的后进先出、满空状态及指定位置的插入和删除。 /
 * Tests stack order, full and empty results, insertion and deletion.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_stack()
{
  LibXR::Stack<int> stack(10);
  for (int i = 0; i < 10; i++)
  {
    stack.Push(i);
  }

  TEST_ASSERT(stack.Push(1) == LibXR::ErrorCode::FULL);

  for (int i = 0; i <= 9; i++)
  {
    int tmp = -1;
    stack.Pop(tmp);
    TEST_ASSERT(tmp == 9 - i);
  }

  TEST_ASSERT(stack.Pop() == LibXR::ErrorCode::EMPTY);

  for (int i = 0; i <= 5; i++)
  {
    stack.Push(i);
  }

  stack.Insert(10, 2);
  TEST_ASSERT(stack[2] == 10);
  TEST_ASSERT(stack[3] == 2);
  TEST_ASSERT(stack.Size() == 7);
  stack.Delete(2);
  TEST_ASSERT(stack[2] == 2);
  TEST_ASSERT(stack[3] == 3);
  TEST_ASSERT(stack.Size() == 6);
}
