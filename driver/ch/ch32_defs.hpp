#pragma once

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

// 这里只做 WCH 不同系列头文件之间的符号兼容，不补充包含外设头文件。
// This file only normalizes symbols across WCH series headers; peripheral headers
// must be included by LIBXR_CH32_CONFIG_FILE.

// 部分系列把 GPIO 速度枚举改名为 High，驱动侧统一使用传统 50MHz 名称。
// Some series rename the GPIO speed enum to High; keep the traditional 50MHz
// spelling in drivers.
#if !defined(GPIO_Speed_50MHz) && (defined(__CH32V205_H) || defined(__CH32H417_H))
#define GPIO_Speed_50MHz GPIO_Speed_High
#endif

// X035 官方库没有普通/复用开漏输出枚举，提供最接近的可编译别名。
// The X035 library does not expose normal/alternate open-drain modes, so provide
// the closest compile-time aliases.
#if !defined(GPIO_Mode_Out_OD) && defined(__CH32X035_H)
#define GPIO_Mode_Out_OD GPIO_Mode_Out_PP
#endif

#if !defined(GPIO_Mode_AF_OD) && defined(__CH32X035_H)
#define GPIO_Mode_AF_OD GPIO_Mode_AF_PP
#endif

// H417 设备头把 I3C 外设实例定义成宏，会与 libxr USB 描述符枚举项重名。
// The H417 device header defines the I3C peripheral instance as a macro, which
// collides with the libxr USB descriptor enum item.
#if defined(__CH32H417_H) && defined(I3C)
#undef I3C
#endif

// X035 I2C 头裁剪了 SMBus timeout/alert 错误位；置 0 表示共享错误掩码跳过它们。
// The X035 I2C header omits SMBus timeout/alert bits; map them to 0 so shared
// error masks skip those conditions.
#if defined(__CH32X035_H)
#if !defined(I2C_FLAG_TIMEOUT)
#define I2C_FLAG_TIMEOUT 0u
#endif
#if !defined(I2C_FLAG_SMBALERT)
#define I2C_FLAG_SMBALERT 0u
#endif
#if !defined(I2C_IT_TIMEOUT)
#define I2C_IT_TIMEOUT 0u
#endif
#if !defined(I2C_IT_SMBALERT)
#define I2C_IT_SMBALERT 0u
#endif
#endif

// V205/H417 使用 PB2/HB2 命名 GPIO/AFIO 等高速外设总线，驱动统一使用 APB2 名称。
// V205/H417 name the GPIO/AFIO high-speed peripheral bus PB2/HB2; drivers use
// the common APB2 spelling.
#if !defined(RCC_APB2Periph_AFIO) && defined(RCC_PB2Periph_AFIO)
#define RCC_APB2PeriphClockCmd RCC_PB2PeriphClockCmd
#define RCC_APB2PeriphResetCmd RCC_PB2PeriphResetCmd
#elif !defined(RCC_APB2Periph_AFIO) && defined(RCC_HB2Periph_AFIO)
#define RCC_APB2PeriphClockCmd RCC_HB2PeriphClockCmd
#define RCC_APB2PeriphResetCmd RCC_HB2PeriphResetCmd
#endif

#if !defined(RCC_APB2Periph_AFIO) && defined(RCC_PB2Periph_AFIO)
#define RCC_APB2Periph_AFIO RCC_PB2Periph_AFIO
#elif !defined(RCC_APB2Periph_AFIO) && defined(RCC_HB2Periph_AFIO)
#define RCC_APB2Periph_AFIO RCC_HB2Periph_AFIO
#endif

#if !defined(RCC_APB2Periph_GPIOA) && defined(RCC_PB2Periph_GPIOA)
#define RCC_APB2Periph_GPIOA RCC_PB2Periph_GPIOA
#elif !defined(RCC_APB2Periph_GPIOA) && defined(RCC_HB2Periph_GPIOA)
#define RCC_APB2Periph_GPIOA RCC_HB2Periph_GPIOA
#endif

