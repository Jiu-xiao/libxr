if(NOT TIMER_PRIORITY)
set(TIMER_PRIORITY "Thread::Priority::HIGH")
endif()

add_compile_definitions(LIBXR_TIMER_PRIORITY=${TIMER_PRIORITY})

if(LIBXR_DEFAULT_SCALAR)
  add_compile_definitions(LIBXR_DEFAULT_SCALAR=${LIBXR_DEFAULT_SCALAR})
endif()

# Detect system
if(CMAKE_CROSSCOMPILING)
  message("Cross compiling.")
else()
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
    message(FATAL_ERROR "Unkonw system.")
  endif()
endif()

if(NOT LIBXR_SYSTEM)
  message(FATAL_ERROR "No system select.")
endif()

if(NOT LIBXR_DRIVER)
  message(WARNING "No driver select.")
endif()

add_compile_definitions(LIBXR_SYSTEM ${LIBXR_SYSTEM})

if("${LIBXR_SYSTEM}" STREQUAL "None")
  add_compile_definitions(LIBXR_NOT_SUPPORT_MUTI_THREAD=1)
  message("Not support multi thread.")
endif()

message("-- Platfrom: ${LIBXR_SYSTEM}")
