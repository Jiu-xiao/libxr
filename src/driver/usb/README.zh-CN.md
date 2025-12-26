# XRUSB

<div align="center">

<img src="https://github.com/Jiu-xiao/LibXR_CppCodeGenerator/raw/main/imgs/XRobot.jpeg" width="300">

真正小巧、美观、超快且现代的嵌入式系统 USB 堆栈。

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
[![Documentation](https://img.shields.io/badge/docs-online-brightgreen)](https://jiu-xiao.github.io/libxr/)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2FJiu-xiao%2Flibxr.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2FJiu-xiao%2Flibxr?ref=badge_shield)

</div>

## 介绍

XRUSB是一个独立的现代 C++ USB 协议栈。以subtree的形式作为[LibXR](https://github.com/Jiu-xiao/libxr)的一部分，同时也提供独立仓库。XRUSB专注于可移植性、高性能和简单集成。

## 主要特性

* 现代 C++ 实现: 采用 C++ 17 标准编写，使用类和模板模块化封装，便于扩展。

* 无锁数据结构：所有传输和事件处理都无锁且线程安全，效率极高。

* 双缓冲机制：充分利用硬件/软件双buffer与DMA，读写交替进行，大幅提升数据传输速度。

* 端点动态分配：仅在枚举阶段按需分配端点，多个类可自动管理和复用，避免资源浪费。

* 一次性内存申请：内存由编译期确定并在构造时一次分配，避免为字符串/描述符预留冗余空间。

* 中断驱动：不依赖于定期轮询或后台线程，所有操作均由硬件中断驱动。

* 中断安全：可以直接从中断服务例程 (ISR) 调用驱动程序函数。

* 优化的内存复制：可在批量传输和描述符处理中实现更高的吞吐量。

## 设备驱动

本仓库只包含平台无关的协议栈代码，具体的设备驱动请到libxr对应平台的驱动下查看。如`driver/st/stm32_usb_ep.cpp`和`driver/ch/ch32_usb_endpoint_otghs.cpp`。

## 支持进度

### 设备协议栈

| 协议    | 支持状态        | 其他                                              |
| ------- | --------------- | ------------------------------------------------- |
| CDC-ACM | 支持            | 封装为LibXR的UART实现                             |
| HID     | 支持            | 仅提供标准键盘/鼠标与遥控器，其他类型需要自行派生 |
| UAC     | 支持            | 目前仅实现了UAC1.0的麦克风                        |
| GSUSB   | 支持(CAN/FDCAN) | 适用于 Linux 平台的免驱 SocketCAN                 |

### 主机协议栈

TODO

### 平台支持

| 平台  | Phy           | 支持情况      | 测试设备                   |
| ----- | ------------- | ------------- | -------------------------- |
| STM32 | USB_DEVICE_FS | 支持          | STM32F103                  |
| STM32 | USB_DRV_FS    | 支持 (Device) | STM32G431                  |
| STM32 | USB_OTG_FS    | 支持 (Device) | STM32F407                  |
| STM32 | USB_OTG_HS    | 支持 (Device) | STM32F407/STM32H750        |
| CH32  | USB_OTG_FS    | 支持 (Device) | CH32V307/CH32V203/CH32V208 |
| CH32  | USB_HS        | 支持          | CH32V307                   |

## 文档

与[LibXR文档](https://xrobot-org.github.io/docs/xrusb)一起发布