#if !defined(RCC_APB2Periph_GPIOB) && defined(RCC_PB2Periph_GPIOB)
#define RCC_APB2Periph_GPIOB RCC_PB2Periph_GPIOB
#elif !defined(RCC_APB2Periph_GPIOB) && defined(RCC_HB2Periph_GPIOB)
#define RCC_APB2Periph_GPIOB RCC_HB2Periph_GPIOB
#endif

#if !defined(RCC_APB2Periph_GPIOC) && defined(RCC_PB2Periph_GPIOC)
#define RCC_APB2Periph_GPIOC RCC_PB2Periph_GPIOC
#elif !defined(RCC_APB2Periph_GPIOC) && defined(RCC_HB2Periph_GPIOC)
#define RCC_APB2Periph_GPIOC RCC_HB2Periph_GPIOC
#endif

#if !defined(RCC_APB2Periph_GPIOD) && defined(RCC_PB2Periph_GPIOD)
#define RCC_APB2Periph_GPIOD RCC_PB2Periph_GPIOD
#elif !defined(RCC_APB2Periph_GPIOD) && defined(RCC_HB2Periph_GPIOD)
#define RCC_APB2Periph_GPIOD RCC_HB2Periph_GPIOD
#endif

#if !defined(RCC_APB2Periph_GPIOE) && defined(RCC_PB2Periph_GPIOE)
#define RCC_APB2Periph_GPIOE RCC_PB2Periph_GPIOE
#elif !defined(RCC_APB2Periph_GPIOE) && defined(RCC_HB2Periph_GPIOE)
#define RCC_APB2Periph_GPIOE RCC_HB2Periph_GPIOE
#endif

#if !defined(RCC_APB2Periph_GPIOF) && defined(RCC_HB2Periph_GPIOF)
#define RCC_APB2Periph_GPIOF RCC_HB2Periph_GPIOF
#endif

#if !defined(RCC_APB2Periph_ADC1) && defined(RCC_PB2Periph_ADC1)
#define RCC_APB2Periph_ADC1 RCC_PB2Periph_ADC1
#elif !defined(RCC_APB2Periph_ADC1) && defined(RCC_HB2Periph_ADC1)
#define RCC_APB2Periph_ADC1 RCC_HB2Periph_ADC1
#endif

#if !defined(RCC_APB2Periph_TIM1) && defined(RCC_PB2Periph_TIM1)
#define RCC_APB2Periph_TIM1 RCC_PB2Periph_TIM1
#elif !defined(RCC_APB2Periph_TIM1) && defined(RCC_HB2Periph_TIM1)
#define RCC_APB2Periph_TIM1 RCC_HB2Periph_TIM1
#endif

#if !defined(RCC_APB2Periph_TIM8) && defined(RCC_HB2Periph_TIM8)
#define RCC_APB2Periph_TIM8 RCC_HB2Periph_TIM8
#endif

#if !defined(RCC_APB2Periph_TIM9) && defined(RCC_HB2Periph_TIM9)
#define RCC_APB2Periph_TIM9 RCC_HB2Periph_TIM9
#endif

#if !defined(RCC_APB2Periph_TIM10) && defined(RCC_HB2Periph_TIM10)
#define RCC_APB2Periph_TIM10 RCC_HB2Periph_TIM10
#endif

#if !defined(RCC_APB2Periph_TIM11) && defined(RCC_HB2Periph_TIM11)
#define RCC_APB2Periph_TIM11 RCC_HB2Periph_TIM11
#endif

#if !defined(RCC_APB2Periph_TIM12) && defined(RCC_HB2Periph_TIM12)
#define RCC_APB2Periph_TIM12 RCC_HB2Periph_TIM12
#endif

#if !defined(RCC_APB2Periph_SPI1) && defined(RCC_PB2Periph_SPI1)
#define RCC_APB2Periph_SPI1 RCC_PB2Periph_SPI1
#elif !defined(RCC_APB2Periph_SPI1) && defined(RCC_HB2Periph_SPI1)
#define RCC_APB2Periph_SPI1 RCC_HB2Periph_SPI1
#endif

