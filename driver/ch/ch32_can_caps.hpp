#pragma once

#include "libxr_def.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

// These macros belong to the legacy ch32v20x.h/ch32v30x.h SDK families.
// Newer devices such as CH32V205 use a separate header/resource model and are not
// identified by CH32V20x_D6/D8/D8W.
// 这些宏对应旧版 ch32v20x.h/ch32v30x.h 资源族；CH32V205 使用独立资源模型。
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
