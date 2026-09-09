# ADC 手动测试 / Manual ADC test

给 ADC 提供一个已知且稳定的输入电压，连续检查读数，并返回最小值、最大值和平均值。每个读数都要在允许误差内，不能用平均值掩盖异常采样。

Supply a known, stable input voltage, check consecutive readings and return their minimum, maximum and mean. Every reading must be within tolerance; a mean cannot hide a bad sample.

## 准备输入 / Prepare the input

调用方初始化 ADC、参考电压和系统时间基，保证输入没有超过 ADC 允许范围。切换电压后，先等待电路和 ADC 采样结果稳定，再调用测试；如果驱动使用 DMA 或滤波缓冲区，也要留出更新这些数据的时间。

Initialize the ADC, reference voltage and system timebase, and keep the input within the ADC's allowed range. After changing the voltage, allow the circuit and ADC readings to settle before calling the test, including any DMA or filter buffer update time.

信号源由调用方选择，可以是外部电源、分压电路、GPIO 或 DAC。GPIO 高低电平适合检查端点和读数更新，但不能检查中间电压或线性度。测试本身不控制信号源，也不要求板上有 DAC。

The caller chooses the source: an external supply, divider, GPIO or DAC. GPIO levels can check endpoints and reading updates, but not intermediate voltages or linearity. The test does not control the source or require an on-board DAC.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从普通任务调用。例如输入已经稳定在已知的 1.2 V 时：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal task. For an input already settled at a known 1.2 V, for example:

```cpp
#include "adc/test_adc.hpp"

LibXR::Test::ADCTestResult CheckInput(LibXR::ADC& adc)
{
  return LibXR::Test::TestADC(adc, 1.2f, 0.05f);
}
```

完整参数为 `TestADC(adc, expected_voltage, tolerance_voltage, interval_ms, samples)`。电压和容差以 V 为单位；示例允许每个读数偏离目标最多 0.05 V，这个值不是所有板子的默认精度要求，应根据实际信号源和 ADC 设定。

The full call is `TestADC(adc, expected_voltage, tolerance_voltage, interval_ms, samples)`. Voltages and tolerance are in V. The example allows each reading to differ by at most 0.05 V; this is not a universal accuracy requirement. Choose it for the actual source and ADC.

默认读取 1000 次，两次读取之间等待 1 ms。最后一个参数控制次数，填 1 可快速检查；间隔填 0 表示连续读取。读取次数不等于独立转换次数，因为部分驱动返回的是缓存或滤波结果。

By default, the test reads 1000 times with a 1 ms delay between reads. The last argument sets the count; use 1 for a quick check or an interval of 0 for back-to-back reads. Read count is not necessarily conversion count: some drivers return cached or filtered data.

`ADCTestResult` 的 `minimum`、`maximum` 和 `average` 都是电压值，可由应用打印或在调试器中查看。遇到 NaN、无穷大或超出容差的读数时，始终生效的 `TEST_ASSERT` 会立即停止测试，不会丢弃异常值后重试。

`ADCTestResult.minimum`, `maximum` and `average` are voltages for application logging or debugger inspection. NaN, infinity or an out-of-tolerance value triggers the always-active `TEST_ASSERT` immediately; bad readings are not discarded and retried.

## 检查读数更新 / Check reading updates

恒定输入不能发现所有“卡在旧值”的问题。调用方可以反复切换两个或多个电压，等待固定的稳定时间后，以新的预期电压调用本测试。具体接线、切换方法和重复轮数留在调用方工程中。

A constant input cannot reveal every stale-reading failure. The caller can repeatedly switch between two or more voltages, wait a fixed settling interval and call the test with the new expected voltage. Keep the wiring, switching method and repetition count in the caller's project.

测试不会改变 ADC 配置或停止采样。返回后设备继续保持原有工作方式。用同一参考电压下的 GPIO 或 DAC 回环，只能验证对应的功能与更新行为，不能代替独立信号源的绝对精度测量。

The test neither reconfigures the ADC nor stops sampling; the device keeps its original operating mode. GPIO or DAC loopback sharing the reference checks the corresponding function and updates, but does not replace absolute accuracy measurement with an independent source.
