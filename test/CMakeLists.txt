if(NOT _xr_system STREQUAL "linux")
  message(FATAL_ERROR "The current test tree only supports LIBXR_SYSTEM=linux.")
endif()

add_compile_options(-g)

add_library(
  test_assert_disabled OBJECT
  framework/assert_disabled_headers.cpp ../src/core/rw/read_port.cpp
  ../src/core/rw/write_port.cpp ../src/core/rw/write_stream.cpp
)
target_include_directories(
  test_assert_disabled PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(
  test_assert_disabled PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_COMPILE_DEFINITIONS>
)
target_compile_options(
  test_assert_disabled PRIVATE -ULIBXR_DEBUG_BUILD -ULIBXR_DEV_ASSERT_BUILD -Wall -Werror
                               -Wunused-parameter
)

enable_testing()
foreach(debug RANGE 0 1)
  foreach(dev RANGE 0 1)
    set(mode_target test_assert_${debug}_${dev})
    add_executable(${mode_target} framework/assert_modes.cpp)
    target_include_directories(${mode_target} PRIVATE framework ../src/core)
    if(debug)
      target_compile_definitions(${mode_target} PRIVATE LIBXR_DEBUG_BUILD)
    endif()
    if(dev)
      target_compile_definitions(${mode_target} PRIVATE LIBXR_DEV_ASSERT_BUILD)
    endif()
    add_test(NAME ${mode_target} COMMAND ${mode_target})
  endforeach()
endforeach()

file(GLOB_RECURSE XR_TEST_BASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp"
)
list(SORT XR_TEST_BASE_SOURCES)

file(GLOB_RECURSE XR_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
)
list(SORT XR_TEST_RUNTIME_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_STDIO_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_stdio/*.cpp"
)
list(SORT XR_TEST_LINUX_STDIO_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_DATABASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_database/*.cpp"
)
list(SORT XR_TEST_LINUX_DATABASE_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_SHM_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_shm/*.cpp"
)
list(SORT XR_TEST_LINUX_SHM_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_BENCH_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_bench/*.cpp"
)
list(SORT XR_TEST_LINUX_BENCH_SOURCES)

set(XR_TEST_HARNESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/framework/main.cpp")

add_executable(
  test
  ${XR_TEST_HARNESS_SOURCES}
  ${XR_TEST_BASE_SOURCES}
  ${XR_TEST_RUNTIME_SOURCES}
  ${XR_TEST_LINUX_STDIO_SOURCES}
  ${XR_TEST_LINUX_DATABASE_SOURCES}
  ${XR_TEST_LINUX_SHM_SOURCES}
  ${XR_TEST_LINUX_BENCH_SOURCES}
)
add_dependencies(test xr)

target_link_libraries(test PUBLIC xr)

target_include_directories(
  test
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/framework"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

file(GLOB_RECURSE XR_TEST_PRINT_CONFIG_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/print_config/*.cpp"
)
list(SORT XR_TEST_PRINT_CONFIG_SOURCES)

add_library(test_print_config_matrix OBJECT ${XR_TEST_PRINT_CONFIG_SOURCES})
add_dependencies(test_print_config_matrix xr)

target_compile_features(test_print_config_matrix PRIVATE cxx_std_20)
target_compile_definitions(
  test_print_config_matrix PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_COMPILE_DEFINITIONS>
)

target_include_directories(
  test_print_config_matrix
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/print_config"
  PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)
