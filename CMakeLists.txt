cmake_minimum_required(VERSION 3.11)
project(xr LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(LIBXR_DEBUG_BUILD)

# Detect platform
if(CMAKE_CROSSCOMPILING)
  Set(PLATFORM "CrossCompiling")
else()
  if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
    Set(PLATFORM "Linux")
  elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    Set(PLATFORM "Windows")
  elseif()
    message(FATAL_ERROR, "Unkonw platform.")
  endif()
endif()

message("-- Platfrom: ${PLATFORM}")

# Share/Static
if(LIBXR_SHARED_BUILD)
  add_library(${PROJECT_NAME} SHARED)
else()
  add_library(${PROJECT_NAME} STATIC)
endif()

# Source File
file(
  GLOB ${PROJECT_NAME}_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_SYSTEM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

file(
  GLOB ${PROJECT_NAME}_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
  
  

target_sources(${PROJECT_NAME} 
  PRIVATE ${${PROJECT_NAME}_SOURCES}
  PRIVATE ${${PROJECT_NAME}_CORE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES}
  PRIVATE ${${PROJECT_NAME}_PLATFORM_SOURCES}
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/system
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM})



project(example)

add_executable(example main.cpp)
  
add_dependencies(example xr)

target_link_libraries(
    example
    PUBLIC xr
)

target_include_directories(
    example
    PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
