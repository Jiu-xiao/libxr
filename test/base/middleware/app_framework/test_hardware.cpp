/**
 * @file test_hardware.cpp
 * @brief `HardwareContainer` 别名注册与类型化查找测试。 `HardwareContainer` alias registration and typed lookup tests.
 *
 * 测试项目 / Test items:
 * 1. 主别名直接查找。 Direct alias lookup: verify each registered device is found by its primary alias.
 * 2. 共享别名下的类型过滤。 Shared alias filtering: verify the same alias can resolve different objects only when the requested type matches.
 * 3. 回退别名与运行时追加注册。 Fallback-alias search and post-construction registration: verify multi-alias search and later `Register()` calls both become visible to typed lookup.
 *
 * 测试原理 / Test principles:
 * 1. 用同名别名在不同类型下交叉查找，验证真正契约是“名字 + 精确类型”。 Cross-check the same alias under multiple requested types, because the core contract is "name + exact type" rather than name alone.
 * 2. 同时覆盖构造期和运行时注册路径，避免两条别名填充路径漂移。 Exercise both constructor-time and runtime registration so the test covers both alias population paths.
 */
#include "libxr.hpp"
#include "test.hpp"

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
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  DeviceA dev_a;
  DeviceB dev_b;
  DeviceC dev_c;
  DeviceD dev_d;

  LibXR::HardwareContainer hw(
      LibXR::Entry<DeviceA>{dev_a, {"a", "shared", "fallback-a"}},
      LibXR::Entry<DeviceB>{dev_b, {"b", "shared"}},
      LibXR::Entry<DeviceC>{dev_c, {"c-only"}});

  ASSERT(hw.Find<DeviceA>("a") == &dev_a);
  ASSERT(hw.Find<DeviceB>("b") == &dev_b);
  ASSERT(hw.Find<DeviceC>("c-only") == &dev_c);

  ASSERT(hw.Find<DeviceA>("shared") == &dev_a);
  ASSERT(hw.Find<DeviceB>("shared") == &dev_b);
  ASSERT(hw.Find<DeviceC>("shared") == nullptr);

  ASSERT(hw.Find<DeviceA>("missing") == nullptr);
  ASSERT(hw.Find<DeviceB>({"missing", "shared"}) == &dev_b);
  ASSERT(hw.Find<DeviceA>({"missing", "fallback-a"}) == &dev_a);
  ASSERT(hw.Find<DeviceC>({"missing", "shared"}) == nullptr);
  ASSERT(hw.Find<DeviceA>({}) == nullptr);

  hw.Register(LibXR::Entry<DeviceD>{dev_d, {"d", "shared-d"}});
  ASSERT(hw.Find<DeviceD>("d") == &dev_d);
  ASSERT(hw.Find<DeviceD>({"missing", "shared-d"}) == &dev_d);
  ASSERT(hw.Find<DeviceA>("shared-d") == nullptr);
}
