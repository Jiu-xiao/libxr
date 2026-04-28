cmake_minimum_required(VERSION 3.12)

project(test_hpm_gpio_irq_shared LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_hpm_gpio_irq_shared
  ${CMAKE_CURRENT_LIST_DIR}/test_hpm_gpio_irq_shared.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../driver/hpm/hpm_gpio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk/hpm_test_state.cpp
)

target_include_directories(test_hpm_gpio_irq_shared PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk
  ${CMAKE_CURRENT_LIST_DIR}/../../driver/hpm
  ${CMAKE_CURRENT_LIST_DIR}/../../src
  ${CMAKE_CURRENT_LIST_DIR}/../../src/core
  ${CMAKE_CURRENT_LIST_DIR}/../../src/system
  ${CMAKE_CURRENT_LIST_DIR}/../../src/utils
  ${CMAKE_CURRENT_LIST_DIR}/../../src/structure
  ${CMAKE_CURRENT_LIST_DIR}/../../src/middleware
  ${CMAKE_CURRENT_LIST_DIR}/../../src/driver
  ${CMAKE_CURRENT_LIST_DIR}/../../system/None
)

target_compile_definitions(test_hpm_gpio_irq_shared PRIVATE
  LIBXR_NO_EIGEN
  LIBXR_DEFAULT_SCALAR=double
  LIBXR_PRINTF_BUFFER_SIZE=128
  LIBXR_LOG_LEVEL=4
  LIBXR_LOG_OUTPUT_LEVEL=4
  XR_LOG_MESSAGE_MAX_LEN=64
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_hpm_gpio_irq_shared PRIVATE -Wno-psabi)
endif()
