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

class CountingApp : public LibXR::Application
{
 public:
  CountingApp(int id, int* log, size_t* index) : id_(id), log_(log), index_(index) {}

  void OnMonitor() override { log_[(*index_)++] = id_; }

 private:
  int id_;
  int* log_;
  size_t* index_;
};

}  // namespace

void test_app_framework()
{
  DeviceA dev_a;
  DeviceB dev_b;
  DeviceC dev_c;

  LibXR::HardwareContainer hw(
      LibXR::Entry<DeviceA>{dev_a, {"a", "shared"}},
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
  ASSERT(hw.Find<DeviceC>({"missing", "shared"}) == nullptr);

  int monitor_log[4] = {0, 0, 0, 0};
  size_t monitor_index = 0;
  CountingApp app1(1, monitor_log, &monitor_index);
  CountingApp app2(2, monitor_log, &monitor_index);

  LibXR::ApplicationManager manager;
  ASSERT(manager.Size() == 0);
  manager.Register(app1);
  manager.Register(app2);
  ASSERT(manager.Size() == 2);

  manager.MonitorAll();
  ASSERT(monitor_index == 2);
  ASSERT(monitor_log[0] == 1);
  ASSERT(monitor_log[1] == 2);
}
