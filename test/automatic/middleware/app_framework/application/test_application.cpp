/**
 * @file test_application.cpp
 * @brief ApplicationManager 注册与巡检测试 / ApplicationManager registration and
 * monitoring tests.
 *
 * 检查注册数量，以及每轮 MonitorAll 对每个应用恰好调用一次 OnMonitor。
 * Check registration counts and one OnMonitor call per application on each MonitorAll.
 */

#include "libxr.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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
  TEST_ASSERT(manager.Size() == 0);

  manager.Register(app1);
  TEST_ASSERT(manager.Size() == 1);

  manager.Register(app2);
  manager.Register(app3);
  TEST_ASSERT(manager.Size() == 3);

  manager.MonitorAll();
  TEST_ASSERT(hit_count == 3);
  TEST_ASSERT(seen_mask == 0x07);

  manager.MonitorAll();
  TEST_ASSERT(hit_count == 6);
  TEST_ASSERT(seen_mask == 0x07);
}