#if !defined(RCC_APB2Periph_USART1) && defined(RCC_PB2Periph_USART1)
#define RCC_APB2Periph_USART1 RCC_PB2Periph_USART1
#elif !defined(RCC_APB2Periph_USART1) && defined(RCC_HB2Periph_USART1)
#define RCC_APB2Periph_USART1 RCC_HB2Periph_USART1
#endif

// V205/H417 使用 PB1/HB1 命名低速外设总线，驱动统一使用 APB1 名称。
// V205/H417 name the low-speed peripheral bus PB1/HB1; drivers use the common
// APB1 spelling.
#if !defined(RCC_APB1Periph_TIM2) && defined(RCC_PB1Periph_TIM2)
#define RCC_APB1PeriphClockCmd RCC_PB1PeriphClockCmd
#define RCC_APB1PeriphResetCmd RCC_PB1PeriphResetCmd
#elif !defined(RCC_APB1Periph_TIM2) && defined(RCC_HB1Periph_TIM2)
#define RCC_APB1PeriphClockCmd RCC_HB1PeriphClockCmd
#define RCC_APB1PeriphResetCmd RCC_HB1PeriphResetCmd
#endif

#if !defined(RCC_APB1Periph_TIM2) && defined(RCC_PB1Periph_TIM2)
#define RCC_APB1Periph_TIM2 RCC_PB1Periph_TIM2
#elif !defined(RCC_APB1Periph_TIM2) && defined(RCC_HB1Periph_TIM2)
#define RCC_APB1Periph_TIM2 RCC_HB1Periph_TIM2
#endif

#if !defined(RCC_APB1Periph_TIM3) && defined(RCC_PB1Periph_TIM3)
#define RCC_APB1Periph_TIM3 RCC_PB1Periph_TIM3
#elif !defined(RCC_APB1Periph_TIM3) && defined(RCC_HB1Periph_TIM3)
#define RCC_APB1Periph_TIM3 RCC_HB1Periph_TIM3
#endif

#if !defined(RCC_APB1Periph_TIM4) && defined(RCC_PB1Periph_TIM4)
#define RCC_APB1Periph_TIM4 RCC_PB1Periph_TIM4
#elif !defined(RCC_APB1Periph_TIM4) && defined(RCC_HB1Periph_TIM4)
#define RCC_APB1Periph_TIM4 RCC_HB1Periph_TIM4
#endif

#if !defined(RCC_APB1Periph_TIM5) && defined(RCC_HB1Periph_TIM5)
#define RCC_APB1Periph_TIM5 RCC_HB1Periph_TIM5
#endif

#if !defined(RCC_APB1Periph_TIM6) && defined(RCC_HB1Periph_TIM6)
#define RCC_APB1Periph_TIM6 RCC_HB1Periph_TIM6
#endif

#if !defined(RCC_APB1Periph_TIM7) && defined(RCC_HB1Periph_TIM7)
#define RCC_APB1Periph_TIM7 RCC_HB1Periph_TIM7
#endif

#if !defined(RCC_APB1Periph_SPI2) && defined(RCC_PB1Periph_SPI2)
#define RCC_APB1Periph_SPI2 RCC_PB1Periph_SPI2
#elif !defined(RCC_APB1Periph_SPI2) && defined(RCC_HB1Periph_SPI2)
#define RCC_APB1Periph_SPI2 RCC_HB1Periph_SPI2
#endif

#if !defined(RCC_APB1Periph_SPI3) && defined(RCC_HB1Periph_SPI3)
#define RCC_APB1Periph_SPI3 RCC_HB1Periph_SPI3
#endif

#if !defined(RCC_APB1Periph_I2C1) && defined(RCC_PB1Periph_I2C1)
#define RCC_APB1Periph_I2C1 RCC_PB1Periph_I2C1
#elif !defined(RCC_APB1Periph_I2C1) && defined(RCC_HB1Periph_I2C1)
#define RCC_APB1Periph_I2C1 RCC_HB1Periph_I2C1
#endif

