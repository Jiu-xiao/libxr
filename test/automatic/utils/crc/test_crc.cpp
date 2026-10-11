/**
 * @file test_crc.cpp
 * @brief CRC 计算与校验测试 / CRC calculation and verification tests.
 *
 * 分别给打包数据附加 CRC8、CRC16 和 CRC32，再检查完整数据能通过校验；用独立的逐位实现和
 * "123456789" 校验值核对四种 CRC 的结果和查找表；在全新进程里让多个线程同时首次调用 CRC。
 * Append CRC8, CRC16 and CRC32 to packed data and verify each complete record. Check the
 * results and lookup tables of all four CRCs against an independent bitwise
 * implementation and the "123456789" check values, and call every CRC for the first time
 * from several threads at once in a fresh process.
 */

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "crc.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
// 不使用查找表的逐位反射 CRC，作为独立参照。
// Table-free bitwise reflected CRC used as an independent reference.
template <typename T>
T ReferenceCrc(const void* raw, size_t len, T init, T poly)
{
  const uint8_t* buf = static_cast<const uint8_t*>(raw);
  T crc = init;
  while (len-- > 0)
  {
    crc = static_cast<T>(crc ^ *buf++);
    for (int bit = 0; bit < 8; ++bit)
    {
      crc = (crc & 1U) ? static_cast<T>((crc >> 1U) ^ poly) : static_cast<T>(crc >> 1U);
    }
  }
  return crc;
}

uint8_t RefCrc8(const void* raw, size_t len)
{
  return ReferenceCrc<uint8_t>(raw, len, 0xFF, 0x8C);
}

uint16_t RefCrc16(const void* raw, size_t len)
{
  return ReferenceCrc<uint16_t>(raw, len, 0xFFFF, 0x8408);
}

uint32_t RefCrc32(const void* raw, size_t len)
{
  return ReferenceCrc<uint32_t>(raw, len, 0xFFFFFFFFU, 0xEDB88320U);
}

uint64_t RefCrc64(const void* raw, size_t len)
{
  return ReferenceCrc<uint64_t>(raw, len, 0xFFFFFFFFFFFFFFFFULL, 0xC96C5795D7870F42ULL);
}

// 输入 "123456789" 的期望值。LibXR 的初值全 1、不做末尾异或，所以与常见的
// CRC-32 0xCBF43926 等标准值互为按位取反；这些数值由独立的逐位实现算出，并与
// zlib.crc32 交叉核对。
// Expected results for the input "123456789". LibXR starts from all ones and applies no
// final XOR, so they are the bitwise complements of the usual check values such as
// CRC-32 0xCBF43926. They come from the independent bitwise implementation and were
// cross-checked against zlib.crc32.
constexpr char CHECK_INPUT[] = "123456789";
constexpr size_t CHECK_LEN = sizeof(CHECK_INPUT) - 1;
constexpr uint8_t CHECK_CRC8 = 0x0B;
constexpr uint16_t CHECK_CRC16 = 0x6F91;
constexpr uint32_t CHECK_CRC32 = 0x340BC6D9U;
constexpr uint64_t CHECK_CRC64 = 0x66A2364420E6C605ULL;

// 查找表在编译期生成；这里核对几项与公开的 CRC-32、CRC-64/XZ 等表一致的值。
// The tables are built at compile time; spot-check entries against the published tables.
static_assert(LibXR::CRC8::tab_.size() == 256 && LibXR::CRC8::tab_[1] == 0x5E &&
              LibXR::CRC8::tab_[128] == 0x8C && LibXR::CRC8::tab_[255] == 0x35);
static_assert(LibXR::CRC16::tab_.size() == 256 && LibXR::CRC16::tab_[1] == 0x1189 &&
              LibXR::CRC16::tab_[128] == 0x8408 && LibXR::CRC16::tab_[255] == 0x0F78);
static_assert(LibXR::CRC32::tab_.size() == 256 && LibXR::CRC32::tab_[1] == 0x77073096U &&
              LibXR::CRC32::tab_[128] == 0xEDB88320U &&
              LibXR::CRC32::tab_[255] == 0x2D02EF8DU);
static_assert(LibXR::CRC64::tab_.size() == 256 &&
              LibXR::CRC64::tab_[1] == 0xB32E4CBE03A75F6FULL &&
              LibXR::CRC64::tab_[128] == 0xC96C5795D7870F42ULL &&
              LibXR::CRC64::tab_[255] == 0xE0ADA17364673F59ULL);

