# DAC 手动测试 / Manual DAC test

将 DAC 输出接到 ADC 输入，反复输出“低→中→高→中→低”五个电压点。每次写入后等待固定时间，再复用 `TestADC` 检查连续 16 次读数，读取间隔为 1 ms。

Wire the DAC output to an ADC input and repeatedly apply five levels: low, middle, high, middle and low. After a fixed settling delay, reuse `TestADC` to check 16 readings, 1 ms apart.

## 接线和准备 / Wiring and setup

调用方初始化 DAC、ADC 和系统时间基，独占两个通道。信号源和采样设备需要共地，输入端不能同时连接其他输出。最低、最高电压由调用方给定，中间电压取两者平均；测试没有默认量程。

Initialize the DAC, ADC and system timebase, and reserve both channels. The output and sampling device need a common ground; no other output may drive the input. The caller supplies the lower and upper voltages, and the middle is their average. The test has no default voltage range.

所选范围必须同时满足 DAC 的有效输出范围和 ADC 的允许输入范围。部分 DAC 在接近电源轨时不能保持精度，应按实际器件与负载选择范围。

The range must fit both the DAC's usable output range and the ADC's allowed input range. Some DACs lose accuracy near the supply rails; choose the range for the actual device and load.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从普通任务调用。由应用传入适用的电压范围与容差：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal task. Supply the appropriate voltage range and tolerance from the application:

```cpp
#include "dac/test_dac.hpp"

void RunDACTest(LibXR::DAC& dac, LibXR::ADC& adc,
                float min_voltage, float max_voltage, float tolerance_voltage)
{
  LibXR::Test::TestDAC(dac, adc, min_voltage, max_voltage, tolerance_voltage);
}
```

完整参数为 `TestDAC(dac, adc, min_voltage, max_voltage, tolerance_voltage, settle_ms, iterations)`。电压与容差以 V 为单位。容差必须小于相邻电压差的一半，否则旧电压可能被误判为新电压。

The full call is `TestDAC(dac, adc, min_voltage, max_voltage, tolerance_voltage, settle_ms, iterations)`. Voltages and tolerance are in V. Tolerance must be less than half the spacing between adjacent levels so an old level cannot be accepted as the new one.

默认每次写入后等待 5 ms，重复 100 轮，共检查 8000 次读数。最后一个参数填 1 可做快速检查。稳定时间应覆盖 DAC 建立、外部电路响应和 ADC 缓冲区或滤波结果更新；按实际系统在测试前确定，不会在失败后自动延长。

Defaults are a 5 ms delay after each write and 100 rounds, for 8000 readings. Set the last argument to 1 for a quick check. Choose the settling time before the test to cover DAC settling, the external circuit and ADC buffer or filter updates. It is not automatically extended after a failure.

## 结果与结束状态 / Results and final state

每次 `Write()` 必须成功，每个 ADC 读数都必须有效且在容差内，失败立即触发 `TEST_ASSERT`。没有额外线程、回调或动态分配，CI 不自动运行这项接线测试。

Every `Write()` must succeed, and every ADC reading must be finite and within tolerance. Failure immediately triggers `TEST_ASSERT`. The test creates no extra threads, callbacks or dynamic allocations, and CI does not run this wired test automatically.

正常返回后 DAC 保持最低测试电压，ADC 继续原有工作方式。测试不恢复原输出电压；通用 DAC 接口没有关闭输出的方法。

On success, the DAC holds the lower test voltage and the ADC keeps its original operating mode. The original output voltage is not restored; the generic DAC interface has no output-disable method.

回读结果同时受 DAC、接线、ADC 和参考电压影响。它适合发现输出不更新、电压明显错误或间歇异常，但失败时仍需区分信号源和采样端。若 DAC 与 ADC 共用参考电压，这项回环不能代替独立仪器的绝对精度或完整线性度测量。

Readback depends on the DAC, wiring, ADC and voltage reference. It can reveal stale output, incorrect levels or intermittent errors, but a failure still needs to be traced to the output or sampling side. When the DAC and ADC share a reference, loopback does not replace absolute accuracy or full linearity measurement with an independent instrument.
