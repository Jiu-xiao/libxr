/**
 * @file test_def.cpp
 * @brief 检查公共常量、成员定位和长度限制判断。 /
 * Tests public constants, member offsets, container lookup and size limits.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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
  TEST_ASSERT(equal(LibXR::TWO_PI, LibXR::PI * 2.0));
  TEST_ASSERT(equal(LibXR::STANDARD_GRAVITY, 9.80665));
  TEST_ASSERT(LibXR::ALIGN_SIZE == sizeof(void*));
  TEST_ASSERT(LibXR::CACHE_LINE_SIZE == (sizeof(void*) == 8 ? 64u : 32u));
  TEST_ASSERT(std::string_view(DEF2STR(LIBXR_TEST_TOKEN)) == "LIBXR_TEST_TOKEN");

  LayoutProbe probe{0x1234u, 12.5, 0x7Fu};
  const auto expected_offset =
      static_cast<size_t>(reinterpret_cast<const uint8_t*>(&probe.value) -
                          reinterpret_cast<const uint8_t*>(&probe));
  TEST_ASSERT(LibXR::OffsetOf(&LayoutProbe::value) == expected_offset);
  TEST_ASSERT(LibXR::ContainerOf(&probe.value, &LayoutProbe::value) == &probe);
  TEST_ASSERT(LibXR::ContainerOf(&std::as_const(probe).value, &LayoutProbe::value) ==
              &probe);

  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::EQUAL, 8, 8));
  TEST_ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::EQUAL, 8, 7));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 7));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 8));
  TEST_ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::LESS, 8, 9));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 8));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 9));
  TEST_ASSERT(!LibXR::SizeLimitCheck(LibXR::SizeLimitMode::MORE, 8, 7));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::NONE, 8, 0));
  TEST_ASSERT(LibXR::SizeLimitCheck(LibXR::SizeLimitMode::NONE, 8, 99));
}
