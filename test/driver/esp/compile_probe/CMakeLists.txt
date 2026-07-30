cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION)
  message(
    FATAL_ERROR
      "Set LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION to 0 (DirectPolicy) or 1 (IrqSerializedPolicy)"
  )
endif()
if(NOT "${LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION}" MATCHES "^[01]$")
  message(FATAL_ERROR "LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION must be exactly 0 or 1")
endif()

if(LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION)
  set(LIBXR_SINGLE_CORE
      OFF
      CACHE BOOL "" FORCE
  )
else()
  set(LIBXR_SINGLE_CORE
      ON
      CACHE BOOL "" FORCE
  )
endif()

set(LIBXR_NO_EIGEN
    ON
    CACHE BOOL "" FORCE
)
set(SDKCONFIG
    "${CMAKE_BINARY_DIR}/sdkconfig"
    CACHE FILEPATH "" FORCE
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD OFF)
project(libxr_esp_uart_compile_probe)
