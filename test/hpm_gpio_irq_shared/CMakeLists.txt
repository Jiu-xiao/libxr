cmake_minimum_required(VERSION 3.12)

project(test_hpm_gpio_irq_shared LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBXR_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(_libxr_test_include_dirs
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk
  ${LIBXR_ROOT}/driver/hpm
  ${LIBXR_ROOT}/src
  ${LIBXR_ROOT}/src/core
  ${LIBXR_ROOT}/src/system
  ${LIBXR_ROOT}/src/utils
  ${LIBXR_ROOT}/src/structure
  ${LIBXR_ROOT}/src/middleware
  ${LIBXR_ROOT}/src/driver
  ${LIBXR_ROOT}/system/None
)

file(GLOB_RECURSE _libxr_core_headers CONFIGURE_DEPENDS
  ${LIBXR_ROOT}/src/core/*.hpp
)
foreach(_libxr_header IN LISTS _libxr_core_headers)
  get_filename_component(_libxr_header_dir "${_libxr_header}" DIRECTORY)
  list(APPEND _libxr_test_include_dirs "${_libxr_header_dir}")
endforeach()

file(GLOB_RECURSE _libxr_middleware_headers CONFIGURE_DEPENDS
  ${LIBXR_ROOT}/src/middleware/*.hpp
)
foreach(_libxr_header IN LISTS _libxr_middleware_headers)
  get_filename_component(_libxr_header_dir "${_libxr_header}" DIRECTORY)
  list(APPEND _libxr_test_include_dirs "${_libxr_header_dir}")
endforeach()

list(REMOVE_DUPLICATES _libxr_test_include_dirs)

add_executable(test_hpm_gpio_irq_shared
  ${CMAKE_CURRENT_LIST_DIR}/test_hpm_gpio_irq_shared.cpp
  ${LIBXR_ROOT}/driver/hpm/hpm_gpio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk/hpm_test_state.cpp
)

target_include_directories(test_hpm_gpio_irq_shared PRIVATE ${_libxr_test_include_dirs})

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

if(MSVC)
  target_compile_options(test_hpm_gpio_irq_shared PRIVATE /utf-8)
endif()
