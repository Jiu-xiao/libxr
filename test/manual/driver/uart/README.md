# UART 手动测试 / Manual UART test

将 TX 接到 RX，反复发送不同长度的数据并逐字节回读。每个长度分别使用阻塞、轮询和回调完成方式。

Wire TX to RX and repeatedly send and read back different packet lengths. Each length uses blocking, polling and callback completion.

## 接线和准备 / Wiring and setup

由应用初始化串口，使用 8 位数据和匹配的收发设置，配置好中断及所需的 DMA。RX 不能接其他发送端。测试期间独占串口，包括关闭该串口的日志输出；开始前接收队列须为空，且没有其他未完成的收发操作。

Initialize the UART with 8-bit data and matching transmit/receive settings, including interrupts and any required DMA. Connect no other transmitter to RX. Reserve the UART, including disabling console output on it. Start with an empty receive queue and no other pending transfers.

准备两个独立的 RAM 工作缓冲区，不得相互重叠，也不能与后端正在使用的缓冲区重叠。每个长度必须大于零，并同时不超过两个工作缓冲区和读写端口的容量。

Provide two separate RAM work buffers, disjoint from each other and active backend buffers. Each length must be positive and fit both work buffers and both port capacities.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入头文件搜索路径，从普通任务调用：

Add `test/` and `test/manual/driver/` to the include paths and call from a normal task:

```cpp
#include "uart/test_uart.hpp"

void CheckUART(LibXR::UART& uart, LibXR::RawData tx, LibXR::RawData rx,
               std::initializer_list<size_t> lengths)
{
  LibXR::Test::TestUARTLoopback(uart, tx, rx, lengths);
}
```

长度列表由应用选择，可包含短包、较长包和容量边界。最后两个参数是 `timeout_ms` 和 `iterations`，默认 1000 ms 和 100 轮。总包数为“长度数量 × 3 × 轮数”。重复传输能让队列读写位置回绕；具体 DMA 边界由板级工程选择长度和轮数来触及。

Choose short, longer and capacity-boundary lengths in the application. The final arguments are `timeout_ms` and `iterations`, defaulting to 1000 ms and 100 rounds. Total packets are “number of lengths × 3 × rounds”. Repeated transfers wrap queue positions; the board project chooses lengths and rounds to reach its DMA boundaries.

## 检查内容 / Checks

阻塞方式先发再读。轮询和回调方式先提交读取，再发送，检查没有数据时挂起的读取能否完成。收发各用一套完成状态，允许操作同步完成，也允许稍后完成。

Blocking mode writes before reading. Polling and callback modes submit the read before writing, checking that a read submitted without available data completes later. TX and RX use separate completion state; both inline and deferred completion are accepted.

每次 `Write()` 返回后立即改写发送源缓冲区。回读仍须符合原始数据的生成规则，不能跟着发送源一起改变。接收区预先填入错误值，防止漏写后误通过。每包前后检查接收队列为空，整个测试不调用清空队列来掩盖残留数据。

Overwrite the transmit source immediately after each `Write()` returns. Received bytes must still match the original generated pattern. Prefill RX with incorrect bytes to catch missing writes. Check that the receive queue is empty before and after each packet; never clear it to hide residual data.

发送完成表示后端已接纳数据，不代表数据已全部发上线路，所以测试还必须等接收完成并核对内容。回调只更新原子状态，错误由调用线程通过 `TEST_ASSERT` 报告并停止。超时从各次提交返回后计算，不能强行中断后端调用内部的同步执行。

TX completion means backend acceptance, not necessarily that all bytes have left the wire. The test also waits for RX and checks its contents. Callbacks update only atomic state; the calling thread reports failures through `TEST_ASSERT` and stops. Completion timeouts start when each submission returns and cannot preempt synchronous work inside a backend call.

测试不创建辅助线程，不修改串口配置。正常返回时收发均已完成，工作缓冲区保留最后一包的测试内容。CI 不自动执行接线测试。

The test creates no helper threads and leaves UART configuration unchanged. On normal return, both directions have completed and the work buffers retain the final packet's test contents. CI does not run this wired test automatically.

每次只处理一个容量以内的数据包。这项收发测试不验证大包流式传输、多请求突发和错误恢复。配置和速率用下面的独立入口检查。

