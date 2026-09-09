# I2C 手动测试 / Manual I2C test

提供固定寄存器读取和可读写寄存器的写入回读两项测试。两项都使用阻塞、轮询和回调三种完成方式，等当前操作完成后才开始下一次。

Two tests cover fixed-register reads and write/readback of writable registers. Both use blocking, polling and callback completion, starting the next operation only after the current one completes.

## 准备设备 / Prepare the device

调用方初始化 I2C 总线、从机、系统时间基和中断／DMA 资源，并独占总线。确认供电、共地和 SDA/SCL 上拉电平符合两端要求。

Initialize the bus, slave, system timebase and required interrupt/DMA resources, and reserve the bus. Check power, common ground and SDA/SCL pull-up levels against both devices' requirements.

固定读取测试选择内容固定、没有清除状态等读取副作用的寄存器，例如已确认的器件标识。预期值来自器件资料或其他独立依据，不能先读取一次，再把读取结果当作正确答案。不要用持续变化的传感器测量值作固定值比较。

For the fixed-read test, choose stable registers without read side effects such as clearing status, for example a verified device identifier. Derive expected bytes from device documentation or other independent evidence, not from a previous read of the same device. Do not compare changing sensor measurements against fixed bytes.

从机地址不带 R/W 位；7 位或 10 位寻址模式由调用方配置。寄存器地址长度明确传入 `BYTE_8` 或 `BYTE_16`。连续读取多个字节时，确认从机支持这种访问方式，而且长度没有超过后端的传输或 DMA 缓冲区限制。

Supply the slave address without the R/W bit; configure 7-bit or 10-bit addressing in the caller. Explicitly select `BYTE_8` or `BYTE_16` for the register address. For multi-byte reads, verify the slave's access behavior and the backend's transfer/DMA buffer limits.

## 固定寄存器读取 / Fixed-register reads

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从普通任务调用：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal task:

```cpp
#include "i2c/test_i2c.hpp"

void CheckRegisters(LibXR::I2C& bus, uint16_t device, uint16_t reg,
                    LibXR::I2C::MemAddrLength address_length,
                    LibXR::ConstRawData expected, LibXR::RawData buffer)
{
  LibXR::Test::TestI2CMemRead(bus, device, reg, address_length, expected, buffer);
}
```

`expected` 的长度决定每次读取多少字节，`buffer` 至少需要同样大小。预期数据必须保持不变，且不能与实际接收区重叠。调用期间保留总线对象及两个缓冲区；后端完成操作后不得继续访问本次缓冲区或通知对象。

The length of `expected` determines the transfer size; `buffer` must be at least that large. Expected bytes must remain unchanged and must not overlap the receive area. Keep the bus and both buffers alive throughout the call; completed operations must not continue accessing their buffers or notification targets.

最后两个参数为 `timeout_ms` 和 `iterations`，默认 1000 ms、1000 轮，即 3000 次读取。次数填 1 可快速检查。超时值传给 BLOCK 操作，并限制 `MemRead()` 返回后的轮询或回调等待；它不能强行中断后端在 `MemRead()` 内部执行的同步调用。

The final arguments are `timeout_ms` and `iterations`, defaulting to 1000 ms and 1000 rounds, or 3000 reads. Use one round for a quick check. The timeout is passed to BLOCK operations and bounds polling/callback waits after `MemRead()` returns; it cannot preempt synchronous work inside the backend call.

## 写入后回读 / Write and read back

`TestI2CMemWriteRead` 交替写入调用方提供的两组数据，每次写入完成后按指定时间等待，再回读并逐字节比较。两组数据必须不同、长度相同，而且都是目标区域允许的值；测试不会自行生成反码去修改未知控制位。

`TestI2CMemWriteRead` alternates two caller-provided patterns. After each write completes, wait the specified interval, read back and compare every byte. Patterns must be distinct, equal-sized and legal for the selected region; the test does not invent inverted values for unknown control bits.

```cpp
LibXR::Test::TestI2CMemWriteRead(bus, device, reg, address_length,
                                first_pattern, second_pattern, buffer);
```

输入区与接收区不能重叠。目标必须支持写入后原样回读，不能选具有命令副作用或无法原样回读的寄存器。分页、写入寿命和写后稳定时间由调用方按实际器件确认。

Neither pattern may overlap the receive area. Choose registers that read back the written bytes, without command side effects. The caller must verify paging, write endurance and settling time for the device.

最后三个参数为 `settle_ms`、`timeout_ms` 和 `iterations`，默认 0 ms、1000 ms 和 100 轮。每轮在三种完成方式下分别写入两组数据，共六次写入和六次回读。`settle_ms` 从写完成开始计时；非易失存储器尤其需要按写周期和寿命调整参数。

The final three arguments are `settle_ms`, `timeout_ms` and `iterations`, defaulting to 0 ms, 1000 ms and 100 rounds. Each round uses both patterns in all three completion modes: six writes and six reads. Settling starts after write completion; adjust it and the round count for nonvolatile memory's write cycle and endurance.

成功后保留第二组数据。需要保留原值时，由调用工程在测试前保存，成功后恢复并核对。测试失败会立即停止，不能承诺失败时已恢复原值。

Success leaves the second pattern in place. If old values must be preserved, the calling application saves them before testing, then restores and verifies them after success. Failure stops immediately; restoration after a failed test is not guaranteed.

## 检查内容 / Checks

每次读取前，接收区填入预期值的反码，防止未复制数据却误通过。阻塞方式检查调用结果；轮询方式等待 `DONE` 并拒绝 `ERROR`；回调方式检查成功结果及观测到的回调次数。最后逐字节比较数据，任何错误都立即触发始终生效的 `TEST_ASSERT`，不重试。

Before each read, fill the receive area with inverted expected bytes so missing copies cannot pass. Blocking mode checks the call result; polling waits for `DONE` and rejects `ERROR`; callback mode checks success and observed completion counts. Compare every byte after completion. Any failure immediately triggers the always-active `TEST_ASSERT`, without retries.

回调可能在提交函数内直接执行，也可能稍后从任务或 ISR 执行。测试不写死回调上下文，回调中只更新原子计数和错误标志；报错在调用线程进行。回调只创建一次并在所有轮次中复用，循环中不创建线程或分配通知对象。测试失败会停止，不会带着未完成的操作返回。

Callbacks may run inline during submission or later in a task or ISR. The test does not assume one callback context; callbacks only update atomic counts and an error flag, while diagnostics run in the calling thread. A callback is created once and reused across rounds; no threads or notification objects are created in the loop. A failed test stops rather than returning with an operation pending.

CI 不自动运行这项设备测试。寄存器读写通过，只证明相应路径；它不代表长包 DMA、普通 `Read/Write`、总线速率或异常恢复已经验证。设备型号、具体地址、寄存器与接线留在调用工程中。

CI does not run this device test automatically. Passing register read/write checks only validates the exercised paths; it does not establish long-transfer DMA, plain `Read/Write`, bus speed or error recovery. Keep device models, addresses, registers and wiring in the calling project.
