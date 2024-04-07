# libxr

A standardized compatibility layer for operating systems and peripherals. Use C++ and for different platform.

<p align="center">
  <img src="doc/image/XRobot.jpeg" alt="XRobot logo"  height="100"/>
</p>

## Who is this library for

* People who have no need for desktop and web application.
* Realtime and high performance.
* Finish the whole project quickly and reliably.
* Don't want to care the differences of APIs.

## System Layer

We set several preconditions:

1. The application will never exit, except reboot or enter into low-power mode.
1. All memory is allocated only during initializtion, and will never be released.
1. The minimum Non-blocking delay is 1us, minimum blocking delay is 1ms.

## Data structure

1. Except list and tree, the memory of other data structures are determined before construction.
2. No blocking APIs. If you want one, use Semaphore.

## Periheral Layer

Only have virtual class, you can find the drivers in `Platfrom` folder. For example class `STM32Uart` based on the virtual class `Uart`.

## Support

|`System`|None|FreeRTOS|RT-Thread|ThreadX|Linux|
|-|-|-|-|-|-|
|Thread|❌|❌|❌|❌|❌|
|Timer|❌|❌|❌|❌|❌|
|Semaphore|❌|❌|❌|❌|❌|
|Mutex|❌|❌|❌|❌|❌|
|Signal|❌|❌|❌|❌|❌|

|`Structure`|DualList|RingQueue|RBTree|HashTable|Stack|
|-|-|-|-|-|-|
|Base|❌|❌|❌|❌|❌|
|Thread Safe|❌|❌|❌|❌|❌|
|From interrupt|❌|❌|❌|❌|❌|

|`Peripheral`|POWER|GPIO|PWM|UART|SPI|I2C|WDG|CAN/CANFD|TCP/UDP|
|-|-|-|-|-|-|-|-|-|-|
|STM32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|ESP32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|Linux|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|GD32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|HC32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|WCH32|❌|❌|❌|❌|❌|❌|❌|❌|❌|
|HPM|❌|❌|❌|❌|❌|❌|❌|❌|❌|
