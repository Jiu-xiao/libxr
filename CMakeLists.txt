cmake_minimum_required(VERSION 3.11)

include(CMake/config.cmake)

project(xr LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/build)

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
  GLOB ${PROJECT_NAME}_SYSTEM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/system/*.cpp")

file(
  GLOB ${PROJECT_NAME}_UTILS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.cpp")

file(
  GLOB ${PROJECT_NAME}_STRUCTURE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/structure/*.cpp")

file(
  GLOB ${PROJECT_NAME}_MIDDLEWARE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/middleware/*.cpp")

file(
  GLOB ${PROJECT_NAME}_SYSTEM_EXT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/system/${LIBXR_SYSTEM}/*.cpp")

file(
  GLOB ${PROJECT_NAME}_DRIVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/driver/${LIBXR_DRIVER}/*.cpp")

  file(
    GLOB ${PROJECT_NAME}_DRIVER_C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/driver/${LIBXR_DRIVER}/*.c")

target_sources(${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_SOURCES}
  PRIVATE ${${PROJECT_NAME}_CORE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_EXT_SOURCES}
  PRIVATE ${${PROJECT_NAME}_UTILS_SOURCES}
  PRIVATE ${${PROJECT_NAME}_STRUCTURE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_MIDDLEWARE_SOURCES}
)

# Header File
target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/system
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/structure
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/middleware
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/driver
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/system/${LIBXR_SYSTEM}
)

# Driver
if(LIBXR_DRIVER)
target_sources(${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_DRIVER_SOURCES}
  PRIVATE ${${PROJECT_NAME}_DRIVER_C_SOURCES}
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/driver/${LIBXR_DRIVER}
)
endif()

# Webots
if(LIBXR_SYSTEM MATCHES "Webots")
  target_link_directories(
    ${PROJECT_NAME}
    PUBLIC ${WEBOTS_HOME}/lib/controller
  )

  target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC ${LIBRARIES}
    PUBLIC ${CMAKE_SHARED_LIBRARY_PREFIX}Controller${CMAKE_SHARED_LIBRARY_SUFFIX}
    PUBLIC ${CMAKE_SHARED_LIBRARY_PREFIX}CppController${CMAKE_SHARED_LIBRARY_SUFFIX}
  )

  target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${WEBOTS_HOME}/include/controller/c
    PUBLIC ${WEBOTS_HOME}/include/controller/cpp
    PUBLIC ${WEBOTS_HOME}/lib/controller
  )
endif()

# Eigen
add_subdirectory(lib/Eigen)

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/Eigen
)

# add_compile_definitions(EIGEN_NO_MALLOC)


# Test
if(LIBXR_TEST_BUILD)
  include(CMake/test.cmake)
endif()
