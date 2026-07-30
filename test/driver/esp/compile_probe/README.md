# ESP UART Compile Probe

This IDF project compiles and links the explicit `ESP32UartFifo` API on every ESP-IDF 5.5.2
stable MCU target and the explicit `ESP32UartDma` API where AHB-GDMA plus UHCI are
available. The probe receives its expected policy as an external CMake value and uses
a compile-time assertion against the production
`LibXR::Detail::ESP_UART_USES_IRQ_SERIALIZATION` constant:

- DirectPolicy: `esp32s2`, `esp32c2`, `esp32c3`, `esp32c5`, `esp32c6`,
  `esp32c61`, `esp32h2`.
- IrqSerializedPolicy: `esp32`, `esp32s3`, `esp32p4`, with SMP enabled.

The default matrix contains 11 independent target/variant builds. The
`esp32s3-unicore` negative-control variant uses an independent sdkconfig defaults file
and must select DirectPolicy on the same SoC. The runner reads each S3 variant's
generated sdkconfig back and requires `CONFIG_FREERTOS_UNICORE` to be disabled for
`esp32s3` and enabled for `esp32s3-unicore` before compiling.

After exporting an ESP-IDF 5.5.2 environment, run the full matrix or selected targets:

```sh
./build_matrix.sh
./build_matrix.sh esp32 esp32s3
./build_matrix.sh esp32s3-unicore
```

Each case has separate absolute build and sdkconfig paths under
`build/targets/<case>/`. Set `MATRIX_BUILD_ROOT` to place all generated output outside
the source tree and `MATRIX_JOBS` to change the default parallelism of two jobs.

The probe's `app_main()` constructs `ESP32UartFifo` on an unpinned UART with no routed
pins. The project is build-only and must not be flashed; the construction exists to pull
the complete FIFO dependency graph into the application ELF.

Every run rewrites `build/summary.tsv`. It records result, target, variant, externally
selected expectation, exit code, duration, and the byte size and SHA-256 of the FIFO
object, application ELF, and `libxr.a`, together with the FIFO source SHA-256, the count
of defined FIFO constructor symbols in the ELF, and a per-case log path. The FIFO object
is resolved by matching the absolute
`driver/esp/esp_uart_fifo.cpp` path in `compile_commands.json`; the match must be
unique. The application ELF and LibXR archive must also be unique, and the target's
configured `CMAKE_NM` must find at least one defined `ESP32UartFifo` constructor in the
ELF. Missing or ambiguous evidence fails that row.

Configuration, compilation, or evidence failure in one case does not stop later
cases. The runner returns nonzero after writing every selected row if any row failed.
Logs are stored under `build/logs/`.

This is compile/link evidence. Physical FIFO behavior remains in the multi-target UART
loopback runner under `test/hardware/esp32s3_uart_loopback/`.
