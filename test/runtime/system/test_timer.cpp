/**
 * @file test_timer.cpp
 * @brief runtime 周期 timer start/stop 测试。 Runtime periodic timer start/stop behavior test.
 *
 * 测试项目 / Test items:
 * 1. timer 周期回调执行。 Timer periodic execution: verify a started timer repeatedly invokes its callback.
 * 2. stop/restart 后的重复可用性。 Stop/restart behavior: verify stopping and restarting the same timer handle still yields the expected periodic count.
 *
 * 测试原理 / Test principles:
 * 1. 使用真实 timer thread 和重复 restart 尝试，验证 runtime 调度而不是模拟回调循环。 Use the real timer thread and repeated restart attempts so the test checks runtime scheduling rather than a simulated callback loop.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "timer.hpp"

/**
 * @brief 测试入口函数 `test_timer`。 Test entry function `test_timer`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_timer()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  int timer_arg = 0;

  auto handle =
      LibXR::Timer::CreateTask<int*>([](int* arg) { *arg = *arg + 1; }, &timer_arg, 10);

  LibXR::Timer::Add(handle);
  LibXR::Timer::Start(handle);

  LibXR::Thread::Sleep(205);
  LibXR::Timer::Stop(handle);
  for (int i = 0; i < 10; i++)
  {
    timer_arg = 0;
    LibXR::Timer::Start(handle);
    LibXR::Thread::Sleep(205);
    LibXR::Timer::Stop(handle);
    if (timer_arg == 20)
    {
      break;
    }
  }

  ASSERT(timer_arg == 20);

  pthread_cancel(LibXR::Timer::thread_handle_);
}