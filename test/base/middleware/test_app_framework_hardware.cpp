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