#if !defined(RCC_APB1Periph_I2C2) && defined(RCC_PB1Periph_I2C2)
#define RCC_APB1Periph_I2C2 RCC_PB1Periph_I2C2
#elif !defined(RCC_APB1Periph_I2C2) && defined(RCC_HB1Periph_I2C2)
#define RCC_APB1Periph_I2C2 RCC_HB1Periph_I2C2
#endif

#if !defined(RCC_APB1Periph_I2C3) && defined(RCC_HB1Periph_I2C3)
#define RCC_APB1Periph_I2C3 RCC_HB1Periph_I2C3
#endif

#if !defined(RCC_APB1Periph_USART2) && defined(RCC_PB1Periph_USART2)
#define RCC_APB1Periph_USART2 RCC_PB1Periph_USART2
#elif !defined(RCC_APB1Periph_USART2) && defined(RCC_HB1Periph_USART2)
#define RCC_APB1Periph_USART2 RCC_HB1Periph_USART2
#endif

#if !defined(RCC_APB1Periph_USART3) && defined(RCC_PB1Periph_USART3)
#define RCC_APB1Periph_USART3 RCC_PB1Periph_USART3
#elif !defined(RCC_APB1Periph_USART3) && defined(RCC_HB1Periph_USART3)
#define RCC_APB1Periph_USART3 RCC_HB1Periph_USART3
#endif

#if !defined(RCC_APB1Periph_USART4) && defined(RCC_HB1Periph_USART4)
#define RCC_APB1Periph_USART4 RCC_HB1Periph_USART4
#endif

#if !defined(RCC_APB1Periph_USART5) && defined(RCC_HB1Periph_USART5)
#define RCC_APB1Periph_USART5 RCC_HB1Periph_USART5
#endif

#if !defined(RCC_APB1Periph_USART6) && defined(RCC_HB1Periph_USART6)
#define RCC_APB1Periph_USART6 RCC_HB1Periph_USART6
#endif

#if !defined(RCC_APB1Periph_USART7) && defined(RCC_HB1Periph_USART7)
#define RCC_APB1Periph_USART7 RCC_HB1Periph_USART7
#endif

#if !defined(RCC_APB1Periph_USART8) && defined(RCC_HB1Periph_USART8)
#define RCC_APB1Periph_USART8 RCC_HB1Periph_USART8
#endif

#if !defined(RCC_APB1Periph_UART4) && defined(RCC_PB1Periph_USART4)
#define RCC_APB1Periph_UART4 RCC_PB1Periph_USART4
#elif !defined(RCC_APB1Periph_UART4) && defined(RCC_HB1Periph_USART4)
#define RCC_APB1Periph_UART4 RCC_HB1Periph_USART4
#endif

#if !defined(RCC_APB1Periph_UART5) && defined(RCC_PB1Periph_USART5)
#define RCC_APB1Periph_UART5 RCC_PB1Periph_USART5
#elif !defined(RCC_APB1Periph_UART5) && defined(RCC_HB1Periph_USART5)
#define RCC_APB1Periph_UART5 RCC_HB1Periph_USART5
#endif

#if !defined(RCC_APB1Periph_UART6) && defined(RCC_PB1Periph_USART6)
#define RCC_APB1Periph_UART6 RCC_PB1Periph_USART6
#elif !defined(RCC_APB1Periph_UART6) && defined(RCC_HB1Periph_USART6)
#define RCC_APB1Periph_UART6 RCC_HB1Periph_USART6
#endif

#if !defined(RCC_APB1Periph_UART7) && defined(RCC_PB1Periph_USART7)
#define RCC_APB1Periph_UART7 RCC_PB1Periph_USART7
#elif !defined(RCC_APB1Periph_UART7) && defined(RCC_HB1Periph_USART7)
#define RCC_APB1Periph_UART7 RCC_HB1Periph_USART7
#endif

#if !defined(RCC_APB1Periph_UART8) && defined(RCC_PB1Periph_USART8)
#define RCC_APB1Periph_UART8 RCC_PB1Periph_USART8
#elif !defined(RCC_APB1Periph_UART8) && defined(RCC_HB1Periph_USART8)
#define RCC_APB1Periph_UART8 RCC_HB1Periph_USART8
#endif

