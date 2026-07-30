idf_component_register(SRCS "main.cpp" PRIV_REQUIRES driver)

include(${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/esp32.cmake)
target_compile_definitions(
  ${COMPONENT_LIB}
  PRIVATE
    LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=$<BOOL:${LIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION}>
)