struct CrcResults
{
  uint8_t crc8;
  uint16_t crc16;
  uint32_t crc32;
  uint64_t crc64;
  bool verify_ok;
};

// 依次使用四种 CRC，并校验尾部带 CRC 的记录。
// Use all four CRCs and verify records that carry a trailing CRC.
CrcResults CalculateAll(const uint8_t* data, size_t len, const uint8_t* rec8,
                        const uint8_t* rec16, const uint8_t* rec32)
{
  CrcResults r{};
  r.crc8 = LibXR::CRC8::Calculate(data, len);
  r.crc16 = LibXR::CRC16::Calculate(data, len);
  r.crc32 = LibXR::CRC32::Calculate(data, len);
  r.crc64 = LibXR::CRC64::Calculate(data, len);
  r.verify_ok = LibXR::CRC8::Verify(rec8, len + sizeof(uint8_t)) &&
                LibXR::CRC16::Verify(rec16, len + sizeof(uint16_t)) &&
                LibXR::CRC32::Verify(rec32, len + sizeof(uint32_t));
  return r;
}

void FillPseudoRandom(std::vector<uint8_t>& data, uint32_t seed)
{
  for (auto& byte : data)
  {
    seed = seed * 1664525U + 1013904223U;
    byte = static_cast<uint8_t>(seed >> 24U);
  }
}
}  // namespace

void test_crc()
{
  struct __attribute__((packed))
  {
    double a;
    char b;
    uint8_t crc;
  } test_crc8 = {.a = LibXR::PI, .b = 'X', .crc = 0};

  struct __attribute__((packed))
  {
    double a;
    char b;
    uint16_t crc;
  } test_crc16 = {.a = LibXR::PI * 2, .b = 'X', .crc = 0};

  struct __attribute__((packed))
  {
    double a;
    char b;
    uint32_t crc;
  } test_crc32 = {.a = LibXR::PI * 3, .b = 'X', .crc = 0};

  test_crc8.crc = LibXR::CRC8::Calculate(&test_crc8, sizeof(test_crc8) - sizeof(uint8_t));
  test_crc16.crc =
      LibXR::CRC16::Calculate(&test_crc16, sizeof(test_crc16) - sizeof(uint16_t));
  test_crc32.crc =
      LibXR::CRC32::Calculate(&test_crc32, sizeof(test_crc32) - sizeof(uint32_t));

  TEST_ASSERT(LibXR::CRC8::Verify(&test_crc8, sizeof(test_crc8)));
  TEST_ASSERT(LibXR::CRC16::Verify(&test_crc16, sizeof(test_crc16)));
  TEST_ASSERT(LibXR::CRC32::Verify(&test_crc32, sizeof(test_crc32)));

  // 标准校验值 / Standard check values.
  TEST_ASSERT(LibXR::CRC8::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC8);
  TEST_ASSERT(LibXR::CRC16::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC16);
  TEST_ASSERT(LibXR::CRC32::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC32);
  TEST_ASSERT(LibXR::CRC64::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC64);
  TEST_ASSERT(RefCrc8(CHECK_INPUT, CHECK_LEN) == CHECK_CRC8);
  TEST_ASSERT(RefCrc16(CHECK_INPUT, CHECK_LEN) == CHECK_CRC16);
  TEST_ASSERT(RefCrc32(CHECK_INPUT, CHECK_LEN) == CHECK_CRC32);
  TEST_ASSERT(RefCrc64(CHECK_INPUT, CHECK_LEN) == CHECK_CRC64);

  // 空输入返回初值 / Empty input returns the initial value.
  TEST_ASSERT(LibXR::CRC8::Calculate(CHECK_INPUT, 0) == 0xFF);
  TEST_ASSERT(LibXR::CRC16::Calculate(CHECK_INPUT, 0) == 0xFFFF);
  TEST_ASSERT(LibXR::CRC32::Calculate(CHECK_INPUT, 0) == 0xFFFFFFFFU);
  TEST_ASSERT(LibXR::CRC64::Calculate(CHECK_INPUT, 0) == 0xFFFFFFFFFFFFFFFFULL);

  // 不同长度下与逐位参照一致 / Match the bitwise reference for varied lengths.
  std::vector<uint8_t> data(600);
  FillPseudoRandom(data, 0x12345678U);
  for (size_t len : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{8}, size_t{255},
                     size_t{256}, size_t{257}, data.size()})
  {
    TEST_ASSERT(LibXR::CRC8::Calculate(data.data(), len) == RefCrc8(data.data(), len));
    TEST_ASSERT(LibXR::CRC16::Calculate(data.data(), len) == RefCrc16(data.data(), len));
    TEST_ASSERT(LibXR::CRC32::Calculate(data.data(), len) == RefCrc32(data.data(), len));
    TEST_ASSERT(LibXR::CRC64::Calculate(data.data(), len) == RefCrc64(data.data(), len));
  }

  // Verify 拒绝过短输入和被破坏的数据 / Verify rejects short input and corrupted data.
  TEST_ASSERT(!LibXR::CRC8::Verify(data.data(), 1));
  TEST_ASSERT(!LibXR::CRC16::Verify(data.data(), 1));
  TEST_ASSERT(!LibXR::CRC32::Verify(data.data(), 3));
  std::vector<uint8_t> rec(data.begin(), data.begin() + 64);
  rec.push_back(RefCrc8(rec.data(), rec.size()));
  TEST_ASSERT(LibXR::CRC8::Verify(rec.data(), rec.size()));
  rec[10] ^= 0x01;
  TEST_ASSERT(!LibXR::CRC8::Verify(rec.data(), rec.size()));
}

