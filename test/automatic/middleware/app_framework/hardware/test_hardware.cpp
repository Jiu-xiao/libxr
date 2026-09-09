/**
 * @file test_hardware.cpp
 * @brief HardwareContainer 查找测试 / HardwareContainer lookup tests.
 *
 * 检查按类型和别名查找、候选名称顺序，以及注册新设备后的查找结果。
 * Check typed aliases, fallback names and lookup after registering another device.
 */

#include "libxr.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{

struct DeviceA
{
  int value = 1;
};

struct DeviceB
{
  int value = 2;
};

struct DeviceC
{
  int value = 3;
};

struct DeviceD
{
  int value = 4;
};

}  // namespace

void test_app_framework_hardware()
{
  DeviceA dev_a;
  DeviceB dev_b;
  DeviceC dev_c;
  DeviceD dev_d;

  LibXR::HardwareContainer hw(LibXR::Entry<DeviceA>{dev_a, {"a", "shared", "fallback-a"}},
                              LibXR::Entry<DeviceB>{dev_b, {"b", "shared"}},
                              LibXR::Entry<DeviceC>{dev_c, {"c-only"}});

  TEST_ASSERT(hw.Find<DeviceA>("a") == &dev_a);
  TEST_ASSERT(hw.Find<DeviceB>("b") == &dev_b);
  TEST_ASSERT(hw.Find<DeviceC>("c-only") == &dev_c);

  TEST_ASSERT(hw.Find<DeviceA>("shared") == &dev_a);
  TEST_ASSERT(hw.Find<DeviceB>("shared") == &dev_b);
  TEST_ASSERT(hw.Find<DeviceC>("shared") == nullptr);

  TEST_ASSERT(hw.Find<DeviceA>("missing") == nullptr);
  TEST_ASSERT(hw.Find<DeviceB>({"missing", "shared"}) == &dev_b);
  TEST_ASSERT(hw.Find<DeviceA>({"missing", "fallback-a"}) == &dev_a);
  TEST_ASSERT(hw.Find<DeviceC>({"missing", "shared"}) == nullptr);
  TEST_ASSERT(hw.Find<DeviceA>({}) == nullptr);

  hw.Register(LibXR::Entry<DeviceD>{dev_d, {"d", "shared-d"}});
  TEST_ASSERT(hw.Find<DeviceD>("d") == &dev_d);
  TEST_ASSERT(hw.Find<DeviceD>({"missing", "shared-d"}) == &dev_d);
  TEST_ASSERT(hw.Find<DeviceA>("shared-d") == nullptr);
}
