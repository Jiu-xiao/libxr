function(xr_hardware_test_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ${target} PRIVATE -Werror -Wno-error=missing-field-initializers
    )
  endif()
endfunction()

add_library(
  xr_hardware_test_support STATIC
  driver/adc_sampling_test.cpp
  driver/can_loopback_test.cpp
  driver/dac_adc_tracking_test.cpp
  driver/i2c_register_device_test.cpp
  driver/pwm_gpio_loopback_test.cpp
  driver/spi_loopback_test.cpp
  driver/uart_concurrency_stress_test.cpp
  driver/uart_loopback_test.cpp
)

target_compile_features(xr_hardware_test_support PUBLIC cxx_std_20)
target_include_directories(xr_hardware_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(xr_hardware_test_support PUBLIC xr)

xr_hardware_test_apply_warnings(xr_hardware_test_support)

if(_xr_system STREQUAL "linux")
  function(xr_add_hardware_test_selftest target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE xr_hardware_test_support)
    xr_hardware_test_apply_warnings(${target})
  endfunction()

  xr_add_hardware_test_selftest(
    xr_hardware_test_support_selftest driver/uart_loopback_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_spi_hardware_test_support_selftest driver/spi_loopback_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_adc_hardware_test_support_selftest driver/adc_sampling_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_dac_adc_hardware_test_support_selftest driver/dac_adc_tracking_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_pwm_gpio_hardware_test_support_selftest driver/pwm_gpio_loopback_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_can_hardware_test_support_selftest driver/can_loopback_test_selftest.cpp
  )
  xr_add_hardware_test_selftest(
    xr_i2c_hardware_test_support_selftest driver/i2c_register_device_test_selftest.cpp
  )
endif()
