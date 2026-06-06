/**
 * @file test_application.cpp
 * @brief `ApplicationManager` registration and dispatch tests.
 *
 * Test items:
 * 1. Registration accounting: verify manager size grows with each registered application.
 * 2. Monitor dispatch coverage: verify `MonitorAll()` reaches every registered application and can be called repeatedly.
 *
 * Test principle:
 * 1. Record a seen-bitmask and hit count instead of asserting callback order, because the source contract explicitly does not guarantee traversal order.
 * 2. Call `MonitorAll()` more than once so the test documents steady-state repeatability rather than only first-use behavior.
 */
#include "libxr.hpp"
#include "test.hpp"

namespace
{

class CountingApp : public LibXR::Application
{
 public:
  CountingApp(int id, int* seen_mask, int* hit_count)
      : id_(id), seen_mask_(seen_mask), hit_count_(hit_count)
  {
  }

  void OnMonitor() override
  {
    *seen_mask_ |= (1 << id_);
    (*hit_count_)++;
  }

 private:
  int id_;
  int* seen_mask_;
  int* hit_count_;
};

}  // namespace

void test_app_framework_application()
{
  int seen_mask = 0;
  int hit_count = 0;

  CountingApp app1(0, &seen_mask, &hit_count);
  CountingApp app2(1, &seen_mask, &hit_count);
  CountingApp app3(2, &seen_mask, &hit_count);

  LibXR::ApplicationManager manager;
  ASSERT(manager.Size() == 0);

  manager.Register(app1);
  ASSERT(manager.Size() == 1);

  manager.Register(app2);
  manager.Register(app3);
  ASSERT(manager.Size() == 3);

  manager.MonitorAll();
  ASSERT(hit_count == 3);
  ASSERT(seen_mask == 0x07);

  manager.MonitorAll();
  ASSERT(hit_count == 6);
  ASSERT(seen_mask == 0x07);
}
