/**
 * @file test_application.cpp
 * @brief `ApplicationManager` 注册与调度测试。 `ApplicationManager` registration and dispatch tests.
 *
 * 测试项目 / Test items:
 * 1. 注册计数行为。 Registration accounting: verify manager size grows with each registered application.
 * 2. `MonitorAll()` 的覆盖与重复调用行为。 Monitor dispatch coverage: verify `MonitorAll()` reaches every registered application and can be called repeatedly.
 *
 * 测试原理 / Test principles:
 * 1. 使用 seen bitmask 和 hit count，而不是假定遍历顺序，因为源码本身不保证顺序。 Record a seen-bitmask and hit count instead of asserting callback order, because the source contract explicitly does not guarantee traversal order.
 * 2. 重复调用 `MonitorAll()`，验证 steady-state 行为而不只是一轮调用。 Call `MonitorAll()` more than once so the test documents steady-state repeatability rather than only first-use behavior.
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
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