#if !defined(RCC_APB1Periph_CAN1) && defined(RCC_PB1Periph_CAN1)
#define RCC_APB1Periph_CAN1 RCC_PB1Periph_CAN1
#elif !defined(RCC_APB1Periph_CAN1) && defined(RCC_HB1Periph_CAN1)
#define RCC_APB1Periph_CAN1 RCC_HB1Periph_CAN1
#endif

#if !defined(RCC_APB1Periph_CAN2) && defined(RCC_HB1Periph_CAN2)
#define RCC_APB1Periph_CAN2 RCC_HB1Periph_CAN2
#endif

#if !defined(RCC_APB1Periph_CAN3) && defined(RCC_HB1Periph_CAN3)
#define RCC_APB1Periph_CAN3 RCC_HB1Periph_CAN3
#endif

#if !defined(RCC_APB1Periph_BKP) && defined(RCC_PB1Periph_BKP)
#define RCC_APB1Periph_BKP RCC_PB1Periph_BKP
#elif !defined(RCC_APB1Periph_BKP) && defined(RCC_HB1Periph_BKP)
#define RCC_APB1Periph_BKP RCC_HB1Periph_BKP
#endif

#if !defined(RCC_APB1Periph_PWR) && defined(RCC_PB1Periph_PWR)
#define RCC_APB1Periph_PWR RCC_PB1Periph_PWR
#elif !defined(RCC_APB1Periph_PWR) && defined(RCC_HB1Periph_PWR)
#define RCC_APB1Periph_PWR RCC_HB1Periph_PWR
#endif

// V205/H417 使用 HB 命名 AHB 外设总线，驱动统一使用 AHB 名称。
// V205/H417 name the AHB peripheral bus HB; drivers use the common AHB
// spelling.
#if !defined(RCC_AHBPeriph_DMA1) && defined(RCC_HBPeriph_DMA1)
#define RCC_AHBPeriphClockCmd RCC_HBPeriphClockCmd
#define RCC_AHBPeriphResetCmd RCC_HBPeriphResetCmd
#endif

#if !defined(RCC_AHBPeriph_DMA1) && defined(RCC_HBPeriph_DMA1)
#define RCC_AHBPeriph_DMA1 RCC_HBPeriph_DMA1
#endif

#if !defined(RCC_AHBPeriph_DMA2) && defined(RCC_HBPeriph_DMA2)
#define RCC_AHBPeriph_DMA2 RCC_HBPeriph_DMA2
#endif

#if !defined(RCC_AHBPeriph_CRC) && defined(RCC_HBPeriph_CRC)
#define RCC_AHBPeriph_CRC RCC_HBPeriph_CRC
#endif

// USB 总线时钟名在不同系列中有 USBFS/USBHS/OTG_FS 变体，统一成 AHB 名称。
// USB bus-clock symbols vary between USBFS/USBHS/OTG_FS spellings; normalize
// them to AHB names.
#if !defined(RCC_AHBPeriph_USBHS) && defined(RCC_HBPeriph_USBHS)
#define RCC_AHBPeriph_USBHS RCC_HBPeriph_USBHS
#endif

#if !defined(RCC_AHBPeriph_USBSS) && defined(RCC_HBPeriph_USBSS)
#define RCC_AHBPeriph_USBSS RCC_HBPeriph_USBSS
#endif

#if !defined(RCC_AHBPeriph_USBFS) && defined(RCC_HBPeriph_USBFS)
#define RCC_AHBPeriph_USBFS RCC_HBPeriph_USBFS
#endif

#if !defined(RCC_AHBPeriph_USBOTGFS) && defined(RCC_HBPeriph_OTG_FS)
#define RCC_AHBPeriph_USBOTGFS RCC_HBPeriph_OTG_FS
#endif

