# Timer 测试 / Timer tests

Timer 有两组测试，各自在新进程中运行，避免前一组留下的定时器或调度线程影响后一组。

Timer has two groups, each run in a fresh process so timers or scheduler threads left by one group cannot affect the other.

- `timer_semantics`：手动调用 `Refresh()` 推进时间，检查周期边界、停止后保留的计数，以及 `SetCycle()` 的效果。这里不启动 Timer 调度线程，每一步应该触发几次回调都能确定。
  Calls `Refresh()` manually to check period boundaries, the count retained while stopped, and the effect of `SetCycle()`. No Timer scheduler thread runs, so the callback count after each step is predictable.
- `timer_scheduler`：启动真正的 Timer 调度线程。回调执行到第三次时停止定时器并通知主线程，主线程最多等待一秒。这里检查调度能否完成，不把这一秒当作精确周期测量。
  Starts the real Timer scheduler. The third callback stops the timer and notifies the main thread, which waits up to one second. This checks that scheduling completes; the timeout is not a precise period measurement.

按[测试说明](../../../README.md)构建后，在仓库根目录运行：

After building with the [test instructions](../../../README.md), run from the repository root:

```sh
script -q -e -c './build/test/libxr_test --case timer_semantics' /dev/null
script -q -e -c './build/test/libxr_test --case timer_scheduler' /dev/null
```
