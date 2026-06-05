/**
 * @brief 显式指定 logger 使用 brace 风格前端 / Explicitly force the logger brace frontend
 */
#define XR_FMT(fmt) LibXR::Detail::LoggerLiteral::Frontend::Format, fmt

/**
 * @brief 显式指定 logger 使用 printf 风格前端 / Explicitly force the logger printf frontend
 */
#define XR_PRINTF(fmt) LibXR::Detail::LoggerLiteral::Frontend::Printf, fmt

#if LIBXR_LOG_LEVEL >= 4
/**
 * @brief 输出调试日志 / Output debug log
 */
#define XR_LOG_DEBUG(fmt, ...)                                                          \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_DEBUG, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_DEBUG(...)
#endif

#if LIBXR_LOG_LEVEL >= 3
/**
 * @brief 输出一般信息日志 / Output info log
 */
#define XR_LOG_INFO(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_INFO, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_INFO(...)
#endif

#if LIBXR_LOG_LEVEL >= 2
/**
 * @brief 输出通过测试日志 / Output pass log
 */
#define XR_LOG_PASS(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_PASS, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_PASS(...)
#endif

#if LIBXR_LOG_LEVEL >= 1
/**
 * @brief 输出警告日志 / Output warning log
 */
#define XR_LOG_WARN(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_WARN, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_WARN(...)
#endif

#if LIBXR_LOG_LEVEL >= 0
/**
 * @brief 输出错误日志 / Output error log
 */
#define XR_LOG_ERROR(fmt, ...)                                                          \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_ERROR, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_ERROR(...)
#endif