// 部分 SPL 把 DMA 初始化结构的内存地址字段命名为 Memory0，驱动统一使用
// MemoryBaseAddr 名称。
// Some SPL variants name the DMA init memory address field Memory0; drivers use
// the common MemoryBaseAddr spelling.
#if !defined(DMA_MemoryBaseAddr) && (defined(__CH32V205_H) || defined(__CH32H417_H))
#define DMA_MemoryBaseAddr DMA_Memory0BaseAddr
#endif

// H417 的 RCC_ClocksTypeDef 只暴露 HCLK；CH32 驱动中 APB1/APB2 时钟读法保持统一。
// H417 only exposes HCLK in RCC_ClocksTypeDef; keep APB1/APB2 clock reads
// spelled uniformly in CH32 drivers.
#if defined(__CH32H417_H)
#define PCLK1_Frequency HCLK_Frequency
#define PCLK2_Frequency HCLK_Frequency
#endif

// H417 的 SPI 分频宏使用 Mode0..Mode7 命名，驱动统一使用数值分频命名。
// H417 names SPI prescalers Mode0..Mode7 while the driver uses numeric divider
// names.
#if !defined(SPI_BaudRatePrescaler_2) && defined(SPI_BaudRatePrescaler_Mode0)
#define SPI_BaudRatePrescaler_2 SPI_BaudRatePrescaler_Mode0
#define SPI_BaudRatePrescaler_4 SPI_BaudRatePrescaler_Mode1
#define SPI_BaudRatePrescaler_8 SPI_BaudRatePrescaler_Mode2
#define SPI_BaudRatePrescaler_16 SPI_BaudRatePrescaler_Mode3
#define SPI_BaudRatePrescaler_32 SPI_BaudRatePrescaler_Mode4
#define SPI_BaudRatePrescaler_64 SPI_BaudRatePrescaler_Mode5
#define SPI_BaudRatePrescaler_128 SPI_BaudRatePrescaler_Mode6
#define SPI_BaudRatePrescaler_256 SPI_BaudRatePrescaler_Mode7
#endif

// H417 CAN IRQ/FIFO 命名与 V205/V30x 不同，兼容到驱动使用的旧名称。
// H417 CAN IRQ/FIFO names differ from V205/V30x; normalize them to the legacy
// spellings used by the driver.
#if defined(__CH32H417_H)
#define USB_HP_CAN1_TX_IRQn CAN1_TX_IRQn
#define USB_LP_CAN1_RX0_IRQn CAN1_RX0_IRQn
#if !defined(CAN_FilterFIFO0) && defined(CAN_Filter_FIFO0)
#define CAN_FilterFIFO0 CAN_Filter_FIFO0
#endif
#if !defined(CAN_FilterFIFO1) && defined(CAN_Filter_FIFO1)
#define CAN_FilterFIFO1 CAN_Filter_FIFO1
#endif
#define CAN_SlaveStartBank(bank) CAN_SlaveStartBank((bank), (bank))
#endif

// 部分 SPL 的 DMA 中断位名没有 DMA1_ 前缀，驱动侧使用带控制器前缀的名称。
// Some SPL variants omit the DMA1_ prefix on DMA interrupt bits; drivers use
// the controller-prefixed spelling.
#if !defined(DMA1_IT_TC1) && defined(DMA_IT_TC1)
#define DMA1_IT_TC1 DMA_IT_TC1
#define DMA1_IT_TC2 DMA_IT_TC2
#define DMA1_IT_TC3 DMA_IT_TC3
#define DMA1_IT_TC4 DMA_IT_TC4
#define DMA1_IT_TC5 DMA_IT_TC5
#define DMA1_IT_TC6 DMA_IT_TC6
#define DMA1_IT_TC7 DMA_IT_TC7
#define DMA1_IT_TC8 DMA_IT_TC8
#endif

#if !defined(DMA1_IT_HT1) && defined(DMA_IT_HT1)
#define DMA1_IT_HT1 DMA_IT_HT1
#define DMA1_IT_HT2 DMA_IT_HT2
#define DMA1_IT_HT3 DMA_IT_HT3
#define DMA1_IT_HT4 DMA_IT_HT4
#define DMA1_IT_HT5 DMA_IT_HT5
#define DMA1_IT_HT6 DMA_IT_HT6
#define DMA1_IT_HT7 DMA_IT_HT7
#define DMA1_IT_HT8 DMA_IT_HT8
#endif

