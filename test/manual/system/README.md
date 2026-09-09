# 系统手动测试 / Manual system tests

这里用于需要实际系统环境的测试，例如 RTOS 上的线程调度、信号量唤醒和时钟行为。目前还没有测试代码，也不由 CI 执行。

This directory is for tests that need an actual system, such as thread scheduling, semaphore wakeups and clock behavior on an RTOS. It has no test code yet and is not run by CI.

测试使用 LibXR 的线程、同步和时间接口。调用方先初始化系统，再调用所需测试。每项测试需要说明：

Tests use LibXR APIs for threads, synchronization and time. The caller initializes the system before calling a test. Each test should describe:

- 所需系统能力，例如能否创建并发任务，以及时钟和调度配置。
  Required system capabilities, such as concurrent task creation, and the clock and scheduler configuration.
- 入口函数、参数和需要准备的资源。
  The entry function, arguments and resources to prepare.
- 预期结果，以及辅助任务何时结束、资源何时可以释放。
  Expected results, when helper tasks finish and when resources can be released.

检查失败时立即停止，使用与 automatic 相同的测试断言方式，便于在调试器中定位。

A failed check stops execution immediately, using the same test assertion behavior as automatic tests so the failure can be inspected in a debugger.
