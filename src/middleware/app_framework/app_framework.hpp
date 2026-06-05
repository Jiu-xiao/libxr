#pragma once

/**
 * @brief `app_framework` 对外包含入口 / Public include entry for `app_framework`
 *
 * @note 外部代码仍应优先包含这个头；硬件别名注册和应用模块管理已拆到同目录的内部片段。
 *       External code should still include this header first; hardware-alias
 *       registration and application-module management are split into internal
 *       fragments in the same directory.
 */

#include "hardware.hpp"
#include "application.hpp"