The loopback test handles one packet within capacity at a time. It does not verify large streaming transfers, request bursts or error recovery. Use the separate entry below to check configuration and transfer rate.

## 配置和耗时 / Configuration and timing

`TestUARTConfig()` 设置一组配置，使用阻塞方式连续收发，逐包检查数据，并返回各包传输耗时之和。计时从 `Write()` 前开始，到完整 `Read()` 返回后结束，包含后端及调度开销，不包含数据准备和核对。没有额外的逐包休眠。

`TestUARTConfig()` applies one configuration, transfers packets with blocking operations, checks every packet and returns the sum of transfer times. Each interval starts before `Write()` and ends after the complete `Read()` returns. It includes backend and scheduling overhead, excludes data preparation and checking, and adds no per-packet sleep.

传入等长的独立收发缓冲区，每个缓冲区大小就是单包字节数，仍须不超过两个端口容量。支持按配置生成最多 8 位的数据；例如 7 位数据不会发送超出 0～127 的数值。微秒时间基须已初始化且分辨率足够。

Supply separate, equal-size buffers; each buffer's size is the packet length and must fit both ports. Generated values respect up to 8 configured data bits; for example, 7-bit data stays within 0–127. Initialize a microsecond timebase with sufficient resolution.

接收字节只有配置指定的低位是有效数据，高位没有规定值，由上层按需屏蔽。因此 7 位模式按 `received & 0x7F` 比较，8 位模式比较完整字节。测试不改写收到的原始字节。

Only the configured low bits of a received byte are valid data. Upper bits are unspecified and callers mask them when needed. The test therefore compares `received & 0x7F` in 7-bit mode and the whole byte in 8-bit mode, leaving the raw received bytes unchanged.

```cpp
uint64_t CheckUARTConfig(LibXR::UART& uart, LibXR::UART::Configuration config,
                         LibXR::RawData tx, LibXR::RawData rx,
                         uint64_t min_us, uint64_t max_us)
{
  // 100 包传输时间的允许范围，由调用工程预先确定。
  // The calling project determines the allowed time for 100 packets in advance.
  return LibXR::Test::TestUARTConfig(uart, config, tx, rx, min_us, max_us);
}
```

最后两个可选参数是包数 `iterations`（默认 100）和每次阻塞调用的超时 `timeout_ms`（默认 1000）。调用前须确认串口空闲、没有残留接收数据。后端应能用本次空闲配置请求设置的参数执行后续收发；`SetConfig()` 返回成功本身不等于已经验证参数生效。

The final optional arguments are `iterations` (100 packets by default) and `timeout_ms` (1000 per blocking call). Start with an idle UART and no queued receive data. The backend must use the accepted idle configuration for subsequent transfers; a successful `SetConfig()` return alone does not verify that it took effect.

耗时上下限由调用方在运行前给出，不从测量结果反推。线路时间可按“字节数 × 包数 × 每帧位数 ÷ 波特率”估算；再为调用、接收通知、协议间隙和调度延迟留出合理余量。范围必须足够窄，能区分准备测试的快慢档。返回值是含这些开销的时间，不是精确的波特率测量。

Supply time bounds before running; do not derive them from the observed result. Estimate wire time as “bytes × packets × bits per frame ÷ baud rate”, then allow for calls, receive notifications, protocol gaps and scheduling latency. Keep the windows narrow enough to distinguish the selected fast and slow settings. The returned time includes these overheads and is not a precise baud-rate measurement.

依次用 A、A、B、A 调用，可以检查重复设置和切换后恢复。每次调用前按后端要求等待线路空闲，这段等待放在测试外。函数正常返回后保留新配置，不自动恢复；需要恢复时由调用方再次设置原配置。

Call with A, A, B, A to check repeated setup and switching back. Wait for line idle as required by the backend before each call, outside the test. A successful call retains the new configuration; the caller explicitly reapplies the original configuration when needed.

耗时不能区分所有帧格式。例如 8E1 与 8N2 都是每字节 11 位。回环数据和耗时检查可发现部分错误配置，但不能代替独立接收端或逻辑分析仪对校验位、停止位和线路速率的确认。

Timing cannot distinguish every frame format: 8E1 and 8N2 both use 11 bits per byte. Loopback data and timing can detect some configuration errors but do not replace an independent receiver or logic analyzer for verifying parity, stop bits and line rate.