// 以下能力宏只描述当前 libxr CH32 驱动实现需要的硬件/API 形态。
// The capability macros below describe the hardware/API shape required by the
// current libxr CH32 driver implementations.
#if defined(GPIOA) || defined(GPIOB) || defined(GPIOC) || defined(GPIOD) || \
    defined(GPIOE) || defined(GPIOF) || defined(GPIOG) || defined(GPIOH) || \
    defined(GPIOI)
#define LIBXR_CH32_HAS_GPIO 1
#endif

#if defined(SysTick) || defined(SysTick0)
#define LIBXR_CH32_HAS_TIMEBASE 1
#endif

#if defined(DMA1_Channel1) || defined(DMA2_Channel1)
#define LIBXR_CH32_HAS_DMA 1
#endif

#if defined(__CH32V205_H) || defined(__CH32H417_H)
#define LIBXR_CH32_DMA_IT_REQUIRES_CONTROLLER 1
#endif

#if defined(CAN1) || defined(CAN2)
#define LIBXR_CH32_HAS_CAN 1
#endif

#if defined(I2C1) || defined(I2C2)
#define LIBXR_CH32_HAS_I2C 1
#endif

#if defined(SPI1) || defined(SPI2) || defined(SPI3)
#define LIBXR_CH32_HAS_SPI 1
#endif

#if defined(USART1) || defined(USART2) || defined(USART3) || defined(USART4) || \
    defined(USART5) || defined(USART6) || defined(USART7) || defined(USART8) || \
    defined(UART1) || defined(UART2) || defined(UART3) || defined(UART4) ||     \
    defined(UART5) || defined(UART6) || defined(UART7) || defined(UART8)
#define LIBXR_CH32_HAS_UART 1
#endif

#if defined(TIM1) || defined(TIM2) || defined(TIM3) || defined(TIM4) || defined(TIM5)
#define LIBXR_CH32_HAS_PWM 1
#endif

#if defined(__CH32V205_PWR_H) || defined(__CH32X035_PWR_H) || \
    defined(__CH32V30x_PWR_H) || defined(__CH32H417_PWR_H)
#define LIBXR_CH32_HAS_POWER 1
#endif

#if defined(__CH32V205_FLASH_H) || defined(__CH32X035_FLASH_H) || \
    defined(__CH32V30x_FLASH_H) || defined(__CH32H417_FLASH_H)
#define LIBXR_CH32_HAS_FLASH 1
#endif

#if defined(RCC_APB1Periph_USB)
#define LIBXR_CH32_HAS_USB_DEV_FS 1
#endif

#if defined(USBFSD)
#define LIBXR_CH32_HAS_USB_OTG_FS 1
#endif

// 当前 OTG-HS 实现依赖新版 USBHSD 端点配置寄存器；V205/H417 的 USBHSD
// 结构同名但寄存器形态不同，因此不能仅以 USBHSD 是否存在作为能力判断。
// The current OTG-HS backend requires the newer USBHSD endpoint-configuration
// register layout. V205/H417 expose a same-named USBHSD block with a different
// register shape, so USBHSD alone is not a sufficient capability check.
#if defined(USBHSD) && defined(__CH32H417_H) && defined(USBHS_UD_DEV_EN) && \
    defined(USBHS_UDIF_TRANSFER) && defined(USBHS_UDIS_EP_ID_MASK)
#define LIBXR_CH32_HAS_USB_OTG_HS 1
#elif defined(USBHSD) && !defined(__CH32V205_H) && !defined(__CH32H417_H) && \
    defined(USBHS_UIF_SETUP_ACT) && defined(USBHS_UC_SPEED_HIGH) &&          \
    defined(USBHS_UEP0_T_TYPE) && defined(USBHS_UEP0_R_TYPE)
#define LIBXR_CH32_HAS_USB_OTG_HS 1
#endif
