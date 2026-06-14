if(LIBXR_TEST_DRIVER_MODE)
  add_compile_options(-g)

  file(GLOB_RECURSE XR_TEST_BASE_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp")
  list(SORT XR_TEST_BASE_SOURCES)

  file(GLOB_RECURSE XR_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp")
  list(SORT XR_TEST_RUNTIME_SOURCES)

  set(XR_TEST_HARNESS_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/framework/main.cpp")

  add_executable(test
    ${XR_TEST_HARNESS_SOURCES}
    ${XR_TEST_BASE_SOURCES}
    ${XR_TEST_RUNTIME_SOURCES})
  add_dependencies(test xr)
  target_link_libraries(test PUBLIC xr)
  target_include_directories(
    test
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen)

  if(_xr_system STREQUAL "linux")
    file(GLOB_RECURSE XR_TEST_BINDING_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/binding/*.cpp")
    list(SORT XR_TEST_BINDING_SOURCES)

    list(PREPEND XR_TEST_BINDING_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/framework/binding_main.cpp")

    add_executable(test_binding ${XR_TEST_BINDING_SOURCES})
    add_dependencies(test_binding xr)
    target_link_libraries(test_binding PUBLIC xr)
    target_include_directories(
      test_binding
      PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/framework"
      PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
      PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen)

    add_subdirectory(verify)
    add_subdirectory(measure)
  endif()
  return()
endif()

set(_xr_test_none_build_dir "${CMAKE_BINARY_DIR}/build_test_none")
set(_xr_test_linux_build_dir "${CMAKE_BINARY_DIR}/build_test_linux")

add_custom_target(
  test_none
  COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${_xr_test_none_build_dir}"
          -DLIBXR_TEST_BUILD=True -DLIBXR_TEST_DRIVER_MODE=ON -DLIBXR_SYSTEM=None
          -DCMAKE_BUILD_TYPE=$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
  COMMAND ${CMAKE_COMMAND} --build "${_xr_test_none_build_dir}" --target test -j
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=bare_metal
          "${_xr_test_none_build_dir}/test/test"
  USES_TERMINAL)

add_custom_target(
  test_linux
  COMMAND ${CMAKE_COMMAND} -S "${CMAKE_SOURCE_DIR}" -B "${_xr_test_linux_build_dir}"
          -DLIBXR_TEST_BUILD=True -DLIBXR_TEST_DRIVER_MODE=ON -DLIBXR_SYSTEM=Linux -DLIBXR_DRIVER=Linux
          -DCMAKE_BUILD_TYPE=$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
  COMMAND ${CMAKE_COMMAND} --build "${_xr_test_linux_build_dir}" -j
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=bare_metal
          "${_xr_test_linux_build_dir}/test/test"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=rtos
          "${_xr_test_linux_build_dir}/test/test"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os
          "${_xr_test_linux_build_dir}/test/test"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os
          "${_xr_test_linux_build_dir}/test/test_binding"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os
          "${_xr_test_linux_build_dir}/test/verify/environment/linux_shm/test_linux_shm_topic"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os LIBXR_BENCH_SCALE_DIV=1024
          BENCH_SET=standard
          "${_xr_test_linux_build_dir}/test/measure/perf/bench_linux_shared_topic"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os LIBXR_BENCH_SCALE_DIV=1024
          BENCH_SET=latency
          "${_xr_test_linux_build_dir}/test/measure/perf/bench_linux_shared_topic"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os LIBXR_BENCH_SCALE_DIV=1024
          BENCH_SET=overload
          "${_xr_test_linux_build_dir}/test/measure/perf/bench_linux_shared_topic"
  COMMAND ${CMAKE_COMMAND} -E env XR_TEST_SET=full_os LIBXR_BENCH_SCALE_DIV=1024
          BENCH_SET=modes
          "${_xr_test_linux_build_dir}/test/measure/perf/bench_linux_shared_topic"
  USES_TERMINAL)
