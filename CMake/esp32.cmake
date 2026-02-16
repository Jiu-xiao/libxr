# LibXR target selection for ESP-IDF
set(LIBXR_SYSTEM FreeRTOS)
set(LIBXR_DRIVER esp)
set(LIBXR_STATIC_BUILD ON)

if(NOT TARGET xr)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_BINARY_DIR}/libxr_build)
endif()

target_compile_definitions(xr PUBLIC ESP_PLATFORM=1)

# Prefer official ESP-IDF component targets so headers/libs are managed by IDF.
set(_libxr_idf_components
    freertos
    driver
    hal
    esp_hw_support
    esp_timer
    esp_event
    esp_netif
    esp_wifi
    esp_adc
    nvs_flash)

set(_libxr_idf_targets "")
foreach(_component IN LISTS _libxr_idf_components)
  if(TARGET idf::${_component})
    list(APPEND _libxr_idf_targets idf::${_component})
  endif()
endforeach()

# Newer IDF versions split driver components; link them when available.
foreach(_component IN ITEMS esp_driver_gpio esp_driver_ledc)
  if(TARGET idf::${_component})
    list(APPEND _libxr_idf_targets idf::${_component})
  endif()
endforeach()
list(REMOVE_DUPLICATES _libxr_idf_targets)

if(NOT TARGET idf::freertos)
  message(FATAL_ERROR "libxr esp32.cmake must be used under ESP-IDF (missing idf::freertos)")
endif()

target_link_libraries(xr PUBLIC ${_libxr_idf_targets})

# IDF exports FreeRTOS include roots, but LibXR uses `#include "FreeRTOS.h"`
# (without `freertos/` prefix). Keep a narrow compatibility fallback so this
# integration works across IDF versions/layout changes.
set(_libxr_idf_path "")
if(DEFINED IDF_PATH)
  set(_libxr_idf_path "${IDF_PATH}")
elseif(DEFINED ENV{IDF_PATH})
  set(_libxr_idf_path "$ENV{IDF_PATH}")
endif()

if(_libxr_idf_path)
  set(_libxr_freertos_compat_dirs
      "${_libxr_idf_path}/components/freertos/include/freertos"
      "${_libxr_idf_path}/components/freertos/FreeRTOS-Kernel/include/freertos")

  foreach(_dir IN LISTS _libxr_freertos_compat_dirs)
    if(EXISTS "${_dir}/FreeRTOS.h")
      target_include_directories(xr PUBLIC "${_dir}")
    endif()
  endforeach()
endif()

target_link_libraries(${COMPONENT_LIB} PUBLIC xr)
