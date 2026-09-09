# ASync 测试 / ASync tests

这组测试检查任务从 `READY` 到 `BUSY`，完成后变为 `DONE`，读取完成状态后又回到 `READY`。两个提交入口都在 BUSY 或尚未取走的 DONE 状态下拒绝新任务；并发读取完成状态时，只能有一个读取者取得 DONE。

This group checks the transition from `READY` to `BUSY`, then to `DONE` when the job finishes, and back to `READY` after the completion status is read. Both submission entries reject jobs while BUSY or while DONE is unconsumed. Concurrent status readers must consume each completion only once.

回调先等信号量，让主线程有机会检查 `BUSY`；放行后，主线程等到 `DONE` 再读取结果。因此测试不依赖“睡一会儿，任务应该已经完成”的假设。

The callback first waits on a semaphore so the main thread can check `BUSY`. Once released, the main thread waits for `DONE` before reading the result. The test does not assume that sleeping for a while means the job has finished.

当前入口不单独隔离进程。ASync 的工作线程常驻，测试对象和回调数据需要在整个测试进程运行期间保持有效；具体存储方式见 `test_async.cpp`。

This entry does not use a separate process. ASync has a permanent worker, so the test object and callback data remain valid for the lifetime of the test process. See `test_async.cpp` for their storage.

按[测试说明](../../../README.md)构建后，在仓库根目录运行：

After building with the [test instructions](../../../README.md), run from the repository root:

```sh
script -q -e -c './build/test/test --case async' /dev/null
```

`test_async_cooperative` 是另一个独立测试程序，编译实际裸机后端并控制时间推进，检查 Timer 延后执行、任务完整发布和重复使用。它通过根 CTest 运行，不使用硬件中断。

`test_async_cooperative` is a separate executable using the real bare-metal backend and a controlled clock. It checks deferred Timer execution, complete publication and reuse through root CTest, without hardware interrupts.

```sh
ctest --test-dir build -R '^test_async_cooperative$' --output-on-failure --no-tests=error
```
