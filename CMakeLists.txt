cmake_minimum_required(VERSION 3.12)

project(xr LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(LIBXR_SHARED_BUILD)
  add_library(${PROJECT_NAME} SHARED)
elseif(LIBXR_STATIC_BUILD)
  add_library(${PROJECT_NAME} STATIC)
else()
  add_library(${PROJECT_NAME} OBJECT)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

target_compile_options(
  ${PROJECT_NAME}
  PUBLIC -Wno-psabi
  PUBLIC -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.
)

include(cmake/config.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_DEBUG_BUILD)
endif()

file(GLOB ${PROJECT_NAME}_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(SORT ${PROJECT_NAME}_SOURCES)

file(GLOB ${PROJECT_NAME}_CORE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
)
list(SORT ${PROJECT_NAME}_CORE_SOURCES)

file(GLOB ${PROJECT_NAME}_SYSTEM_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/system/*.cpp"
)
list(SORT ${PROJECT_NAME}_SYSTEM_SOURCES)

file(GLOB ${PROJECT_NAME}_DRIVER_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/driver/*.cpp"
)
list(SORT ${PROJECT_NAME}_DRIVER_SOURCES)

file(GLOB ${PROJECT_NAME}_UTILS_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.cpp"
)
list(SORT ${PROJECT_NAME}_UTILS_SOURCES)

file(GLOB ${PROJECT_NAME}_STRUCTURE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/structure/*.cpp"
)
list(SORT ${PROJECT_NAME}_STRUCTURE_SOURCES)

file(GLOB ${PROJECT_NAME}_MIDDLEWARE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/middleware/*.cpp"
)
list(SORT ${PROJECT_NAME}_MIDDLEWARE_SOURCES)

target_sources(
  ${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_SOURCES}
  PRIVATE ${${PROJECT_NAME}_CORE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES}
  PRIVATE ${${PROJECT_NAME}_DRIVER_SOURCES}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_EXT_SOURCES}
  PRIVATE ${${PROJECT_NAME}_UTILS_SOURCES}
  PRIVATE ${${PROJECT_NAME}_STRUCTURE_SOURCES}
  PRIVATE ${${PROJECT_NAME}_MIDDLEWARE_SOURCES}
)

# The current tree still relies on basename and short subtree includes across major layers.
# Keep the exported roots explicit here until the include-style migration lands.
set(_xr_public_include_dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/system
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/structure
    ${CMAKE_CURRENT_SOURCE_DIR}/src/middleware
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver
    ${CMAKE_CURRENT_SOURCE_DIR}/system/${_xr_system}
)

target_include_directories(${PROJECT_NAME} PUBLIC ${_xr_public_include_dirs})

if(_xr_driver)
  target_sources(${PROJECT_NAME} PRIVATE ${${PROJECT_NAME}_DRIVER_C_SOURCES})

  target_include_directories(
    ${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/driver/${_xr_driver}
  )
endif()

# XRUSB
if(_xr_driver)
  include(${CMAKE_CURRENT_SOURCE_DIR}/src/driver/usb/CMakeLists.txt)
endif()

if(_xr_system MATCHES "webots")
  target_link_directories(${PROJECT_NAME} PUBLIC ${WEBOTS_HOME}/lib/controller)

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

if(LIBXR_NO_EIGEN)
  target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_NO_EIGEN)
else()
  add_subdirectory(lib/Eigen)

  target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/Eigen)

  target_compile_definitions(${PROJECT_NAME} PUBLIC EIGEN_NO_IO)
endif()

if(XROBOT_MODULES_DIR)
  file(
    GLOB _xr_module_dirs CONFIGURE_DEPENDS
    RELATIVE ${XROBOT_MODULES_DIR}
    ${XROBOT_MODULES_DIR}/*
  )
  list(SORT _xr_module_dirs)

  foreach(_xr_mod IN LISTS _xr_module_dirs)
    if(IS_DIRECTORY "${XROBOT_MODULES_DIR}/${_xr_mod}")
      if(EXISTS "${XROBOT_MODULES_DIR}/${_xr_mod}/CMakeLists.txt")
        message(STATUS "[XRobot] Including module: ${_xr_mod}")
        include("${XROBOT_MODULES_DIR}/${_xr_mod}/CMakeLists.txt")
      endif()
    endif()
  endforeach()

  get_property(_xr_module_deps GLOBAL PROPERTY XR_MODULE_DEPS)
  if(_xr_module_deps)
    list(REMOVE_DUPLICATES _xr_module_deps)
    list(SORT _xr_module_deps)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${_xr_module_deps})
  endif()
endif()

if(LIBXR_TEST_BUILD)
  include(cmake/test.cmake)
endif()
