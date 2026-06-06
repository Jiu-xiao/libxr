/**
 * @file test_mem.cpp
 * @brief `LibXR::Memory` 快速 set/copy/compare 测试。 `LibXR::Memory` fast set/copy/compare tests.
 *
 * 测试项目 / Test items:
 * 1. `FastSet` 的填充与零长度空操作。 `FastSet`: verify full-buffer fill and zero-length no-op behavior.
 * 2. `FastCopy` 的普通拷贝、自拷贝、非对齐拷贝和小尺寸扫宽。 `FastCopy`: verify basic copy, self-copy, unaligned copy and a width sweep over small copy lengths.
 * 3. `FastCmp` 与 `std::memcmp` 的一致性。 `FastCmp`: verify equality, first/middle/last-byte differences and sign alignment with `std::memcmp`, including unaligned spans.
 *
 * 测试原理 / Test principles:
 * 1. 把标准库语义当参照，因为这些函数本质上是在优化一个已知契约。 Compare against standard-library semantics at the byte level, because these routines are performance-focused replacements for well-known contracts.
 * 2. 显式压非对齐地址和零长度边界，因为最容易在这里偏离参考行为。 Stress unaligned addresses and zero-length cases explicitly, since those are the edge conditions most likely to diverge from the reference behavior.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
#include "test.hpp"

static int sign(int v) { return (v > 0) - (v < 0); }

/**
 * @brief 测试入口函数 `test_memory`。 Test entry function `test_memory`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_memory()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  // --------------------------
  // FastSet: 基础填充与 size=0
  // --------------------------
  {
    uint8_t buf[64] = {};
    LibXR::Memory::FastSet(buf, 0xAA, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
      ASSERT(buf[i] == 0xAA);
    }

    // size = 0: 不应修改
    uint8_t guard = 0x5A;
    LibXR::Memory::FastSet(&guard, 0x00, 0);
    ASSERT(guard == 0x5A);
  }

  // --------------------------
  // FastCopy: 基础拷贝、size=0、自拷贝、非对齐地址、不同长度
  // --------------------------
  {
    uint8_t src[128];
    uint8_t dst[128];

    for (size_t i = 0; i < sizeof(src); ++i)
    {
      src[i] = static_cast<uint8_t>(i ^ 0x5C);
      dst[i] = 0x00;
    }

    // 基础整段拷贝
    LibXR::Memory::FastCopy(dst, src, sizeof(src));
    ASSERT(std::memcmp(dst, src, sizeof(src)) == 0);

    // size = 0: 不应修改 dst
    dst[17] = 0x11;
    LibXR::Memory::FastCopy(dst, src, 0);
    ASSERT(dst[17] == 0x11);

    // dst == src: 自拷贝不应破坏数据
    uint8_t backup[128];
    std::memcpy(backup, src, sizeof(src));
    LibXR::Memory::FastCopy(src, src, sizeof(src));
    ASSERT(std::memcmp(src, backup, sizeof(src)) == 0);

    // 非对齐地址拷贝：dst+1 <- src+3
    uint8_t src2[200];
    uint8_t dst2[200];
    for (size_t i = 0; i < sizeof(src2); ++i)
    {
      src2[i] = static_cast<uint8_t>(0xA5 ^ (i * 3));
      dst2[i] = 0xCC;
    }

    const size_t OFF_DST = 1;
    const size_t OFF_SRC = 3;
    const size_t LEN = 73;  // 非 4/8 对齐长度
    LibXR::Memory::FastCopy(dst2 + OFF_DST, src2 + OFF_SRC, LEN);
    ASSERT(std::memcmp(dst2 + OFF_DST, src2 + OFF_SRC, LEN) == 0);

    // 小尺寸/多种长度覆盖（包含 1..65）
    for (size_t n = 1; n <= 65; ++n)
    {
      for (size_t i = 0; i < sizeof(src); ++i)
      {
        src[i] = static_cast<uint8_t>(i + n);
        dst[i] = 0xEE;
      }
      LibXR::Memory::FastCopy(dst + 7, src + 5, n);  // 故意偏移
      ASSERT(std::memcmp(dst + 7, src + 5, n) == 0);
      // 确保范围外不被写（简单哨兵检查）
      ASSERT(dst[6] == 0xEE);
      ASSERT(dst[7 + n] == 0xEE);
    }
  }

  // --------------------------
  // FastCmp: 等于/不等，差异位置覆盖，并与 std::memcmp 符号对齐
  // --------------------------
  {
    uint8_t a[96];
    uint8_t b[96];

    for (size_t i = 0; i < sizeof(a); ++i)
    {
      a[i] = static_cast<uint8_t>(0x3C ^ (i * 7));
      b[i] = a[i];
    }

    // 完全相等
    ASSERT(LibXR::Memory::FastCmp(a, b, sizeof(a)) == 0);

    // 首字节差异
    b[0] = static_cast<uint8_t>(b[0] + 1);
    int r1 = LibXR::Memory::FastCmp(a, b, sizeof(a));
    int m1 = std::memcmp(a, b, sizeof(a));
    ASSERT(sign(r1) == sign(m1));
    b[0] = a[0];

    // 中间差异
    b[37] = static_cast<uint8_t>(b[37] - 1);
    int r2 = LibXR::Memory::FastCmp(a, b, sizeof(a));
    int m2 = std::memcmp(a, b, sizeof(a));
    ASSERT(sign(r2) == sign(m2));
    b[37] = a[37];

    // 末尾差异
    b[95] = static_cast<uint8_t>(b[95] + 5);
    int r3 = LibXR::Memory::FastCmp(a, b, sizeof(a));
    int m3 = std::memcmp(a, b, sizeof(a));
    ASSERT(sign(r3) == sign(m3));
    b[95] = a[95];

    // size = 0：应视为相等（对齐 memcmp 语义）
    ASSERT(LibXR::Memory::FastCmp(a, b, 0) == 0);

    // 非对齐指针比较
    uint8_t c[128];
    uint8_t d[128];
    for (size_t i = 0; i < sizeof(c); ++i)
    {
      c[i] = static_cast<uint8_t>(i * 11);
      d[i] = c[i];
    }
    // 制造一个差异
    d[19] ^= 0x01;

    const size_t OFF1 = 1;
    const size_t OFF2 = 3;
    const size_t N = 64;
    int r4 = LibXR::Memory::FastCmp(c + OFF1, d + OFF2, N);
    int m4 = std::memcmp(c + OFF1, d + OFF2, N);
    ASSERT(sign(r4) == sign(m4));
  }
}
