add_library(
  xr_hardware_test_support STATIC
  driver/adc_sampling_test.cpp
  driver/can_loopback_test.cpp
  driver/dac_adc_tracking_test.cpp
  driver/i2c_register_device_test.cpp
  driver/pwm_gpio_loopback_test.cpp
  driver/uart_concurrency_stress_test.cpp
  driver/uart_loopback_test.cpp
  driver/spi_loopback_test.cpp
)

target_compile_features(xr_hardware_test_support PUBLIC cxx_std_20)
target_include_directories(xr_hardware_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(xr_hardware_test_support PUBLIC xr)

if(MSVC)
  target_compile_options(xr_hardware_test_support PRIVATE /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    xr_hardware_test_support PRIVATE -Werror -Wno-error=missing-field-initializers
  )
endif()

if(_xr_system STREQUAL "linux")
  add_executable(xr_hardware_test_support_selftest driver/uart_loopback_test_selftest.cpp)
  target_link_libraries(
    xr_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_hardware_test_support_selftest PRIVATE -Werror
                                                -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_spi_hardware_test_support_selftest driver/spi_loopback_test_selftest.cpp
  )
  target_link_libraries(
    xr_spi_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_spi_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_spi_hardware_test_support_selftest PRIVATE -Werror
                                                    -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_adc_hardware_test_support_selftest driver/adc_sampling_test_selftest.cpp
  )
  target_link_libraries(
    xr_adc_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_adc_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_adc_hardware_test_support_selftest PRIVATE -Werror
                                                    -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_dac_adc_hardware_test_support_selftest driver/dac_adc_tracking_test_selftest.cpp
  )
  target_link_libraries(
    xr_dac_adc_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_dac_adc_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_dac_adc_hardware_test_support_selftest
      PRIVATE -Werror -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_pwm_gpio_hardware_test_support_selftest driver/pwm_gpio_loopback_test_selftest.cpp
  )
  target_link_libraries(
    xr_pwm_gpio_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_pwm_gpio_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_pwm_gpio_hardware_test_support_selftest
      PRIVATE -Werror -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_can_hardware_test_support_selftest driver/can_loopback_test_selftest.cpp
  )
  target_link_libraries(
    xr_can_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_can_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_can_hardware_test_support_selftest PRIVATE -Werror
                                                    -Wno-error=missing-field-initializers
    )
  endif()

  add_executable(
    xr_i2c_hardware_test_support_selftest driver/i2c_register_device_test_selftest.cpp
  )
  target_link_libraries(
    xr_i2c_hardware_test_support_selftest PRIVATE xr_hardware_test_support
  )
  if(MSVC)
    target_compile_options(xr_i2c_hardware_test_support_selftest PRIVATE /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      xr_i2c_hardware_test_support_selftest PRIVATE -Werror
                                                    -Wno-error=missing-field-initializers
    )
  endif()
endif()
