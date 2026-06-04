#pragma once

/**
 * @brief `database` 对外包含入口 / Public include entry for `database`
 *
 * @note 外部代码仍应优先包含这个头；`interface`、`raw_sequential`、`raw`
 *       这些子头主要用于表达模块内部边界。
 *       External code should still include this header first; the `interface`,
 *       `raw_sequential`, and `raw` subheaders are used mainly to express the
 *       internal module boundaries.
 */

#include "interface.hpp"
#include "raw_sequential.hpp"
#include "raw.hpp"
