# XRUSB

<div align="center">

<img src="https://github.com/Jiu-xiao/LibXR_CppCodeGenerator/raw/main/imgs/XRobot.jpeg" width="300">

A truly tiny and beautiful, ultra-fast and modern USB stack for embedded systems.

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
[![Documentation](https://img.shields.io/badge/docs-online-brightgreen)](https://jiu-xiao.github.io/libxr/)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FJiu-xiao%2Flibxr.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2FJiu-xiao%2Flibxr?ref=badge_shield)

</div>

## Introduction

XRUSB is a standalone, modern C++ USB protocol stack. It is provided both as a [LibXR](https://github.com/Jiu-xiao/libxr) subtree and as an independent repository. XRUSB focuses on portability, high performance, and easy integration.

## Key Features

- **Modern C++ Implementation**: Written in C++17, using classes and template-based modular encapsulation for easy extension.
- **Lock-Free Data Structures**: All data transfers and event handling are lock-free and thread-safe for maximum efficiency.
- **Double Buffering Mechanism**: Fully utilizes hardware/software double buffers and DMA. Alternating read/write greatly increases data throughput.
- **Dynamic Endpoint Allocation**: Endpoints are allocated on demand during enumeration; multiple classes can automatically manage and reuse endpoints to avoid resource waste.
- **One-Time Memory Allocation**: All memory is determined at compile time and allocated once at construction. No redundant space is reserved for strings/descriptors.
- **Interrupt-Driven**: Operates fully by hardware interrupts, with no reliance on polling or background threads.
- **Interrupt-Safe**: Driver functions can be called directly from ISR (Interrupt Service Routine).
- **Optimized Memory Copy**: Achieves higher throughput for bulk transfers and descriptor processing.

## Device Drivers

This repository only contains platform-independent stack code. For platform-specific device drivers, please refer to the corresponding drivers in libxr, such as:

- `driver/st/stm32_usb_ep.cpp`
- `driver/ch/ch32_usb_endpoint_otghs.cpp`
- `driver/esp/esp_usb_dev.cpp`

Note:

- `USB-DEVICE` below refers to the native USB device controller path used by XRUSB.
- Mainline libxr currently provides `CDC-JTAG` on ESP32-C3/ESP32-C6 via `driver/esp/esp_cdc_jtag.*`; this is a separate dedicated USB Serial/JTAG UART backend, not the generic XRUSB device-controller path.

## Support Status

### Device Stack

| Protocol | Status | Notes |
| ---------- | ----------------------------- | ---------------------------------------------------------------------------------------------- |
| CDC-ACM | Supported | Implemented as LibXR’s UART class |
| HID | Supported | Only standard keyboard/mouse and remote controller; other types require you to derive your own |
| UAC | Supported | Currently implements a UAC 1.0 microphone only |
| GSUSB | Supported (CAN/FDCAN) | Driverless SocketCAN on Linux |
| DFU Runtime | Supported | Handles runtime `DETACH` and delayed jump to bootloader |
| DFU Bootloader | Supported | Supports `DNLOAD` / `UPLOAD` / `GETSTATUS` / `ABORT` / `CLRSTATUS` / manifest flow |
| DAPLINK V2 | Supports (SWD interface only) | Can be used with Keil/OpenOCD |

### BOS / Platform Capabilities

| Capability | Status | Notes |
| ----------------- | ------------------ | --------------------------------------------------------------------- |
| WebUSB | Supported | Used by DFU runtime / DFU bootloader via BOS platform capability |
| WinUSB MS OS 2.0 | Supported | Used by DAPLink V2 for Windows driverless interface discovery |

### Host Stack

TODO

### Platform Support

| Platform | Phy | Status | Test Device |
| -------- | ------------- | ------------------ | -------------------------- |
| STM32 | USB_DEVICE_FS | Supported | STM32F103 |
| STM32 | USB_DRV_FS | Supported (Device) | STM32G431 |
| STM32 | USB_OTG_FS | Supported (Device) | STM32F401/STM32F407 |
| STM32 | USB_OTG_HS | Supported (Device) | STM32F407/STM32H750 |
| ESP32-S3 | USB_OTG_FS | Supported (Device) | ESP32-S3 |
| CH32 | USB_DEVICE_FS | Supported | CH32V203 |
| CH32 | USB_OTG_FS | Supported (Device) | CH32V307/CH32V203/CH32V208 |
| CH32 | USB_OTG_HS | Supported (Device) | CH32V305/CH32V307 |

## Documentation

Released together with the [LibXR documentation](https://xrobot-org.github.io/en/docs/xrusb).
