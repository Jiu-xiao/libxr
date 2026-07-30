idf_build_get_property(LIBXR_ESP_UART_TEST_BACKEND LIBXR_ESP_UART_TEST_BACKEND)
idf_build_get_property(
  LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION
)

set(_libxr_esp_uart_test_sources "main.cpp")

if(LIBXR_ESP_UART_TEST_BACKEND STREQUAL "FIFO")
  list(APPEND _libxr_esp_uart_test_sources "esp_uart_fifo_test.cpp")
elseif(NOT LIBXR_ESP_UART_TEST_BACKEND STREQUAL "DMA")
  message(
    FATAL_ERROR
      "LIBXR_ESP_UART_TEST_BACKEND must be DMA or FIFO, got: ${LIBXR_ESP_UART_TEST_BACKEND}"
  )
endif()

idf_component_register(SRCS ${_libxr_esp_uart_test_sources} PRIV_REQUIRES driver)

if(LIBXR_ESP_UART_TEST_BACKEND STREQUAL "DMA")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LIBXR_ESP_UART_TEST_BACKEND_DMA=1)
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LIBXR_ESP_UART_TEST_BACKEND_FIFO=1)
endif()
target_compile_definitions(
  ${COMPONENT_LIB}
  PRIVATE
    LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=${LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION}
)

set(LIBXR_HARDWARE_TEST_BUILD
    ON
    CACHE BOOL "" FORCE
)
set(LIBXR_NO_EIGEN
    ON
    CACHE BOOL "" FORCE
)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp32.cmake)
target_link_libraries(${COMPONENT_LIB} PRIVATE xr_hardware_test_support)
target_compile_options(
  ${COMPONENT_LIB} PRIVATE -Werror -Wno-error=missing-field-initializers
)
