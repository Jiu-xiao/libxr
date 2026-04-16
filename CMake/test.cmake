if(LIBXR_SYSTEM MATCHES "Linux")
  message("LibXR union test build.")
else()
  message(FATAL_ERROR "Only linux can build union test.")
endif()

project(test)

add_compile_options(-g)

set(LIBXR_UNION_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_async.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_cb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_crc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_cycle_value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_double_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_encoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_event.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_inertia.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_kinematic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_linux_shm_topic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_list.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_mem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_message.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_mutex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_pid.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_pipe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_ramfs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_rbt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_rw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_semaphore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_stack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_string.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_terminal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_timebase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_timer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_transform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_usb_descriptor_policy.cpp)

function(libxr_add_test_target target_name)
  add_executable(${target_name} ${ARGN})
  add_dependencies(${target_name} xr)
  target_link_libraries(${target_name} PRIVATE xr)
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/Eigen)
endfunction()

libxr_add_test_target(test ${LIBXR_UNION_TEST_SOURCES})

libxr_add_test_target(bench_linux_shared_topic
                      ${CMAKE_CURRENT_SOURCE_DIR}/test/bench_linux_shared_topic.cpp)

libxr_add_test_target(bench_linux_system_sync
                      ${CMAKE_CURRENT_SOURCE_DIR}/test/bench_linux_system_sync.cpp)
