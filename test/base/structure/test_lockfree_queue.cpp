#include <cstddef>
#include <vector>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_lockfree_queue()
{
  LibXR::LockFreeQueue<int> queue(5);
  const size_t max_size = queue.MaxSize();

  ASSERT(max_size >= 5);
  ASSERT(queue.Size() == 0);
  ASSERT(queue.EmptySize() == max_size);

  int first_item = -1;
  ASSERT(queue.Peek(first_item) == LibXR::ErrorCode::EMPTY);
  ASSERT(queue.Pop() == LibXR::ErrorCode::EMPTY);

  std::vector<int> initial(max_size - 1);
  for (size_t i = 0; i < initial.size(); ++i)
  {
    initial[i] = static_cast<int>(i + 1);
  }

  size_t write_index = 0;
  ASSERT(queue.PushWithWriter(initial.size(),
                              [&](int* buffer, size_t chunk_size)
                              {
                                for (size_t i = 0; i < chunk_size; ++i)
                                {
                                  buffer[i] = initial[write_index++];
                                }
                                return LibXR::ErrorCode::OK;
                              }) == LibXR::ErrorCode::OK);
  ASSERT(write_index == initial.size());
  ASSERT(queue.Size() == initial.size());
  ASSERT(queue.EmptySize() == 1);

  ASSERT(queue.Peek(first_item) == LibXR::ErrorCode::OK);
  ASSERT(first_item == 1);
  ASSERT(queue.PushWithWriter(2, [&](int*, size_t) { return LibXR::ErrorCode::OK; }) ==
         LibXR::ErrorCode::FULL);

  const size_t prefix_count = max_size - 4;
  std::vector<int> prefix(prefix_count, 0);
  size_t prefix_index = 0;
  ASSERT(queue.PopWithReader(prefix_count,
                             [&](const int* buffer, size_t chunk_size)
                             {
                               for (size_t i = 0; i < chunk_size; ++i)
                               {
                                 prefix[prefix_index++] = buffer[i];
                               }
                               return LibXR::ErrorCode::OK;
                             }) == LibXR::ErrorCode::OK);
  ASSERT(prefix_index == prefix_count);
  for (size_t i = 0; i < prefix.size(); ++i)
  {
    ASSERT(prefix[i] == initial[i]);
  }
  ASSERT(queue.Size() == 3);

  ASSERT(queue.PopWithReader(1, [&](const int*, size_t) { return LibXR::ErrorCode::BUSY; }) ==
         LibXR::ErrorCode::BUSY);
  ASSERT(queue.Peek(first_item) == LibXR::ErrorCode::OK);
  ASSERT(first_item == initial[prefix_count]);
  ASSERT(queue.Size() == 3);

  const int wrap_values[] = {100, 101, 102};
  size_t wrap_index = 0;
  ASSERT(queue.PushWithWriter(3,
                              [&](int* buffer, size_t chunk_size)
                              {
                                for (size_t i = 0; i < chunk_size; ++i)
                                {
                                  buffer[i] = wrap_values[wrap_index++];
                                }
                                return LibXR::ErrorCode::OK;
                              }) == LibXR::ErrorCode::OK);
  ASSERT(wrap_index == 3);
  ASSERT(queue.Size() == 6);

  std::vector<int> peeked(6, 0);
  ASSERT(queue.PeekBatch(peeked.data(), peeked.size()) == LibXR::ErrorCode::OK);
  ASSERT(peeked[0] == initial[prefix_count]);
  ASSERT(peeked[1] == initial[prefix_count + 1]);
  ASSERT(peeked[2] == initial[prefix_count + 2]);
  ASSERT(peeked[3] == 100);
  ASSERT(peeked[4] == 101);
  ASSERT(peeked[5] == 102);

  std::vector<int> drained(peeked.size(), 0);
  size_t drain_index = 0;
  ASSERT(queue.PopWithReader(drained.size(),
                             [&](const int* buffer, size_t chunk_size)
                             {
                               for (size_t i = 0; i < chunk_size; ++i)
                               {
                                 drained[drain_index++] = buffer[i];
                               }
                               return LibXR::ErrorCode::OK;
                             }) == LibXR::ErrorCode::OK);
  ASSERT(drain_index == drained.size());
  for (size_t i = 0; i < drained.size(); ++i)
  {
    ASSERT(drained[i] == peeked[i]);
  }
  ASSERT(queue.Size() == 0);

  int failed_writes = 0;
  ASSERT(queue.PushWithWriter(2,
                              [&](int*, size_t)
                              {
                                ++failed_writes;
                                return LibXR::ErrorCode::FAILED;
                              }) == LibXR::ErrorCode::FAILED);
  ASSERT(failed_writes == 1);
  ASSERT(queue.Size() == 0);

  ASSERT(queue.Push(55) == LibXR::ErrorCode::OK);
  ASSERT(queue.Push(66) == LibXR::ErrorCode::OK);
  queue.Reset();
  ASSERT(queue.Size() == 0);
  ASSERT(queue.EmptySize() == max_size);
  ASSERT(queue.Peek(first_item) == LibXR::ErrorCode::EMPTY);
  ASSERT(queue.Pop() == LibXR::ErrorCode::EMPTY);
}
