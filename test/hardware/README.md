# LibXR Hardware Tests

This directory contains reusable tests that require real peripherals. They are not
part of the normal host test runner and must be enabled explicitly:

```sh
cmake -DLIBXR_HARDWARE_TEST_BUILD=ON ...
```

`driver/uart_loopback_test.hpp` is backend-independent. A board runner constructs one
UART, provides scratch buffers, invokes the test functions, and reports the structured
result. The common functions cover idle reconfiguration, deterministic single-write
loopback, and two-write active/pending exercise. They preserve stream continuity between
rounds and reject trailing RX bytes instead of clearing evidence between transfers.

`driver/uart_concurrency_stress_test.hpp` is a separate scheduler-neutral helper for
Write-versus-CONFIG pressure. It checks bounded API returns, legal `BUSY`/`FULL` retries,
and continuing Write, CONFIG, and RX progress. Stress traffic uses reproducible random
and boundary lengths with batch depths one and two. RX bytes may be dropped, repeated,
or damaged while CONFIG restarts traffic, so this phase deliberately does not compare
their contents.

After both stress workers stop, `RetireUartStressTraffic()` applies one final
configuration, queues a unique marker behind all accepted writes, discards RX data
through that marker, and requires a quiet window. A strict loopback case then verifies
byte-accurate recovery without confusing retired stress traffic for new data.

The UART must be dedicated to the test while a function is running. For a loopback case,
both UART byte queues and both scratch buffers must hold `frame_size * batch_depth`
bytes. Hardware tests are opt-in; the host self-test validates the harness itself,
including mismatch, duplicate-byte, completion-failure, and capacity-failure cases.

When a physical TX/RX short is unavailable, `host/uart_echo.py` can use a USB-to-UART
adapter as a transparent peer. Connect DUT TX to adapter RX, DUT RX to adapter TX, and
connect ground, then start the helper before resetting or flashing the DUT:

```sh
python test/hardware/host/uart_echo.py --port COM4 --baudrate 115200 --duration-s 60
```

The helper echoes raw binary bytes without parsing frames and reports machine-readable
traffic statistics. The adapter port must not also carry logs. This transport verifies
fixed-line-configuration data tests; changing baud rate while traffic is active requires
a separate control channel to synchronize the host and DUT.

The ESP32-S3 runner is under `esp32s3_uart_loopback/`. Its external loopback wiring is:

```text
GPIO1 (UART2 TX) -> GPIO2 (UART2 RX)
```

It constructs the UART on core 0, where its interrupts are registered, and runs the
same suite from core 0 and core 1. This exercises both same-core and cross-core callers
without constructing a second DMA UART. Its concurrency stress phases place the writer
and configurator on opposite cores, first writer 0/configurator 1 and then writer
1/configurator 0, before the retirement barrier and strict recovery check.
