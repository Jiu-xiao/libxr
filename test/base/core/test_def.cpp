/**
 * @file test_def.cpp
 * @brief `libxr_def.hpp` 基础宏与辅助函数测试。 Foundational helpers in `libxr_def.hpp` tests.
 *
 * 测试项目 / Test items:
 * 1. 基础常量与字符串化宏。 Numeric constants and macros: verify `PI`, `TWO_PI`, gravity and `DEF2STR` behave as declared.
 * 2. `OffsetOf()` / `ContainerOf()` 布局辅助。 Layout helpers: verify `OffsetOf()` and `ContainerOf()` recover the owning object correctly for mutable and const member pointers.
 * 3. `SizeLimitCheck()` 四种模式语义。 Size-limit predicate semantics: verify `SizeLimitCheck()` matches the documented `EQUAL / LESS / MORE / NONE` rules.
 *
 * 测试原理 / Test principles:
 * 1. 使用具体对象布局和宏 token，验证这些小工具作为全库基础契约的稳定性。 Use concrete layout objects and stringized macro tokens, because these helpers are small but widely reused contract surfaces.
 * 2. 同时覆盖正反两类边界，避免只验证单一 happy path。 Check both positive and negative predicate cases so the test documents the exact boundary semantics rather than a single happy path.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

struct LayoutProbe
{
  uint16_t header;
  double value;
  uint8_t tail;
};

static_assert(LibXR::MemberObjectPointer<LayoutProbe, double>);
static_assert(LibXR::CommonOrdered<int, double>);

}  // namespace

void test_def()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  ASSERT(equal(LibXR::TWO_PI, LibXR::PI * 2.0));
  ASSERT(equal(LibXR::STANDARD_GRAVITY, 9.80665));
  ASSERT(LibXR::ALIGN_SIZE == sizeof(void*));
  ASSERT(LibXR::CACHE_LINE_SIZE == (sizeof(void*) == 8 ? 64u : 32u));
  ASSERT(std::string_view(DEF2STR(LIBXR_TEST_TOKEN)) == "LIBXR_TEST_TOKEN");

  LayoutProbe probe{0x1234u, 12.5, 0x7Fu};
  const auto expected_offset =
      static_cast<size_t>(reinterpret_cast<const uint8_t*>(&probe.value) -
                          reinterpret_cast<const uint8_t*>(&probe));
  ASSERT(LibXR::OffsetOf(&LayoutProbe::value) == expected_offset);
  ASSERT(LibXR::ContainerOf(&probe.value, &LayoutProbe::value) == &probe);
  ASSERT(LibXR::ContainerOf(&std::as_const(probe).value, &LayoutProbe::value) == &probe);

  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::EQUAL, 8, 8));
  ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::EQUAL, 8, 7));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 7));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 8));
  ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 9));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 8));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 9));
  ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 7));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::NONE, 8, 0));
  ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::NONE, 8, 99));
}
