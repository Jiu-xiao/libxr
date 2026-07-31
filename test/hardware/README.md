# LibXR Hardware Tests

This directory contains reusable tests that require real peripherals. They are not
part of the normal host test runner and must be enabled explicitly:

```sh
cmake -DLIBXR_HARDWARE_TEST_BUILD=ON ...
```

`driver/uart_loopback_test.hpp` is backend-independent. A board runner constructs one
UART, provides scratch buffers, invokes the test functions, and reports the structured
result. The common functions cover idle reconfiguration, deterministic single-write
loopback, and two queued writes. They preserve stream continuity between
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

`driver/spi_loopback_test.hpp` provides the corresponding backend-independent physical
SPI loopback test. A board runner connects MOSI to MISO, constructs one SPI object, and
passes a deterministic list of transfer sizes. Every transfer uses a BLOCK operation, so
the function compares RX only after polling completion or the DMA terminal callback. The
SPI must be dedicated to the test. Both scratch buffers and both driver-owned transfer
buffers must hold the largest requested transfer, and their active ranges must be pairwise
disjoint. The helper rejects software double buffering because it deliberately verifies
one active transfer at a time. After a timeout, caller-owned scratch storage must remain
alive and untouched until the backend retires the transfer. Its host self-test covers
successful multi-size traffic, absent RX writes, mismatch, transfer/configuration failure,
overlap rejection, overflow rejection, and scratch/driver capacity rejection.

`driver/adc_sampling_test.hpp` repeatedly reads caller-selected ADC channel objects and
checks finite values, inclusive voltage ranges, and maximum observed span. The caller
owns the channel objects and supplies one statistics slot per channel. Board runners can
therefore use internal references for a wiring-free smoke test or external calibrated
sources for tighter accuracy checks without embedding board-specific limits in LibXR.

`driver/dac_adc_tracking_test.hpp` drives one or more DAC/ADC feedback pairs through a
point-major voltage table. It checks every DAC write, rejects non-finite feedback, and
reports the first tolerance violation plus the maximum observed error. Using distinct
setpoints per pair also detects crossed loopback wiring.

`driver/pwm_gpio_loopback_test.hpp` drives one or more PWM/GPIO feedback pairs through a
point-major duty table. Deterministically jittered sampling avoids phase locking, while
the observed high ratio and rising-edge rate check duty cycle and frequency. The caller
must choose a maximum sample interval shorter than the smallest expected high or low
pulse; static 0% and 100% points check level only.

`driver/can_loopback_test.hpp` validates Classic CAN and CAN FD loopback through the
LibXR CAN interface. The caller supplies phase-specific frame cases and storage for
callback state. The helper verifies identifier, frame format, flags, payload, callback
context, and exactly-once completion while draining in-flight callbacks before a phase
change or teardown.

`driver/i2c_register_device_test.hpp` validates one explicitly addressed register
device without scanning the bus. It checks a fixed identity register, saves a writable
register range, performs multi-byte write/readback operations, restores the original
bytes, and verifies restoration. Repeated multi-byte reads exercise the configured
polling or DMA threshold while all operation and scratch storage remains caller-owned.

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

The ESP UART runner remains under the legacy `esp32s3_uart_loopback/` path but supports
four explicit target traits:

| Target | Loopback wiring | Policy expectation | Console profile |
|---|---|---|---|
| ESP32 | GPIO17 (UART2 TX) -> GPIO16 (UART2 RX) | IRQ serialization | UART0 bridge |
| ESP32-S3 | GPIO1 (UART2 TX) -> GPIO2 (UART2 RX) | IRQ serialization | UART0 bridge |
| ESP32-C3 | GPIO21 (UART0 TX) -> GPIO20 (UART0 RX) | direct | USB Serial/JTAG |
| ESP32-H2 | GPIO24 (UART0 TX) -> GPIO23 (UART0 RX) | direct | USB Serial/JTAG |

GPIO16/17 are unavailable on ESP32 modules that use them for PSRAM. Confirm the actual
module exposes both pins before running that case. C3/H2 dedicate UART0 to the test, so
their primary console must be USB Serial/JTAG. Merely enabling USB as a secondary console
does not release UART0.

Select both the backend and expected policy explicitly. The FIFO four-board configuration
commands below use independent build/sdkconfig paths:

```sh
cd test/hardware/esp32s3_uart_loopback

idf.py -B build/esp32-fifo \
  -D LIBXR_ESP_UART_TEST_BACKEND=FIFO \
  -D LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=1 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.smp_uart.defaults' \
  set-target esp32
idf.py -B build/esp32-fifo build

idf.py -B build/esp32s3-fifo \
  -D LIBXR_ESP_UART_TEST_BACKEND=FIFO \
  -D LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=1 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.smp_uart.defaults' \
  set-target esp32s3
idf.py -B build/esp32s3-fifo build

idf.py -B build/esp32c3-fifo \
  -D LIBXR_ESP_UART_TEST_BACKEND=FIFO \
  -D LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=0 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.usb_console.defaults' \
  set-target esp32c3
idf.py -B build/esp32c3-fifo build

idf.py -B build/esp32h2-fifo \
  -D LIBXR_ESP_UART_TEST_BACKEND=FIFO \
  -D LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=0 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.usb_console.defaults' \
  set-target esp32h2
idf.py -B build/esp32h2-fifo build
```

ESP32 supports only the FIFO backend. S3, C3, and H2 may be rebuilt with an explicitly
selected `DMA` backend for regression coverage. There is no automatic backend fallback.

On ESP32/S3 the UART is constructed on core 0, where its interrupts are registered. The
runner executes the ordinary suite on cores 0 and 1, then runs writer/configurator stress
in both cross-core directions. C3/H2 execute the ordinary suite on core 0 and one
same-core, two-task writer/configurator stress phase; unavailable cross-core fields are
reported as `SKIP`.

The FIFO partial-CONFIG case temporarily installs the ESP-IDF driver on UART1 as an
independent RX-only observer. The GPIO matrix routes the tested TX pin to UART1 RX while
the physical TX-to-RX loopback remains connected. This observer proves that the entire
partially loaded record leaves the transmitter exactly once under the old framing; it is
removed before recovery and stress traffic. UART1 must therefore be unused by the BSP,
but no additional jumper is required.

The structured start and final records include board, backend, UART, pins, expected and
actual policy, and topology. A run passes only when the requested identity matches, all
target-required fields pass, and one final record ends in `all=PASS`:

```text
[UART_LOOPBACK_START] board=... backend=... uart=... tx=... rx=... policy_expected=... policy_actual=... topology=...
[UART_LOOPBACK_FINAL] board=... backend=... uart=... tx=... rx=... policy_expected=... policy_actual=... topology=... all=PASS
```
