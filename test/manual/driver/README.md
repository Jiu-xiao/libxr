# 驱动手动测试 / Manual driver tests

这里用于需要真实外设的测试，例如 UART 收发、ADC 采样和 Flash 读写。目前提供 [GPIO 读写和中断测试](gpio/README.md)、[PWM 回读测试](pwm/README.md)、[ADC 读数测试](adc/README.md)、[DAC 回读测试](dac/README.md)、[Flash 擦写测试](flash/README.md)、[I2C 寄存器读写测试](i2c/README.md)、[SPI 回环测试](spi/README.md)、[CAN / CAN FD 内部回环测试](can/README.md)及 [UART 回环测试](uart/README.md)，不由 CI 执行。

This directory is for tests that need real peripherals, such as UART transfers, ADC sampling and Flash access. [GPIO loopback and interrupt tests](gpio/README.md), [PWM readback tests](pwm/README.md), [ADC sampling tests](adc/README.md), [DAC readback tests](dac/README.md), [Flash erase/program tests](flash/README.md), [I2C register read/write tests](i2c/README.md), [SPI loopback tests](spi/README.md), [CAN / CAN FD internal loopback tests](can/README.md) and [UART loopback tests](uart/README.md) are available. CI does not run them.

调用方完成设备初始化和接线，准备所需资源，再把设备对象传给测试函数。测试通过 LibXR 公共驱动接口操作设备，检查失败时立即停止。

The caller initializes and connects the device, prepares the resources and passes the device object to the test function. Tests use LibXR public driver APIs and stop immediately on a failed check.

每项测试需要说明：

Each test should describe:

- 使用的外设和接线，以及需要的回环连接或外部信号。
  The peripheral, wiring, and any loopback connection or external signal.
- 入口函数、参数、缓冲区和其他资源。
  The entry function, arguments, buffers and other resources.
- 预期结果、会修改的配置或数据，以及测试后的恢复方法。
  Expected results, configuration or data that will change, and how to restore them afterwards.

Flash 测试只使用调用方指定的专用测试区域。异步操作使用的缓冲区和回调数据，必须保留到操作结束。

Flash tests use only the dedicated region supplied by the caller. Buffers and callback data used by asynchronous operations must remain valid until those operations finish.

Linux 上能用伪终端或文件检查的行为放在 automatic；这里补充真实硬件才能检查的部分。

Behavior that can be checked with Linux pseudo-terminals or files belongs in automatic. This directory adds the checks that require real hardware.
