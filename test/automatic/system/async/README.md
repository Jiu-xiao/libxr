# ASync 测试 / ASync tests

这组测试检查任务从 `READY` 到 `BUSY`，完成后变为 `DONE`，读取完成状态后又回到 `READY`。连续提交十次任务，检查每次都执行一次。

This group checks the transition from `READY` to `BUSY`, then to `DONE` when the job finishes, and back to `READY` after the completion status is read. It submits ten jobs in sequence and checks that each runs once.

回调先等信号量，让主线程有机会检查 `BUSY`；放行后，主线程等到 `DONE` 再读取结果。因此测试不依赖“睡一会儿，任务应该已经完成”的假设。

The callback first waits on a semaphore so the main thread can check `BUSY`. Once released, the main thread waits for `DONE` before reading the result. The test does not assume that sleeping for a while means the job has finished.

当前入口不单独隔离进程。ASync 的工作线程常驻，测试对象和回调数据需要在整个测试进程运行期间保持有效；具体存储方式见 `test_async.cpp`。

This entry does not use a separate process. ASync has a permanent worker, so the test object and callback data remain valid for the lifetime of the test process. See `test_async.cpp` for their storage.

按[测试说明](../../../README.md)构建后，在仓库根目录运行：

After building with the [test instructions](../../../README.md), run from the repository root:

```sh
script -q -e -c './build/test/test --case async' /dev/null
```