// 在全新进程里让多个线程同时首次使用 CRC。旧实现在首次调用时才于 .bss 中生成查找表，
// 没有任何同步，多线程同时首次调用会把表写坏且之后不再重算；本用例由 main.cpp 以隔离
// 方式运行，保证进程里此前没有用过 CRC。
// Let several threads use the CRCs for the first time at once in a fresh process. The old
// implementation built the lookup tables lazily in .bss without synchronization, so
// concurrent first calls corrupted them and they were never rebuilt. main.cpp runs this
// case isolated, so no CRC has been used earlier in the process.
void test_crc_first_call_race()
{
  const size_t thread_count =
      std::max<size_t>(8, std::thread::hardware_concurrency() * 2);

  std::vector<uint8_t> data(512);
  FillPseudoRandom(data, 0xA5A5A5A5U);
  const size_t len = data.size();

  const uint8_t exp8 = RefCrc8(data.data(), len);
  const uint16_t exp16 = RefCrc16(data.data(), len);
  const uint32_t exp32 = RefCrc32(data.data(), len);
  const uint64_t exp64 = RefCrc64(data.data(), len);

  // 记录末尾的 CRC 用独立实现算出。
  // The trailing CRCs of the records come from the independent reference.
  std::vector<uint8_t> rec8(data), rec16(data), rec32(data);
  rec8.resize(len + sizeof(exp8));
  rec16.resize(len + sizeof(exp16));
  rec32.resize(len + sizeof(exp32));
  std::memcpy(rec8.data() + len, &exp8, sizeof(exp8));
  std::memcpy(rec16.data() + len, &exp16, sizeof(exp16));
  std::memcpy(rec32.data() + len, &exp32, sizeof(exp32));

  std::vector<CrcResults> results(thread_count);
  std::atomic<size_t> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; ++i)
  {
    threads.emplace_back(
        [&, i]()
        {
          ready.fetch_add(1, std::memory_order_acq_rel);
          while (!go.load(std::memory_order_acquire))
          {
          }
          results[i] =
              CalculateAll(data.data(), len, rec8.data(), rec16.data(), rec32.data());
        });
  }
  while (ready.load(std::memory_order_acquire) != thread_count)
  {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);
  for (auto& thread : threads)
  {
    thread.join();
  }

  for (const auto& r : results)
  {
    TEST_ASSERT(r.crc8 == exp8);
    TEST_ASSERT(r.crc16 == exp16);
    TEST_ASSERT(r.crc32 == exp32);
    TEST_ASSERT(r.crc64 == exp64);
    TEST_ASSERT(r.verify_ok);
  }

  // 首次调用结束后再确认一次，防止表被写坏。
  // Check again once the first calls are over, in case a table was left corrupted.
  TEST_ASSERT(LibXR::CRC32::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC32);
  TEST_ASSERT(LibXR::CRC64::Calculate(CHECK_INPUT, CHECK_LEN) == CHECK_CRC64);
}
