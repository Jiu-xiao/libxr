if(LIBXR_TEST_DRIVER_MODE)
  add_compile_options(-g)

  file(GLOB_RECURSE XR_TEST_BASE_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp")
  list(SORT XR_TEST_BASE_SOURCES)

  file(GLOB_RECURSE XR_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp")
  list(SORT XR_TEST_RUNTIME_SOURCES)

  set(XR_TEST_LINUX_EXTRA_SOURCES)
  if(_xr_system STREQUAL "linux")
    file(GLOB_RECURSE XR_TEST_BINDING_CASE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/binding/*.cpp")
    list(SORT XR_TEST_BINDING_CASE_SOURCES)
    list(FILTER XR_TEST_BINDING_CASE_SOURCES EXCLUDE REGEX "/binding_main\\.cpp$")

    file(GLOB_RECURSE XR_TEST_VERIFY_CASE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/verify/**/*.cpp")
    list(SORT XR_TEST_VERIFY_CASE_SOURCES)
    list(FILTER XR_TEST_VERIFY_CASE_SOURCES EXCLUDE REGEX "/main\\.cpp$")

    file(GLOB_RECURSE XR_TEST_MEASURE_CASE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/measure/**/*.cpp")
    list(SORT XR_TEST_MEASURE_CASE_SOURCES)
    list(FILTER XR_TEST_MEASURE_CASE_SOURCES EXCLUDE REGEX "/bench_linux_shared_topic\\.cpp$")

    list(APPEND XR_TEST_LINUX_EXTRA_SOURCES
         ${XR_TEST_BINDING_CASE_SOURCES}
         ${XR_TEST_VERIFY_CASE_SOURCES}
         ${XR_TEST_MEASURE_CASE_SOURCES})
  endif()

  set(XR_TEST_HARNESS_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/framework/main.cpp")

  add_executable(test
    ${XR_TEST_HARNESS_SOURCES}
    ${XR_TEST_BASE_SOURCES}
    ${XR_TEST_RUNTIME_SOURCES}
    ${XR_TEST_LINUX_EXTRA_SOURCES})
  add_dependencies(test xr)
  target_link_libraries(test PUBLIC xr)
  target_include_directories(
    test
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen)

  return()
endif()

set(_xr_test_none_build_dir "${CMAKE_BINARY_DIR}/build_test_none")
set(_xr_test_linux_build_dir "${CMAKE_BINARY_DIR}/build_test_linux")

add_custom_target(
  test_none
  COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${_xr_test_none_build_dir}"
          -DLIBXR_TEST_BUILD=True -DLIBXR_TEST_DRIVER_MODE=ON -DLIBXR_SYSTEM=None
          -DCMAKE_BUILD_TYPE=$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
  COMMAND ${CMAKE_COMMAND} --build "${_xr_test_none_build_dir}" --target test -j4
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=bare_metal
          "${_xr_test_none_build_dir}/test/test"
  USES_TERMINAL)

add_custom_target(
  test_linux
  COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${_xr_test_linux_build_dir}"
          -DLIBXR_TEST_BUILD=True -DLIBXR_TEST_DRIVER_MODE=ON -DLIBXR_SYSTEM=Linux -DLIBXR_DRIVER=Linux
          -DCMAKE_BUILD_TYPE=$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
  COMMAND ${CMAKE_COMMAND} --build "${_xr_test_linux_build_dir}" -j4
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=bare_metal
          "${_xr_test_linux_build_dir}/test/test"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=rtos
          "${_xr_test_linux_build_dir}/test/test"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os
          "${_xr_test_linux_build_dir}/test/test"
  USES_TERMINAL)
