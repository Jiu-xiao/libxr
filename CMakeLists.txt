cmake_minimum_required(VERSION 3.11)
project(libxr LANGUAGES C CXX ASM)

# Detect platfrom
if(CMAKE_CROSSCOMPILING)
  Set(PLATFROM "CrossCompiling")
elseif()
  if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    Set(PLATFROM "Linux")
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    Set(PLATFROM "Windows")
  elseif()
    message(FATAL_ERROR, "Unkonw platform.")
  endif()
endif()

message("Platfrom: ${PLATFROM}")

# Share/Static
if(LIBXR_SHARED_BUILD)
  add_library(${PROJECT_NAME} SHARED)
else()
  add_library(${PROJECT_NAME} STATIC)
endif()

# Source File
file(
  GLOB ${PROJECT_NAME}_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

target_sources(${PROJECT_NAME} PUBLIC ${${PROJECT_NAME}_SOURCES})

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
