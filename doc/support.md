# Platform Peripheral Support List

- ✅：已支持（Supported）
- ⚙️：仅编译（Compile Only）
- 🔄：开发中（In Progress/Working）
- ❌：未支持（Not Supported）
- 🚫：硬件不支持/不可用（Unavailable）

## Linux Support

| `Peripheral` | Ubuntu/Debian |
| ------------ | ------------- |
| POWER        | ✅             |
| GPIO         | ❌             |
| FLASH        | ✅             |
| UART         | ✅             |
| SPI          | ❌             |
| I2C          | ❌             |
| CAN          | ❌             |
| CANFD        | ❌             |
| ADC          | ❌             |
| DAC          | ❌             |
| PWM          | ❌             |
| USB-DEVICE   | ❌             |
| WDG          | ❌             |

| `Network`   | Ubuntu/Debian |
| ----------- | ------------- |
| WIFI Client | ✅             |
| SmartConfig | ❌             |
| Bluetooth   | ❌             |

## STM32 Support

| `Peripheral` | STM32F0/F1/F4/G0/G4/H7 | STM32L0/L1/L4 |
| ------------ | ---------------------- | ------------- |
| POWER        | ✅                      | ⚙️             |
| GPIO         | ✅                      | ⚙️             |
| FLASH        | ✅                      | ⚙️             |
| UART         | ✅                      | ⚙️             |
| SPI          | ✅                      | ⚙️             |
| I2C          | ✅                      | ⚙️             |
| CAN          | ✅                      | ⚙️             |
| CANFD        | ✅                      | ⚙️             |
| ADC          | ✅                      | ⚙️             |
| DAC          | ✅                      | ⚙️             |
| PWM          | ✅                      | ⚙️             |
| USB-DEVICE   | ✅                      | ⚙️             |
| WDG          | ✅                      | ⚙️             |

| `Network`   | STM32 |
| ----------- | ----- |
| WIFI Client | ❌     |
| SmartConfig | ❌     |
| Bluetooth   | ❌     |

> Verified USB device-controller paths:
> - `USB_DRV_FS`: `STM32G431`
> - `USB_OTG_FS`: `STM32F401`, `STM32F407`
> - `USB_OTG_HS`: `STM32F407`, `STM32H750`

## ESP32 Support

| `Peripheral` | ESP32 | ESP32-S3 | ESP32-C3 | ESP32-C6 |
| ------------ | ----- | -------- | -------- | -------- |
| POWER        | ✅     | ✅        | ✅        | ✅        |
| GPIO         | ✅     | ✅        | ✅        | ✅        |
| FLASH        | ✅     | ✅        | ✅        | ✅        |
| UART         | ✅     | ✅        | ✅        | ✅        |
| SPI          | ✅     | ✅        | ✅        | ✅        |
| I2C          | ✅     | ✅        | ✅        | ✅        |
| CAN          | ❌     | ❌        | ❌        | ❌        |
| CANFD        | 🚫     | 🚫        | 🚫        | 🚫        |
| ADC          | ✅     | ✅        | ✅        | ✅        |
| DAC          | ✅     | 🚫        | 🚫        | 🚫        |
| PWM          | ✅     | ✅        | ✅        | ✅        |
| USB-DEVICE   | ❌     | ✅        | ❌        | ❌        |
| CDC-JTAG     | 🚫     | 🚫        | ✅        | ✅        |
| WDG          | ✅     | ✅        | ✅        | ✅        |

| `Network`   | ESP32 | ESP32-S3 | ESP32-C3 | ESP32-C6 |
| ----------- | ----- | -------- | -------- | -------- |
| WIFI Client | ✅     | ✅        | ✅        | ✅        |
| SmartConfig | ❌     | ❌        | ❌        | ❌        |
| Bluetooth   | ❌     | ❌        | ❌        | ❌        |

> Note:
> `USB-DEVICE` refers to the native USB device controller path.
> `CDC-JTAG` refers to the dedicated USB Serial/JTAG peripheral and is distinct from the native USB device stack.
> The currently verified ESP native USB device backend is the `ESP32-S3` path.

## CH32 Support

| `Peripheral` | CH32V307/CH32V203 |
| ------------ | ----------------- |
| POWER        | ✅                 |
| GPIO         | ✅                 |
| FLASH        | ✅                 |
| UART         | ✅                 |
| SPI          | ✅                 |
| I2C          | ✅                 |
| CAN          | ✅                 |
| CANFD        | 🚫                 |
| ADC          | ❌                 |
| DAC          | ❌                 |
| PWM          | ✅                 |
| USB-DEVICE   | ✅                 |
| WDG          | ❌                 |

| `Network`   | CH32V307 |
| ----------- | -------- |
| WIFI Client | ❌        |
| SmartConfig | ❌        |
| Bluetooth   | ❌        |

> Verified USB device-controller paths:
> - `USB_DEVICE_FS`: `CH32V203`
> - `USB_OTG_FS`: `CH32V203`, `CH32V208`, `CH32V307`
> - `USB_OTG_HS`: `CH32V305`, `CH32V307`
