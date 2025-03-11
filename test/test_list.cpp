#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

static uint32_t counter = 0;

void test_list() {
  LibXR::List list;
  LibXR::List::Node<int> node1(10);
  LibXR::List::Node<int> node2(20);
  LibXR::List::Node<int> node3(30);

  list.Add(node1);
  list.Add(node2);
  list.Add(node3);

  ASSERT(list.Size() == 3);

  auto node_foreach_fn = [](int &node) {
    UNUSED(node);

    counter++;
    return ErrorCode::OK;
  };

  list.Foreach<int>(node_foreach_fn);

  ASSERT(list.Delete(node2) == ErrorCode::OK);
  ASSERT(list.Size() == 2);

  ASSERT(list.Delete(node1) == ErrorCode::OK);
  ASSERT(list.Size() == 1);

  ASSERT(list.Delete(node3) == ErrorCode::OK);
  ASSERT(list.Size() == 0);

  ASSERT(list.Delete(node1) == ErrorCode::NOT_FOUND);
}