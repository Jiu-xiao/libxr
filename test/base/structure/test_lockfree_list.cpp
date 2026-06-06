#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_lockfree_list()
{
  LibXR::LockFreeList list;
  LibXR::LockFreeList::Node<int> node1(10);
  LibXR::LockFreeList::Node<int> node2(20);
  LibXR::LockFreeList::Node<int> node3(30);

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  ASSERT(list.Size() == 3);

  const int expected[] = {30, 20, 10};
  uint32_t index = 0;
  ASSERT(list.Foreach<int>(
             [&](int& value)
             {
               ASSERT(value == expected[index]);
               ++index;
               return LibXR::ErrorCode::OK;
             }) == LibXR::ErrorCode::OK);
  ASSERT(index == 3);

  index = 0;
  const auto stop_result = list.Foreach<int>(
      [&](int& value)
      {
        ++index;
        return value == 20 ? LibXR::ErrorCode::BUSY : LibXR::ErrorCode::OK;
      });
  ASSERT(stop_result == LibXR::ErrorCode::BUSY);
  ASSERT(index == 2);
}
