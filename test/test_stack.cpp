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

  ASSERT(stack.Push(1) == ErrorCode::FULL);

  for (int i = 0; i <= 9; i++)
  {
    int tmp = -1;
    stack.Pop(tmp);
    ASSERT(tmp == 9 - i);
  }

  ASSERT(stack.Pop() == ErrorCode::EMPTY);

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