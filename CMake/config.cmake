# Detect system
if(CMAKE_CROSSCOMPILING)
  message("Cross compiling.")
elseif(NOT LIBXR_SYSTEM)
  if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    if(WEBOTS_HOME)
      Set(LIBXR_SYSTEM "Webots")
      Set(LIBXR_DRIVER "Webots")
    else()
      Set(LIBXR_SYSTEM "Linux")
      Set(LIBXR_DRIVER "Linux")
    endif()
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    Set(LIBXR_SYSTEM "Windows")
    Set(LIBXR_DRIVER "Windows")
  elseif()
    message(FATAL_ERROR "Unknown system.")
  endif()
endif()

if(NOT LIBXR_SYSTEM)
  message(FATAL_ERROR "No system select.")
endif()

target_compile_definitions(xr PUBLIC LIBXR_SYSTEM_${LIBXR_SYSTEM}=True)

if(NOT LIBXR_DRIVER)
  message(WARNING "No driver select.")
else()
  include(${CMAKE_CURRENT_LIST_DIR}/../driver/${LIBXR_DRIVER}/CMakeLists.txt)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../system/${LIBXR_SYSTEM}/CMakeLists.txt)

message("-- Platform: ${LIBXR_SYSTEM}")

if(DEFINED LIBXR_DEFAULT_SCALAR)
  target_compile_definitions(xr PUBLIC LIBXR_DEFAULT_SCALAR=${LIBXR_DEFAULT_SCALAR})
else()
  target_compile_definitions(xr PUBLIC LIBXR_DEFAULT_SCALAR=double)
endif()

if(DEFINED LIBXR_PRINTF_BUFFER_SIZE)
  target_compile_definitions(xr PUBLIC LIBXR_PRINTF_BUFFER_SIZE=${LIBXR_PRINTF_BUFFER_SIZE})
else()
  target_compile_definitions(xr PUBLIC LIBXR_PRINTF_BUFFER_SIZE=128)
endif()

if(NOT DEFINED LIBXR_LOG_LEVEL)
  target_compile_definitions(xr PUBLIC LIBXR_LOG_LEVEL=4)
else()
  target_compile_definitions(xr PUBLIC LIBXR_LOG_LEVEL=${LIBXR_LOG_LEVEL})
endif()

if(NOT DEFINED LIBXR_LOG_OUTPUT_LEVEL)
  target_compile_definitions(xr PUBLIC LIBXR_LOG_OUTPUT_LEVEL=4)
else()
  target_compile_definitions(xr PUBLIC LIBXR_LOG_OUTPUT_LEVEL=${LIBXR_LOG_OUTPUT_LEVEL})
endif()

if(NOT DEFINED XR_LOG_MESSAGE_MAX_LEN)
  target_compile_definitions(xr PUBLIC XR_LOG_MESSAGE_MAX_LEN=64)
else()
  target_compile_definitions(xr PUBLIC XR_LOG_MESSAGE_MAX_LEN=${XR_LOG_MESSAGE_MAX_LEN})
endif()
