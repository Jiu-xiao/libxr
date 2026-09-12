#pragma once

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

// The common WCH V30x header declares CAN2 even for single-CAN V303 devices.
// WCH V30x 公共头文件也为单 CAN 的 V303 声明了 CAN2，不能只判断符号是否存在。
#if defined(CH32V20x_D6) || defined(CH32V20x_D8) || defined(CH32V20x_D8W) || \
    defined(CH32V30x_D8)
#define LIBXR_CH32_HAS_CAN2 0
#elif defined(CAN2)
#define LIBXR_CH32_HAS_CAN2 1
#else
#define LIBXR_CH32_HAS_CAN2 0
#endif
