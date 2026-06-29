cmake_minimum_required(VERSION 3.12)

project(test_hpm_i2c_stub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIBXR_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(_libxr_test_include_dirs
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk
  ${LIBXR_ROOT}/driver/hpm
  ${LIBXR_ROOT}/src
  ${LIBXR_ROOT}/src/core
  ${LIBXR_ROOT}/src/system
  ${LIBXR_ROOT}/src/utils
  ${LIBXR_ROOT}/src/structure
  ${LIBXR_ROOT}/src/middleware
  ${LIBXR_ROOT}/src/driver
  ${LIBXR_ROOT}/system/none
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

add_executable(test_hpm_i2c_stub
  ${CMAKE_CURRENT_LIST_DIR}/test_hpm_i2c_stub.cpp
  ${CMAKE_CURRENT_LIST_DIR}/stub_sdk/hpm_test_state.cpp
  ${CMAKE_CURRENT_LIST_DIR}/stub_system.cpp
  ${LIBXR_ROOT}/driver/hpm/hpm_i2c.cpp
)

target_include_directories(test_hpm_i2c_stub PRIVATE ${_libxr_test_include_dirs})

target_compile_definitions(test_hpm_i2c_stub PRIVATE
  HPMSOC_HAS_HPMSDK_I2C=1
  LIBXR_HPM_I2C_HOST_STUB_TEST=1
  LIBXR_NO_EIGEN
  LIBXR_DEFAULT_SCALAR=double
  LIBXR_PRINTF_BUFFER_SIZE=128
  LIBXR_LOG_LEVEL=4
  LIBXR_LOG_OUTPUT_LEVEL=4
  XR_LOG_MESSAGE_MAX_LEN=64
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_hpm_i2c_stub PRIVATE -Wno-psabi)
endif()
