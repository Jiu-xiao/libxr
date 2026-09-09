# GPIO 手动测试 / Manual GPIO tests

用一根线连接两个专用引脚：一个推挽输出，一个输入。测试检查高低电平回读，以及上升沿、下降沿或双沿中断。未测试开漏、内部上下拉的电气特性。

Connect two dedicated pins with a wire: one push-pull output and one input. The tests check level readback and rising, falling or both-edge interrupts. Open-drain and internal pull resistor electrical behavior are not tested.

## 接线和准备 / Wiring and setup

两个引脚必须是不同的物理引脚，电压兼容、共地，没有其他输出驱动这条线。先初始化系统和 GPIO 对象，再从普通任务调用测试；不要从 ISR 或 GPIO 回调中调用。

Use distinct physical pins with compatible voltages and a common ground. No other output may drive the wire. Initialize the system and GPIO objects first, then call from a normal task, never from an ISR or GPIO callback.

读写测试要求输入中断关闭。中断测试还要求输入支持所选边沿，已配置有效中断资源，没有旧挂起事件或在途回调。测试期间独占这些引脚及中断资源；共享中断组的其他设备也不能使用它。

Disable input interrupts before the loopback test. The interrupt test additionally requires support for the selected edge, a valid interrupt resource, and no old pending events or callbacks in progress. Reserve the pins and interrupt resources for the test, including any shared interrupt group.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径。只需包含头文件，无需另加测试源文件或执行器。

Add `test/` and `test/manual/driver/` to the application include paths. Include the header; no extra test source or runner is needed.

```cpp
#include "gpio/test_gpio.hpp"

// 只调用一次；两个 GPIO 对象由应用保留到复位。
// Call once; the application retains both GPIO objects until reset.
void RunGPIOTests(LibXR::GPIO& output, LibXR::GPIO& input)
{
  LibXR::Test::TestGPIO(output, input);

  // MCU ISR 回调填 true；线程派发回调的平台（如 Linux）填 false。
  // Use true for MCU ISR callbacks, false for task dispatch such as Linux.
  static LibXR::Test::GPIOInterruptTest irq_test(output, input, true);
  irq_test.Run(LibXR::GPIO::Direction::FALL_RISING_INTERRUPT);
}
```

`TestGPIO(output, input, settle_ms, timeout_ms, iterations)` 每轮检查低、高电平。`GPIOInterruptTest::Run(edge, settle_ms, timeout_ms, iterations)` 每轮检查所选边沿应出现的回调和另一边沿不应出现的回调；双沿模式每轮应有两次回调。随后禁用中断，再翻转同样多轮，检查没有新回调。

`TestGPIO(output, input, settle_ms, timeout_ms, iterations)` checks low and high levels each round. `GPIOInterruptTest::Run(edge, settle_ms, timeout_ms, iterations)` checks expected and unwanted callbacks for the selected edges; both-edge mode expects two callbacks per round. It then disables interrupts and repeats the toggles for the same number of rounds, checking for no new callbacks.

两项默认各运行 1000 轮，最后一个参数填 1 可快速检查。中断模式也可选择 `RISING_INTERRUPT` 或 `FALL_INTERRUPT`，每次初始化只测一种模式，不要在同一个对象上连续调用 `Run()`。

Both default to 1000 rounds; set the last argument to 1 for a quick check. Select `RISING_INTERRUPT` or `FALL_INTERRUPT` to check a single edge. Test one mode per initialization; do not call `Run()` repeatedly on the same object.

`settle_ms` 默认 1 ms，控制电平稳定后的复查和多余回调的观察间隔；`timeout_ms` 默认 1000 ms，是每次等待电平或回调的上限，不是整项测试的总时限。调度较慢时可增加这两个值。这些有限观察不能证明以后永远没有迟到回调，也不测最高翻转频率。

`settle_ms` defaults to 1 ms and controls level rechecks and observation for extra callbacks. `timeout_ms` defaults to 1000 ms and bounds each level or callback wait, not the whole test. Increase them for slower scheduling. These bounded observations cannot rule out all later callbacks and do not measure maximum toggle frequency.

## 结束状态 / After the test

检查失败会触发始终生效的 `TEST_ASSERT` 并立即停止。中断回调只写原子计数，检查和报错在调用线程进行。

A failed check triggers the always-active `TEST_ASSERT` and stops immediately. Interrupt callbacks only record atomic values; checks and diagnostics run in the calling thread.

正常返回后输出保持低电平。读写测试留下普通输入；中断测试留下关闭的中断和已注册的回调，不恢复原配置。GPIO 没有等待在途回调结束的接口，因此两个 GPIO 和中断测试对象都要保持地址不变并保留到复位。

On success, the output stays low. The loopback test leaves a normal input; the interrupt test leaves interrupts disabled and its callback registered. Neither restores the original configuration. GPIO has no API to wait for in-flight callbacks, so retain both GPIO objects and the interrupt test object at stable addresses until reset.

禁用期间的边沿可能留下硬件挂起位。再次使用该输入中断前，由平台初始化代码清理挂起状态或重新初始化；测试不会直接重新使能它。

Edges while disabled may leave hardware pending bits. Have platform setup clear pending state or reinitialize the input before using its interrupt again; the test does not re-enable it.
