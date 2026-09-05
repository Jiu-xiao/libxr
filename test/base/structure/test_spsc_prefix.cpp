#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "spsc_queue.hpp"
#include "test.hpp"

namespace
{
using Queue = LibXR::SPSCQueue<uint32_t>;

void PositionEmptyQueue(Queue& queue, size_t offset)
{
  for (size_t i = 0; i < offset; ++i)
  {
    ASSERT(queue.Push(0U) == LibXR::ErrorCode::OK);
    ASSERT(queue.Pop() == LibXR::ErrorCode::OK);
  }
}

void CheckContents(Queue& queue, const std::vector<uint32_t>& expected)
{
  ASSERT(queue.Size() == expected.size());
  for (uint32_t value : expected)
  {
    uint32_t actual = 0U;
    ASSERT(queue.Pop(actual) == LibXR::ErrorCode::OK);
    ASSERT(actual == value);
  }
  ASSERT(queue.Size() == 0U);
}

void TestEveryPrefix()
{
  // Independent FIFO values exercise each physical start, occupancy, limit and result.
  for (size_t capacity = 1U; capacity <= 8U; ++capacity)
  {
    for (size_t offset = 0U; offset <= capacity; ++offset)
    {
      for (size_t used = 0U; used <= capacity; ++used)
      {
        for (size_t limit = 0U; limit <= capacity + 2U; ++limit)
        {
          const size_t write_offer = std::min(limit, capacity - used);
          for (size_t prefix = 0U; prefix <= write_offer; ++prefix)
          {
            Queue queue(capacity);
            PositionEmptyQueue(queue, offset);
            std::vector<uint32_t> expected;
            for (size_t i = 0U; i < used; ++i)
            {
              expected.push_back(static_cast<uint32_t>(10U + i));
              ASSERT(queue.Push(expected.back()) == LibXR::ErrorCode::OK);
            }
            size_t calls = 0U;
            const size_t produced = queue.ProduceWithWriter(
                limit,
                [&](uint32_t* first, size_t n1, uint32_t* second, size_t n2)
                {
                  ++calls;
                  ASSERT(first != nullptr && n1 > 0U);
                  ASSERT(n1 + n2 == write_offer);
                  ASSERT((second == nullptr) == (n2 == 0U));
                  ASSERT(queue.Size() == used);
                  for (size_t i = 0U; i < prefix; ++i)
                  {
                    (i < n1 ? first[i] : second[i - n1]) =
                        static_cast<uint32_t>(100U + i);
                  }
                  ASSERT(queue.Size() == used);
                  return prefix;
                });
            ASSERT(produced == prefix);
            ASSERT(calls == (write_offer == 0U ? 0U : 1U));
            for (size_t i = 0U; i < prefix; ++i)
            {
              expected.push_back(static_cast<uint32_t>(100U + i));
            }
            CheckContents(queue, expected);
          }

          const size_t read_offer = std::min(limit, used);
          for (size_t prefix = 0U; prefix <= read_offer; ++prefix)
          {
            Queue queue(capacity);
            PositionEmptyQueue(queue, offset);
            std::vector<uint32_t> expected;
            for (size_t i = 0U; i < used; ++i)
            {
              expected.push_back(static_cast<uint32_t>(10U + i));
              ASSERT(queue.Push(expected.back()) == LibXR::ErrorCode::OK);
            }
            size_t calls = 0U;
            const size_t consumed = queue.ConsumeWithReader(
                limit,
                [&](const uint32_t* first, size_t n1, const uint32_t* second, size_t n2)
                {
                  ++calls;
                  ASSERT(first != nullptr && n1 > 0U);
                  ASSERT(n1 + n2 == read_offer);
                  ASSERT((second == nullptr) == (n2 == 0U));
                  for (size_t i = 0U; i < read_offer; ++i)
                  {
                    ASSERT((i < n1 ? first[i] : second[i - n1]) == expected[i]);
                  }
                  ASSERT(queue.Size() == used);
                  return prefix;
                });
            ASSERT(consumed == prefix);
            ASSERT(calls == (read_offer == 0U ? 0U : 1U));
            expected.erase(expected.begin(), expected.begin() + prefix);
            CheckContents(queue, expected);
          }
        }
      }
    }
  }
}

void TestOppositeSideProgress()
{
  Queue queue(3U);
  const size_t produced =
      queue.ProduceWithWriter(std::numeric_limits<size_t>::max(),
                              [&](uint32_t* first, size_t n1, uint32_t*, size_t n2)
                              {
                                ASSERT(n1 == 3U && n2 == 0U);
                                first[0] = 71U;
                                uint32_t value = 0U;
                                ASSERT(queue.Pop(value) == LibXR::ErrorCode::EMPTY);
                                return 1U;
                              });
  ASSERT(produced == 1U);
  ASSERT(queue.ConsumeWithReader(
             3U,
             [&](const uint32_t* first, size_t n1, const uint32_t*, size_t n2)
             {
               ASSERT(n1 == 1U && n2 == 0U && first[0] == 71U);
               ASSERT(queue.Push(72U) == LibXR::ErrorCode::OK);
               return 1U;
             }) == 1U);
  CheckContents(queue, {72U});

  ASSERT(queue.PushBatch(static_cast<const uint32_t*>(nullptr), 0U) ==
         LibXR::ErrorCode::OK);
  const uint32_t full[] = {1U, 2U, 3U};
  ASSERT(queue.PushBatch(full, 3U) == LibXR::ErrorCode::OK);
  ASSERT(queue.ConsumeWithReader(3U,
                                 [&](const uint32_t*, size_t, const uint32_t*, size_t)
                                 {
                                   ASSERT(queue.Push(99U) == LibXR::ErrorCode::FULL);
                                   return 2U;
                                 }) == 2U);
  ASSERT(queue.Push(99U) == LibXR::ErrorCode::OK);
  CheckContents(queue, {3U, 99U});
}

struct alignas(16) AlignedPayload
{
  uint32_t value;
};

void TestAlignedPayloadAndRawStride()
{
  LibXR::SPSCQueue<AlignedPayload> queue(3U);
  AlignedPayload seed{0U};
  ASSERT(queue.Push(seed) == LibXR::ErrorCode::OK);
  ASSERT(queue.Push(seed) == LibXR::ErrorCode::OK);
  ASSERT(queue.Pop() == LibXR::ErrorCode::OK);
  ASSERT(queue.Pop() == LibXR::ErrorCode::OK);
  ASSERT(queue.ProduceWithWriter(
             3U,
             [](AlignedPayload* first, size_t n1, AlignedPayload* second, size_t n2)
             {
               ASSERT(n1 == 2U && n2 == 1U);
               ASSERT(reinterpret_cast<uintptr_t>(first) % alignof(AlignedPayload) == 0U);
               ASSERT(reinterpret_cast<uintptr_t>(second) % alignof(AlignedPayload) ==
                      0U);
               first[0] = AlignedPayload{11U};
               first[1] = AlignedPayload{12U};
               second[0] = AlignedPayload{13U};
               return 3U;
             }) == 3U);
  for (uint32_t expected = 11U; expected <= 13U; ++expected)
  {
    AlignedPayload actual{};
    ASSERT(queue.Pop(actual) == LibXR::ErrorCode::OK);
    ASSERT(actual.value == expected);
  }

  LibXR::SPSCQueueBase raw(3U, 4U, 2U);
  ASSERT(raw.ProduceWithWriter(2U,
                               [](void* first, size_t n1, void*, size_t n2)
                               {
                                 ASSERT(n1 == 2U && n2 == 0U);
                                 auto* bytes = static_cast<uint8_t*>(first);
                                 for (size_t i = 0U; i < n1; ++i)
                                 {
                                   for (size_t byte = 0U; byte < 3U; ++byte)
                                   {
                                     bytes[i * 4U + byte] = static_cast<uint8_t>(i + 1U);
                                   }
                                 }
                                 return n1;
                               }) == 2U);
  uint8_t payload[3]{};
  for (uint8_t expected = 1U; expected <= 2U; ++expected)
  {
    ASSERT(raw.PopBytes(payload) == LibXR::ErrorCode::OK);
    ASSERT(payload[0] == expected && payload[1] == expected && payload[2] == expected);
  }
}

void TestConcurrentPrefixes()
{
  constexpr uint32_t TOTAL = 50000U;
  Queue queue(17U);
  std::thread producer(
      [&]
      {
        uint32_t next = 0U;
        while (next != TOTAL)
        {
          const size_t produced = queue.ProduceWithWriter(
              std::min<uint32_t>(11U, TOTAL - next),
              [&](uint32_t* first, size_t n1, uint32_t* second, size_t n2)
              {
                const size_t prefix = std::min<size_t>(n1 + n2, 1U + next % 7U);
                for (size_t i = 0U; i < prefix; ++i)
                {
                  (i < n1 ? first[i] : second[i - n1]) = next + static_cast<uint32_t>(i);
                }
                return prefix;
              });
          next += static_cast<uint32_t>(produced);
          if (produced == 0U)
          {
            std::this_thread::yield();
          }
        }
      });
  uint32_t expected = 0U;
  while (expected != TOTAL)
  {
    const size_t consumed = queue.ConsumeWithReader(
        13U,
        [&](const uint32_t* first, size_t n1, const uint32_t* second, size_t n2)
        {
          const size_t prefix = std::min<size_t>(n1 + n2, 1U + expected % 5U);
          for (size_t i = 0U; i < prefix; ++i)
          {
            ASSERT((i < n1 ? first[i] : second[i - n1]) == expected + i);
          }
          return prefix;
        });
    expected += static_cast<uint32_t>(consumed);
    if (consumed == 0U)
    {
      std::this_thread::yield();
    }
  }
  producer.join();
  ASSERT(queue.Size() == 0U);
}
}  // namespace

void test_spsc_prefix()
{
  TestEveryPrefix();
  TestOppositeSideProgress();
  TestAlignedPayloadAndRawStride();
  TestConcurrentPrefixes();
}
