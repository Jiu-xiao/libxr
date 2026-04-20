# Detect system
# Preserve user-provided cache variables and normalize them only for internal use.
set(_xr_system "")
if(DEFINED LIBXR_SYSTEM AND NOT "${LIBXR_SYSTEM}" STREQUAL "")
  string(TOLOWER "${LIBXR_SYSTEM}" _xr_system)
  if(NOT "${LIBXR_SYSTEM}" STREQUAL "${_xr_system}")
    message(STATUS "Normalized LIBXR_SYSTEM: ${LIBXR_SYSTEM} -> ${_xr_system}")
  endif()
endif()

set(_xr_driver "")
if(DEFINED LIBXR_DRIVER AND NOT "${LIBXR_DRIVER}" STREQUAL "")
  string(TOLOWER "${LIBXR_DRIVER}" _xr_driver)
  if(NOT "${LIBXR_DRIVER}" STREQUAL "${_xr_driver}")
    message(STATUS "Normalized LIBXR_DRIVER: ${LIBXR_DRIVER} -> ${_xr_driver}")
  endif()
endif()

if(CMAKE_CROSSCOMPILING)
  message(STATUS "Cross compiling.")
elseif(NOT _xr_system)
  if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    if(WEBOTS_HOME)
      set(_xr_system "webots")
      set(_xr_driver "webots")
    else()
      set(_xr_system "linux")
      set(_xr_driver "linux")
    endif()
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    set(_xr_system "windows")
    set(_xr_driver "windows")
  else()
    message(FATAL_ERROR "Unknown system.")
  endif()
endif()

if(NOT _xr_system)
  message(FATAL_ERROR "No system selected.")
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_SYSTEM_${_xr_system}=True)

if(_xr_system STREQUAL "linux" OR _xr_system STREQUAL "webots")
  target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_SYSTEM_POSIX_HOST=True)
endif()

if(_xr_driver)
  include(${CMAKE_CURRENT_LIST_DIR}/../driver/${_xr_driver}/CMakeLists.txt)
else()
  message(WARNING "No driver selected.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../system/${_xr_system}/CMakeLists.txt)

message(STATUS "Platform: ${_xr_system}")

if(NOT DEFINED LIBXR_DEFAULT_SCALAR)
  set(LIBXR_DEFAULT_SCALAR double)
endif()

if(NOT DEFINED LIBXR_PRINTF_BUFFER_SIZE)
  set(LIBXR_PRINTF_BUFFER_SIZE 128)
endif()

if(NOT DEFINED LIBXR_LOG_LEVEL)
  set(LIBXR_LOG_LEVEL 4)
endif()

if(NOT DEFINED LIBXR_LOG_OUTPUT_LEVEL)
  set(LIBXR_LOG_OUTPUT_LEVEL 4)
endif()

if(NOT DEFINED XR_LOG_MESSAGE_MAX_LEN)
  set(XR_LOG_MESSAGE_MAX_LEN 64)
endif()

target_compile_definitions(
  ${PROJECT_NAME}
  PUBLIC LIBXR_DEFAULT_SCALAR=${LIBXR_DEFAULT_SCALAR}
  PUBLIC LIBXR_PRINTF_BUFFER_SIZE=${LIBXR_PRINTF_BUFFER_SIZE}
  PUBLIC LIBXR_LOG_LEVEL=${LIBXR_LOG_LEVEL}
  PUBLIC LIBXR_LOG_OUTPUT_LEVEL=${LIBXR_LOG_OUTPUT_LEVEL}
  PUBLIC XR_LOG_MESSAGE_MAX_LEN=${XR_LOG_MESSAGE_MAX_LEN}
)
