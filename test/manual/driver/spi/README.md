# SPI 手动测试 / Manual SPI test

将主机 MOSI 接到 MISO，调用 `ReadAndWrite()` 同时发送和接收已知数据。每个指定长度都分别检查阻塞、轮询和回调完成方式，逐字节验证接收数据和发送缓冲区。

Wire the master's MOSI to MISO and use `ReadAndWrite()` to transmit and receive known data simultaneously. For every selected length, check blocking, polling and callback completion, received bytes and the transmit buffer contents.

## 接线和准备 / Wiring and setup

初始化为 **8 位帧、全双工主机**，MISO 上不能同时连接其他输出。调用方完成时钟、模式、片选和中断／DMA 配置，并独占总线。先选择适合回环接线的保守速率。

Configure an **8-bit full-duplex master**, with no other output driving MISO. The caller configures clock, mode, chip select and interrupt/DMA resources and reserves the bus. Start at a conservative rate suitable for the loopback wiring.

传入两个独立的 RAM 工作缓冲区，不得相互重叠，也不能使用后端当前正在操作的缓冲区。各次传输的长度须满足实际容量、对齐和内存访问要求。测试不更改 SPI 配置。

Supply two separate RAM work buffers, disjoint from each other and the backend's active buffers. Transfer lengths must meet the actual capacity, alignment and memory-access requirements. The test does not change the SPI configuration.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从普通任务调用。由应用提供缓冲区和适用的长度列表：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal task. Supply the buffers and appropriate lengths from the application:

```cpp
#include "spi/test_spi.hpp"

void CheckSPI(LibXR::SPI& spi, LibXR::RawData tx, LibXR::RawData rx,
               std::initializer_list<size_t> lengths)
{
  LibXR::Test::TestSPILoopback(spi, tx, rx, lengths);
}
```

选择短包、较长包以及合法的边界长度。如果后端按长度选择不同传输方式，应由调用工程选择能触及这些分支的长度；通用测试不内置 DMA 阈值。

Select short, longer and valid boundary lengths. If the backend chooses a transfer path based on length, the calling project should select lengths that exercise those paths. The generic test contains no DMA threshold.

最后两个参数为 `timeout_ms` 和 `iterations`，默认 1000 ms 和 1000 轮。总传输次数为“长度数量 × 3 × 轮数”，最后一个参数填 1 可快速检查。

The final arguments are `timeout_ms` and `iterations`, defaulting to 1000 ms and 1000 rounds. Total transfers are “number of lengths × 3 × rounds”; use one round for a quick check.

## 检查内容 / Checks

发送内容随位置、长度、轮次和完成方式变化。接收区预先填入错误值，防止没有复制数据却误通过。完成后，收发内容都与生成规则比较，不会仅凭两个缓冲区相等就判定成功。

Transmit data varies with position, length, round and completion mode. Prefill the receive area with incorrect bytes to detect missing copies. After completion, compare both buffers against the generated pattern rather than merely checking that they match each other.

测试复用与 I2C 测试相同的完成等待辅助代码，允许同步或异步完成。回调只更新原子状态，所有报错在调用线程进行。超时参数不能强行中断后端调用内部的同步执行；失败触发 `TEST_ASSERT` 并停止，不带着未完成的操作返回。

The test reuses the completion-wait helper used by I2C tests and accepts inline or deferred completion. Callbacks only update atomic state; diagnostics run in the calling thread. The timeout cannot preempt synchronous work inside the backend call. Failure triggers `TEST_ASSERT` and stops rather than returning with a pending operation.

两个工作缓冲区都会被改写，正常结束后保留最后一帧数据。测试不创建辅助线程，回调和等待对象在轮次间复用；CI 不自动运行接线测试。

Both work buffers are overwritten and retain the final frame on success. No helper threads are created; callback and wait state are reused between rounds. CI does not run the wired test automatically.

## 验证范围 / Scope

这项回环检查等长全双工数据通路。它不验证单独 `Read()`／`Write()` 的空方向处理、`MemRead()`／`MemWrite()` 的从机协议、`Transfer()` 或双缓冲的专门行为。片选时序、实际时钟频率和模式是否符合外部设备要求，也不能仅靠同一控制器的自回环证明。

This loopback checks equal-length full-duplex data transfers. It does not validate empty-direction handling in separate `Read()`/`Write()` calls, slave protocols in `MemRead()`/`MemWrite()`, or the dedicated behavior of `Transfer()` and double buffering. Self-loopback on one controller also cannot independently prove chip-select timing, actual clock frequency or mode compliance with an external device.
