set(_xr_st_component_adc stm32_adc.cpp stm32_adc_gpdma.cpp)
set(_xr_st_component_can stm32_can.cpp)
set(_xr_st_component_canfd stm32_canfd.cpp)
set(_xr_st_component_dac stm32_dac.cpp)
set(_xr_st_component_flash stm32_flash.cpp)
set(_xr_st_component_gpio stm32_gpio.cpp)
set(_xr_st_component_i2c stm32_i2c.cpp)
set(_xr_st_component_power stm32_power.cpp)
set(_xr_st_component_pwm stm32_pwm.cpp)
set(_xr_st_component_spi stm32_spi.cpp)
set(_xr_st_component_timebase stm32_timebase.cpp)
set(_xr_st_component_uart stm32_uart.cpp stm32_uart_gpdma.cpp)
set(_xr_st_component_usb stm32_usb_dev.cpp stm32_usb_ep.cpp)
set(_xr_st_component_watchdog stm32_watchdog.cpp)

if(NOT DEFINED LIBXR_ST_COMPONENTS OR "${LIBXR_ST_COMPONENTS}" STREQUAL "")
  set(_xr_st_requested_components all)
else()
  set(_xr_st_requested_components)
  foreach(_xr_st_component IN LISTS LIBXR_ST_COMPONENTS)
    string(TOLOWER "${_xr_st_component}" _xr_st_component)
    list(APPEND _xr_st_requested_components "${_xr_st_component}")
  endforeach()
  list(REMOVE_DUPLICATES _xr_st_requested_components)
endif()

if("all" IN_LIST _xr_st_requested_components)
  list(LENGTH _xr_st_requested_components _xr_st_component_count)
  if(NOT _xr_st_component_count EQUAL 1)
    message(FATAL_ERROR "LIBXR_ST_COMPONENTS=all cannot be combined with other components")
  endif()

  file(GLOB ${PROJECT_NAME}_DRIVER_CPP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
  file(GLOB ${PROJECT_NAME}_DRIVER_C_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.c")
else()
  set(${PROJECT_NAME}_DRIVER_CPP_SOURCES)
  set(${PROJECT_NAME}_DRIVER_C_SOURCES
      "${CMAKE_CURRENT_LIST_DIR}/stm32_atomic_shim.c")

  foreach(_xr_st_component IN LISTS _xr_st_requested_components)
    if(NOT DEFINED _xr_st_component_${_xr_st_component})
      message(FATAL_ERROR "Unknown LibXR ST component: ${_xr_st_component}")
    endif()

    foreach(_xr_st_source IN LISTS _xr_st_component_${_xr_st_component})
      list(APPEND ${PROJECT_NAME}_DRIVER_CPP_SOURCES
           "${CMAKE_CURRENT_LIST_DIR}/${_xr_st_source}")
    endforeach()
  endforeach()

  list(REMOVE_DUPLICATES ${PROJECT_NAME}_DRIVER_CPP_SOURCES)
  list(SORT ${PROJECT_NAME}_DRIVER_CPP_SOURCES)
endif()

target_sources(
  ${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_DRIVER_CPP_SOURCES}
  PRIVATE ${${PROJECT_NAME}_DRIVER_C_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
