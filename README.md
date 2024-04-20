# libxr

Standardized compatibility layer for operating systems and peripheral devices written in C++. Can run on different platforms.

<p align="center">
  <img src="doc/image/XRobot.jpeg" alt="XRobot logo"  height="100"/>
</p>

## Who is this library for

* People who do not require desktop and web applications.
* Realtime and high performance.
* Finish the entire project quickly and reliably.
* Don't want to care about the differences in APIs.

## System Layer

1. The application will never exit unless it reboot or enter into low-power mode.
1. All memory is only allocated during initializtion and will never be released.
1. The minimum Non-blocking delay is 1us, minimum blocking delay is 1ms.
1. All unused functions will not be linked.

## Data structure

1. Except list and tree, the memory of other data structures are determined before construction.
2. No blocking APIs except Queue. If you want one, use Semaphore.

## Middleware

A collection of commonly used software.

## Periheral Layer

Only have virtual class, you can find the drivers in `Platfrom` folder. For example class `STM32Uart` based on the virtual class `Uart`.

## Utils

Some useful tools for debugging, robotics, and communication.

## Support

`System`|Thread|Timer|Semaphore|Mutex|Signal|ConditionVar|Queue|
|-|-|-|-|-|-|-|-|
|None|❌|❌|❌|❌|❌|❌|❌|
|FreeRTOS|❌|❌|❌|❌|❌|❌|❌|
|RT-Thread|❌|❌|❌|❌|❌|❌|❌|
|ThreadX|❌|❌|❌|❌|❌|❌|❌|
|PX5|❌|❌|❌|❌|❌|❌|❌|
|Linux|✅|✅|✅|✅|✅|✅|✅|


|`Structure`|List|Stack|RBTree|LockFreeQueue|
|-|-|-|-|-|
||✅|✅|✅|✅|

|`Middleware`|Event|Message|Ramfs|Terminal|
|-|-|-|-|-|
||✅|✅|❌|❌|

|`Peripheral`|POWER|GPIO|PWM|UART|SPI|I2C|WDG|CAN/CANFD|TCP/UDP|
|-|-|-|-|-|-|-|-|-|-|
|STM32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|ESP32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|Linux|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|GD32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|HC32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|WCH32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|HPM|❌|❌|❌|❌|❌|❌|❌|❌|❌|


|`Utils`|CRC8/16/32|PID|Filter|CycleValue|FunctionGen|Rotation|Triangle|
|-|-|-|-|-|-|-|-|
||✅|❌|❌|❌|❌|❌|❌|