# CAN / CAN FD 手动测试 / Manual CAN and CAN FD tests

使用控制器内部回环，逐帧检查经典 CAN 标准数据帧和扩展数据帧。每轮各发送 DLC 0～8 的帧，共 18 帧；第一轮包含 ID 的最小值和最大值，其余帧使用变化的 ID 和数据。

Use controller internal loopback to check classic CAN standard and extended data frames. Each round sends DLC 0 through 8 for both types, for 18 frames total. The first round includes minimum and maximum IDs; other frames use varying IDs and data.

## 准备控制器 / Prepare the controller

调用方初始化系统、CAN 位时序、内部回环、接收过滤器和中断，并独占控制器。硬件过滤器应允许全部标准和扩展数据帧进入驱动。内部回环不需要外部收发器接线。

Initialize the system, CAN bit timing, internal loopback, receive filters and interrupts, and reserve the controller. Hardware filters must admit all standard and extended data frames to the driver. Internal loopback needs no external transceiver wiring.

构造测试对象前清理旧通信，确保没有排队帧或在途回调。接收回调须由后端串行调用；测试用一个小 SPSC 队列复制回调帧，不保留后端临时数据的引用。

Finish old traffic before constructing the test object, with no queued frames or callbacks in flight. The backend must serialize receive callbacks. A small SPSC queue copies each callback frame rather than retaining a reference to temporary backend data.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径。在控制器配置完成后，从普通任务调用：

Add `test/` and `test/manual/driver/` to the application's include paths. Once the controller is configured, call from a normal task:

```cpp
#include "can/test_can.hpp"

// 只调用一次，can 对象也保留到复位。
// Call once; retain the CAN object until reset too.
void RunCANTest(LibXR::CAN& can)
{
  static LibXR::Test::CANLoopbackTest test(can);
  test.Run();
}
```

`Run(timeout_ms, iterations)` 默认每帧等待上限为 1000 ms，重复 1000 轮，即 18000 帧。最后一个参数填 1 可快速检查。超时限制提交返回后的接收等待，不能强行中断后端在 `AddMessage()` 内部的同步执行。

`Run(timeout_ms, iterations)` defaults to a 1000 ms receive wait per frame and 1000 rounds, or 18000 frames. Use one round for a quick check. The timeout bounds receive waiting after submission returns; it cannot preempt synchronous work inside `AddMessage()`.

**测试对象和 CAN 对象必须保持地址不变并保留到复位，每个对象只运行一次。** 当前接口没有注销订阅的方法。队列和订阅在构造时创建，循环中不分配内存或创建辅助线程。

**Keep the test and CAN objects at stable addresses until reset, and run once per test object.** The current API cannot unregister subscriptions. Queue and subscriptions are created during construction; no memory or helper threads are allocated in the loop.

## 检查内容 / Checks

`AddMessage()` 成功只表示提交成功，测试必须收到对应帧才继续。每次只保留一个未完成帧，核对 ID、类型、DLC 和有效载荷，不假设多帧排队时的仲裁顺序。DLC 为零时不比较数据数组，其他帧也不比较 DLC 之外的字节。

Successful `AddMessage()` only means submission succeeded; the test waits for the matching received frame. Only one frame is outstanding. Check ID, type, DLC and valid payload without assuming arbitration order for queued frames. Compare no payload for DLC zero and ignore bytes beyond DLC for other frames.

回调只复制帧并更新原子状态，检查和报错在调用任务进行。错误事件、漏帧、队列溢出、数据不符或观测到的多余回调都会使 `TEST_ASSERT` 停止测试，不会丢弃异常后重试。

Callbacks only copy frames and update atomic state; checks and diagnostics run in the caller. Error events, missing frames, queue overflow, mismatches or observed extra callbacks stop the test through `TEST_ASSERT`, without discarding failures and retrying.

正常结束后不再发送，但内部回环配置和订阅仍保留。最后有 1 ms 的额外回调观察窗口，它不能证明以后永远没有迟到帧，也不能替代注销或回调退出确认。

On success, sending stops but loopback configuration and subscriptions remain. A final 1 ms observation window checks for extra callbacks; it cannot rule out all later frames or replace unsubscribe or callback-exit confirmation.

上述经典 CAN 测试不验证 CAN FD、远程帧、真实总线仲裁与 ACK、收发器、终端电阻或 BUS-OFF 恢复。内部回环的通过不能代替外部总线测试，CI 不自动运行此项测试。

The classic CAN test above does not validate CAN FD, remote frames, real bus arbitration or ACK, transceivers, termination or bus-off recovery. Passing internal loopback does not replace an external bus test. CI does not run this test automatically.


## CAN FD 内部回环 / CAN FD internal loopback

FD 测试使用独立的 `FDCANLoopbackTest`，同样要求控制器独占、回调串行、对象保留到复位，每个测试对象只运行一次。先由调用方启用 CAN FD，并配置仲裁相位、数据相位和内部回环。

Use the separate `FDCANLoopbackTest` for FD frames. The same exclusive-controller, serialized-callback and retain-until-reset requirements apply; run once per test object. The caller first enables CAN FD and configures nominal/data phase timing and internal loopback.

```cpp
#include "can/test_fdcan.hpp"

void RunFDTest(LibXR::FDCAN& can)
{
  static LibXR::Test::FDCANLoopbackTest test(can);
  test.Run();
}
```

每轮分别发送标准和扩展 FD 帧，长度为 `0～8、12、16、20、24、32、48、64` 字节，共 32 帧。默认 1000 轮，即 32000 帧。检查 ID、类型、长度和有效数据，并检查 0～8 字节的短 FD 帧没有被误送到经典 CAN 回调。

Each round sends standard and extended FD frames at lengths `0–8, 12, 16, 20, 24, 32, 48, 64` bytes, for 32 frames. The default 1000 rounds check 32000 frames. Validate ID, type, length and valid data, and reject short FD frames incorrectly routed to classic CAN callbacks.

这里只使用 FD 可直接编码的长度，不规定其他长度如何补齐。当前 `FDPack` 回调没有 BRS、ESI 字段，因此它们不作为逐帧回读判据。通过内部回环也不能证明外部总线、电气时序或实际相位速率正确。

Only canonical FD lengths are used; no padding policy is imposed for other lengths. The current `FDPack` callback exposes no BRS or ESI fields, so they are not checked per frame. Internal loopback also does not prove external bus, electrical timing or actual phase-rate correctness.
