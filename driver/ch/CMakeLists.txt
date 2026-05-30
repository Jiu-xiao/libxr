if(DEFINED LIBXR_CH32_CONFIG_FILE)
  target_compile_definitions(${PROJECT_NAME}
    PUBLIC LIBXR_CH32_CONFIG_FILE=${LIBXR_CH32_CONFIG_FILE}
  )
else()
  message(FATAL_ERROR
    "Please set LIBXR_CH32_CONFIG_FILE. e.g. set(LIBXR_CH32_CONFIG_FILE=ch32v30x_conf.h)"
  )
endif()

set(_libxr_ch32_family "")
if(DEFINED LIBXR_CH32_FAMILY AND NOT "${LIBXR_CH32_FAMILY}" STREQUAL "")
  string(TOUPPER "${LIBXR_CH32_FAMILY}" _libxr_ch32_family)
  if(NOT "${LIBXR_CH32_FAMILY}" STREQUAL "${_libxr_ch32_family}")
    message(STATUS
      "Normalized LIBXR_CH32_FAMILY: ${LIBXR_CH32_FAMILY} -> ${_libxr_ch32_family}")
  endif()
elseif(DEFINED LIBXR_CH32_CONFIG_FILE AND NOT "${LIBXR_CH32_CONFIG_FILE}" STREQUAL "")
  string(TOLOWER "${LIBXR_CH32_CONFIG_FILE}" _libxr_ch32_config_file)
  if(_libxr_ch32_config_file MATCHES "^ch32h41[0-9]+_conf\\.h$")
    set(_libxr_ch32_family "H41X")
    message(STATUS
      "Derived LIBXR_CH32_FAMILY=${_libxr_ch32_family} from "
      "LIBXR_CH32_CONFIG_FILE=${LIBXR_CH32_CONFIG_FILE}")
  endif()
endif()

if(_libxr_ch32_family STREQUAL "H41X")
  target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_CH32_IS_H41X=1)
endif()

file(
  GLOB ${PROJECT_NAME}_DRIVER_CPP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)
list(SORT ${PROJECT_NAME}_DRIVER_CPP_SOURCES)

file(
  GLOB ${PROJECT_NAME}_DRIVER_C_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.c"
)
list(SORT ${PROJECT_NAME}_DRIVER_C_SOURCES)

if(_libxr_ch32_family STREQUAL "H41X")
  list(REMOVE_ITEM ${PROJECT_NAME}_DRIVER_CPP_SOURCES
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usb_devfs.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usb_endpoint_devfs.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usb_otgfs.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32h41x_usb_otgss.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usb_otghs.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32h41x_usb_endpoint_otgss.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usb_endpoint_otghs.cpp
      ${CMAKE_CURRENT_LIST_DIR}/ch32_usbcan_shared.cpp)
endif()

target_sources(${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_DRIVER_CPP_SOURCES}
  PRIVATE ${${PROJECT_NAME}_DRIVER_C_SOURCES}
)

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}
)
