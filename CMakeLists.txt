cmake_minimum_required(VERSION 3.11)
project(xr LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build)

include(CMake/config.cmake)

# Detect platform
if(CMAKE_CROSSCOMPILING)
  message("Cross compiling.")
else()
  if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    Set(PLATFORM "Linux")
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    Set(PLATFORM "Windows")
  elseif()
    message(FATAL_ERROR "Unkonw platform.")
  endif()
endif()

if(NOT PLATFORM)
  message(FATAL_ERROR "No platform.")
endif()

add_compile_definitions(LIBXR_PLATFORM ${PLATFORM})

if("${PLATFORM}" STREQUAL "None")
  add_compile_definitions(LIBXR_NOT_SUPPORT_MUTI_THREAD)
endif()

message("-- Platfrom: ${PLATFORM}")

# Share/Static
if(LIBXR_SHARED_BUILD)
  add_library(${PROJECT_NAME} SHARED)
else()
  add_library(${PROJECT_NAME} STATIC)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(LIBXR_DEBUG_BUILD)
endif()

# Source File
file(
  GLOB ${PROJECT_NAME}_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_SYSTEM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_UTILS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp")

file(
  GLOB ${PROJECT_NAME}_STRUCTURE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/structure/*.cpp")

file(
  GLOB ${PROJECT_NAME}_MIDDLEWARE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/middleware/*.cpp")

file(
  GLOB ${PROJECT_NAME}_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM}/*.cpp")

target_sources(${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_SOURCES}
  PRIVATE ${${PROJECT_NAME}_CORE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES}
  PRIVATE ${${PROJECT_NAME}_UTILS_SOURCES}
  PRIVATE ${${PROJECT_NAME}_PLATFORM_SOURCES}
  PRIVATE ${${PROJECT_NAME}_STRUCTURE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_MIDDLEWARE_SOURCES}
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/system
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/structure
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/middleware
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM})

if(LIBXR_TEST_BUILD)
  include(CMake/test.cmake)
endif()
