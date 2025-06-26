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
| USB-CDC      | ❌             |
| WDG          | ❌             |

| `Network`   | Ubuntu/Debian |
| ----------- | ------------- |
| WIFI Client | ✅             |
| SmartConfig | ❌             |
| Bluetooth   | ❌             |

## STM32 Support

| `Peripheral` | STM32F0/F1/F4/G0/G4/H7 | STM32L0/L1/L4 |
| ------------ | ------------------- | ---------------- |
| POWER        | ✅                   | ⚙️                |
| GPIO         | ✅                   | ⚙️                |
| FLASH        | ✅                   | ⚙️                |
| UART         | ✅                   | ⚙️                |
| SPI          | ✅                   | ⚙️                |
| I2C          | ✅                   | ⚙️                |
| CAN          | ✅                   | ⚙️                |
| CANFD        | ✅                   | ⚙️                |
| ADC          | ✅                   | ⚙️                |
| DAC          | ❌                   | ❌                |
| PWM          | ✅                   | ⚙️                |
| USB-CDC      | ✅                   | ⚙️                |
| WDG          | ✅                   | ⚙️                |

| `Network`   | STM32 |
| ----------- | ----- |
| WIFI Client | ❌     |
| SmartConfig | ❌     |
| Bluetooth   | ❌     |

## ESP32 Support

| `Peripheral` | ESP32-C3 |
| ------------ | -------- |
| POWER        | ✅        |
| GPIO         | ✅        |
| FLASH        | ✅        |
| UART         | ✅        |
| SPI          | ❌        |
| I2C          | ❌        |
| CAN          | ❌        |
| CANFD        | 🚫        |
| ADC          | ✅        |
| DAC          | ❌        |
| PWM          | ✅        |
| USB-CDC      | ✅        |
| WDG          | ❌        |

| `Network`   | ESP32-C3 |
| ----------- | -------- |
| WIFI Client | ✅        |
| SmartConfig | ❌        |
| Bluetooth   | ❌        |

## CH32 Support

| `Peripheral` | CH32V307         |
| ------------ | ---------------- |
| POWER        | ✅                |
| GPIO         | ✅                |
| FLASH        | ❌                |
| UART         | ✅                |
| SPI          | ❌                |
| I2C          | ❌                |
| CAN          | ❌                |
| CANFD        | 🚫                |
| ADC          | ❌                |
| DAC          | ❌                |
| PWM          | ❌                |
| USB-CDC      | ✅(using TinyUSB) |
| WDG          | ❌                |

| `Network`   | CH32V307 |
| ----------- | -------- |
| WIFI Client | ❌        |
| SmartConfig | ❌        |
| Bluetooth   | ❌        |
