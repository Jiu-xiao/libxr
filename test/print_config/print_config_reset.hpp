/**
 * @file print_config_reset.hpp
 * @brief 清除 CMake 传入的 print 配置宏。 Clear CMake-provided print config macros.
 * @details 测试项目：
 *          1. 允许每个配置矩阵源文件在 include `print.hpp` 前自行定义
 *             `LIBXR_PRINT_*`。
 *          2. 避免 `xr` target 的 PUBLIC compile definitions 污染本地配置。
 *          Test items:
 *          1. Let each matrix source define its own `LIBXR_PRINT_*` values before
 *             including `print.hpp`.
 *          2. Keep PUBLIC compile definitions from the `xr` target from leaking
 *             into the local profile under test.
 */
#pragma once

#undef LIBXR_PRINT_ENABLE_INTEGER
#undef LIBXR_PRINT_ENABLE_TEXT
#undef LIBXR_PRINT_ENABLE_POINTER
#undef LIBXR_PRINT_ENABLE_FLOAT
#undef LIBXR_PRINT_INTEGER_ENABLE_BASE8_16
#undef LIBXR_PRINT_INTEGER_ENABLE_64BIT
#undef LIBXR_PRINT_FLOAT_ENABLE_FIXED
#undef LIBXR_PRINT_FLOAT_ENABLE_DOUBLE
#undef LIBXR_PRINT_FLOAT_ENABLE_SCIENTIFIC
#undef LIBXR_PRINT_FLOAT_ENABLE_GENERAL
#undef LIBXR_PRINT_FLOAT_ENABLE_LONG_DOUBLE
#undef LIBXR_PRINT_FLOAT_MAX_PRECISION
#undef LIBXR_PRINT_FLOAT_MAX_INTEGER_DIGITS
#undef LIBXR_PRINT_ENABLE_WIDTH
#undef LIBXR_PRINT_ENABLE_PRECISION
#undef LIBXR_PRINT_ENABLE_ALTERNATE
#undef LIBXR_PRINT_ENABLE_EXPLICIT_ARGUMENT_INDEXING
