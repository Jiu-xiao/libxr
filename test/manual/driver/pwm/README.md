# PWM 手动测试 / Manual PWM test

把 PWM 输出接到一个 GPIO 输入，用软件回读检查波形。测试只使用 `PWM`、`GPIO` 和 `Timebase` 公共接口，不创建线程，不注册中断回调，也不由 CI 自动运行。

Connect a PWM output to a GPIO input and check the waveform by polling. The test uses only the public `PWM`, `GPIO` and `Timebase` APIs. It creates no threads or interrupt callbacks and is not run automatically by CI.

## 接线和准备 / Wiring and setup

使用电压兼容、共地的两个专用引脚，不要连接电机或其他负载。输出应为高电平有效的普通 PWM，输入须支持内部下拉，且输入中断已关闭。调用方初始化 PWM 通道和微秒时间基，并独占整个 PWM 定时器，避免改频率影响其他通道。

Use two dedicated pins with compatible voltages and a common ground, without a motor or other load. Use active-high PWM and an input supporting internal pull-down, with input interrupts disabled. Initialize the PWM channel and microsecond timebase, and reserve the whole PWM timer so frequency changes cannot affect other channels.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从普通任务调用：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal task:

```cpp
#include "pwm/test_pwm.hpp"

void RunPWMTest(LibXR::PWM& pwm, LibXR::GPIO& input)
{
  LibXR::Test::TestPWM(pwm, input);
}
```

完整参数为 `TestPWM(pwm, input, frequency_hz, tolerance_us, iterations)`：

The full call is `TestPWM(pwm, input, frequency_hz, tolerance_us, iterations)`:

| 参数 / Argument | 默认 / Default | 含义 / Meaning |
| --- | --- | --- |
| `frequency_hz` | 100 | 检查此频率及两倍频率 / Test this frequency and twice this value |
| `tolerance_us` | 100 | 周期和高电平时间的绝对误差上限 / Absolute tolerance for period and high time |
| `iterations` | 100 | 完整轮数；填 1 可快速检查 / Complete rounds; use 1 for a quick check |

每轮在两个频率下分别检查 25%、50%、75% 占空比，测量相邻的上升、下降、上升沿；再检查 0% 恒低、100% 恒高，以及禁用后停止跳变、重新启用后恢复 50% 波形。每次更新后固定等待两个周期，不会重试失败的测量。

Each round checks 25%, 50% and 75% duty at both frequencies by measuring consecutive rising, falling and rising edges. It then checks constant low at 0%, constant high at 100%, no transitions after disabling, and restoration of a 50% waveform after re-enabling. Every update gets a fixed two-cycle settling interval; failed measurements are not retried.

## 测量限制与结束状态 / Measurement limits and final state

这是低频轮询测试。测试期间不要让其他工作长时间抢占调用任务；采样间隔必须远小于最短高／低电平时间。容差应在测试前按时间基精度和调度延迟确定，最多为较短周期的 5%。如果平台无法满足采样要求，应改用外部仪器，不能靠不断放宽容差取得通过结果。

This is a low-frequency polling test. Avoid long preemptions of the calling task; the sampling interval must be much shorter than the shortest high/low interval. Choose the tolerance before testing from timebase precision and scheduling latency, up to 5% of the shorter period. Use external equipment if the platform cannot sample fast enough, rather than repeatedly increasing the tolerance until it passes.

时间以 LibXR 时间基为参照；若它与 PWM 共用时钟，测试不能校准该时钟的绝对频率。测试不测最高频率、细小毛刺、互补输出或死区。

Timing is relative to the LibXR timebase. If it shares the PWM clock, the test cannot calibrate that clock's absolute frequency. It does not measure maximum frequency, narrow glitches, complementary outputs or dead time.

失败触发始终生效的 `TEST_ASSERT` 并停止。正常返回前设置 0% 并禁用输出，输入保留下拉配置；不恢复原频率和占空比。禁用输出可能保持某个电平或变为高阻，测试只要求停止周期跳变。

A failure triggers the always-active `TEST_ASSERT` and stops. On success, duty is set to 0% and output is disabled; the input retains its pull-down. The original frequency and duty are not restored. A disabled output may hold a level or become high-impedance; the test only requires periodic transitions to stop.
