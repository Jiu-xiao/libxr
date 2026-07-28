add_library(
  xr_hardware_test_support STATIC driver/uart_concurrency_stress_test.cpp
                                  driver/uart_loopback_test.cpp
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
endif()
