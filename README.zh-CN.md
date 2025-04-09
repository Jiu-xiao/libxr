
# LibXR

<div align="center">

<img src="https://github.com/Jiu-xiao/LibXR_CppCodeGenerator/raw/main/imgs/XRobot.jpeg" width="300">

致力于成为最佳的嵌入式开发框架

![License](https://img.shields.io/badge/license-Apache--2.0-blue)
[![Documentation](https://img.shields.io/badge/docs-online-brightgreen)](https://jiu-xiao.github.io/libxr/)
[![GitHub Issues](https://img.shields.io/github/issues/Jiu-xiao/libxr)](https://github.com/Jiu-xiao/libxr/issues)
[![C/C++ CI](https://github.com/Jiu-xiao/libxr/actions/workflows/check.yml/badge.svg)](https://github.com/Jiu-xiao/libxr/actions/workflows/check.yml)
[![Generate and Deploy Doxygen Docs](https://github.com/Jiu-xiao/libxr/actions/workflows/doxygen.yml/badge.svg)](https://github.com/Jiu-xiao/libxr/actions/workflows/doxygen.yml)
[![CI/CD - Python Package](https://github.com/Jiu-xiao/LibXR_CppCodeGenerator/actions/workflows/python-publish.yml/badge.svg)](https://github.com/Jiu-xiao/LibXR_CppCodeGenerator/actions/workflows/python-publish.yml)

</div>

[English](https://github.com/Jiu-xiao/libxr/blob/main/README.md) | [中文](https://github.com/Jiu-xiao/libxr/blob/main/README.zh-CN.md)

## 本库适用于哪些人

* 不需要桌面或网页应用。
* 需要实时性与高性能。
* 希望快速且可靠地完成整个项目。
* 不想关心 API 差异。

## 系统层支持

1. 应用程序除非重启或进入低功耗模式，否则永不退出。
2. 所有内存仅在初始化时分配，不会动态释放。
3. 最小非阻塞延迟为 1 微秒，最小阻塞延迟为 1 毫秒。
4. 所有未使用函数不会被链接进最终程序。

| `System`      | Thread | Timer | Semaphore | Mutex | Signal | ConditionVar | Queue | ASync |
| ------------- | ------ | ----- | --------- | ----- | ------ | ------------ | ----- | ----- |
| None          | ✅      | ✅     | ✅         | ✅     | ✅      | ✅            | ✅     | ✅     |
| FreeRTOS      | ✅      | ✅     | ✅         | ✅     | ✅      | ✅            | ✅     | ✅     |
| RT-Thread     | ❌      | ❌     | ❌         | ❌     | ❌      | ❌            | ❌     | ❌     |
| ThreadX       | ❌      | ❌     | ❌         | ❌     | ❌      | ❌            | ❌     | ❌     |
| PX5           | ❌      | ❌     | ❌         | ❌     | ❌      | ❌            | ❌     | ❌     |
| Linux         | ✅      | ✅     | ✅         | ✅     | ✅      | ✅            | ✅     | ✅     |
| Webots(Linux) | ✅      | ✅     | ✅         | ✅     | ✅      | ✅            | ✅     | ✅     |

## 数据结构支持

1. 除了链表和红黑树外，其它数据结构内存大小在构造前确定。
2. 除队列外，所有数据结构均不包含阻塞 API；若需要阻塞功能，请使用信号量。

| `Structure` | List | Stack | RBTree | LockFreeQueue | ChunkQueue |
| ----------- | ---- | ----- | ------ | ------------- | ---------- |
|             | ✅    | ✅     | ✅      | ✅             | ✅          |

## 中间件支持

A collection of commonly used software.

| `Middleware` | Event | Message | Ramfs | Terminal | Database | Log |
| ------------ | ----- | ------- | ----- | -------- | -------- | --- |
|              | ✅     | ✅       | ✅     | ✅        | ✅        | ❌   |

## 外设抽象层支持

Only have virtual class, you can find the drivers in `Platfrom` folder. For example class `STM32Uart` based on the virtual class `Uart`.

| `Peripheral` | POWER | GPIO | WDG | PWM | ADC | DAC | UART | SPI | I2C | CAN/CANFD | USB-CDC | FLASH |
| ------------ | ----- | ---- | --- | --- | --- | --- | ---- | --- | --- | --------- | ------- | ----- |
| STM32        | ✅     | ✅    | ❌   | ✅   | ✅   | ❌   | ✅    | ✅   | ✅   | ✅         | ✅       | ✅     |
| ESP32        | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ❌     |
| Linux        | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ✅     |
| GD32         | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ❌     |
| HC32         | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ❌     |
| WCH32        | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ❌     |
| HPM          | ❌     | ❌    | ❌   | ❌   | ❌   | ❌   | ❌    | ❌   | ❌   | ❌         | ❌       | ❌     |

| `Network` | TCP/UDP | WIFI | Bluetooth | SmartConfig |
| --------- | ------- | ---- | --------- | ----------- |
| Linux     | ❌       | ❌    | ❌         | ❌           |
| ESP32     | ❌       | ❌    | ❌         | ❌           |
| STM32     | ❌       | ❌    | ❌         | ❌           |

## 实用工具

用于调试、机器人学和通信的一些有用工具。

| Kinematics | Forward-Kinematics | Inverse-Kinematics | Coordinate | Pose and Position |
| ---------- | ------------------ | ------------------ | ---------- | ----------------- |
|            | ✅                  | ✅                  | ❌          | ✅                 |

| Dynamics | Inertia | Torque | G-Compensation |
| -------- | ------- | ------ | -------------- |
|          | ✅       | ❌      | ❌              |

| Control | PID | LQR | MPC |
| ------- | --- | --- | --- |
|         | ✅   | ❌   | ❌   |

| Signal | LP Filter | Kalman Filter | FFT | FunctionGen |
| ------ | --------- | ------------- | --- | ----------- |
|        | ❌         | ❌             | ❌   | ❌           |

| Math | CycleValue | CRC8/16/32 | Triangle |
| ---- | ---------- | ---------- | -------- |
|      | ✅          | ✅          | ❌        |

## 使用方法示例

```sh
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配置 LibXR
set(LIBXR_SYSTEM FreeRTOS) # None/Linux/FreeRTOS
set(LIBXR_DRIVER st)       # st/Linux/empty
add_subdirectory(path_to_libxr)

target_link_libraries(${CMAKE_PROJECT_NAME}
    xr
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
```

## 其他工具

### STM32 C++ 代码自动生成器

[libxr-python-package (PyPI)](https://pypi.org/project/libxr/)

## 视频教程

[Bilibili 视频教程](https://www.bilibili.com/video/BV1c8XVYLERR/)
