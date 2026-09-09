if(NOT _xr_system STREQUAL "linux")
  message(FATAL_ERROR "Automatic tests require LIBXR_SYSTEM=linux.")
endif()

add_compile_options(-g)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")

add_library(
  test_assert_disabled OBJECT
  core/assert/libxr_assert/assert_disabled_headers.cpp ../../src/core/rw/read_port.cpp
  ../../src/core/rw/write_port.cpp ../../src/core/rw/write_stream.cpp
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

foreach(debug RANGE 0 1)
  foreach(dev RANGE 0 1)
    set(mode_target test_assert_${debug}_${dev})
    add_executable(${mode_target} core/assert/libxr_assert/assert_modes.cpp)
    target_include_directories(${mode_target} PRIVATE . ../../src/core)
    if(debug)
      target_compile_definitions(${mode_target} PRIVATE LIBXR_DEBUG_BUILD)
    endif()
    if(dev)
      target_compile_definitions(${mode_target} PRIVATE LIBXR_DEV_ASSERT_BUILD)
    endif()
    add_test(NAME ${mode_target} COMMAND ${mode_target})
    set_tests_properties(${mode_target} PROPERTIES TIMEOUT 10)
  endforeach()
endforeach()

add_executable(
  libxr_test
  main.cpp
  core/assert/libxr_assert/test_assert.cpp
  core/libxr_cb/test_cb.cpp
  core/libxr_color/test_color.cpp
  core/libxr_def/test_def.cpp
  core/libxr_mem/test_mem.cpp
  core/libxr_pipe/test_pipe.cpp
  core/libxr_string/test_string.cpp
  core/libxr_time/test_time.cpp
  core/libxr_type/test_type.cpp
  core/print/format/format_frontend_parser/test_format_frontend.cpp
  core/print/print/test_print.cpp
  core/print/print_api/test_print_api.cpp
  core/print/printf/printf_frontend_parser/test_printf_frontend.cpp
  core/print/writer/test_print_failure.cpp
  core/rw/read_port/test_read_port.cpp
  core/rw/stdio/test_linux_stdio_print.cpp
  core/rw/write_port/test_write_port.cpp
  core/rw/write_stream/test_write_stream.cpp
  driver/uart/test_uart.cpp
  driver/timebase/test_timebase.cpp
  middleware/app_framework/application/test_application.cpp
  middleware/app_framework/hardware/test_hardware.cpp
  middleware/database/database/test_database.cpp
  middleware/database/raw/raw/test_linux_database_raw.cpp
  middleware/database/raw_sequential/test_linux_database_sequential.cpp
  middleware/event/test_event.cpp
  middleware/logger/logger/test_logger.cpp
  middleware/message/packet/packet/test_packet.cpp
  middleware/message/topic/bench_latency.cpp
  middleware/message/topic/bench_modes.cpp
  middleware/message/topic/bench_overload.cpp
  middleware/message/topic/bench_standard.cpp
  middleware/message/topic/test_attach_queue.cpp
  middleware/message/topic/test_balance_rr.cpp
  middleware/message/topic/test_cross_process.cpp
  middleware/message/topic/test_lifecycle.cpp
  middleware/message/topic/test_linux_shm_topic.cpp
  middleware/message/topic/test_mixed_modes.cpp
  middleware/message/topic/test_runtime.cpp
  middleware/message/topic/test_topic.cpp
  middleware/ramfs/ramfs/test_ramfs.cpp
  middleware/terminal/command/test_command.cpp
  middleware/terminal/display/test_display.cpp
  middleware/terminal/input/test_input.cpp
  middleware/terminal/terminal/test_integration.cpp
  structure/double_buffer/test_double_buffer.cpp
  structure/list/test_list.cpp
  structure/lockfree_list/test_lockfree_list.cpp
  structure/object_pool/test_object_pool.cpp
  structure/queue/mpmc_queue/test_mpmc_queue.cpp
  structure/queue/queue/test_queue.cpp
  structure/queue/spsc_queue/test_spsc_prefix.cpp
  structure/queue/spsc_queue/test_spsc_queue.cpp
  structure/rbt/test_rbt.cpp
  structure/stack/test_stack.cpp
  system/async/test_async.cpp
  system/thread/test_thread.cpp
  system/mutex/test_mutex.cpp
  system/semaphore/test_semaphore.cpp
  system/timer/test_timer.cpp
  utils/crc/test_crc.cpp
  utils/cycle_value/test_cycle_value.cpp
  utils/flag/test_flag.cpp
  utils/float_encoder/test_encoder.cpp
  utils/inertia/test_inertia.cpp
  utils/kinematic/test_kinematic.cpp
  utils/pid/test_pid.cpp
  utils/serialized_service/test_serialized_service.cpp
  utils/transform/test_transform.cpp
  utils/transform/test_transform_euler_orders.cpp
)
set_target_properties(libxr_test PROPERTIES OUTPUT_NAME test)
target_link_libraries(libxr_test PRIVATE xr)
target_include_directories(libxr_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

find_program(XR_TEST_SCRIPT_EXECUTABLE script)
if(NOT XR_TEST_SCRIPT_EXECUTABLE)
  message(FATAL_ERROR "Automatic tests require the util-linux script program for a PTY.")
endif()
add_test(NAME libxr_automatic COMMAND "${XR_TEST_SCRIPT_EXECUTABLE}" -q -e -c
                                      "\"$<TARGET_FILE:libxr_test>\"" /dev/null
)
set_tests_properties(
  libxr_automatic PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE WORKING_DIRECTORY
                             "${PROJECT_SOURCE_DIR}"
)

add_library(
  test_print_config_matrix OBJECT
  core/print/print/test_float_disabled.cpp
  core/print/print/test_float_fixed_only.cpp
  core/print/print/test_float_precision_limit.cpp
  core/print/print/test_integer_base_disabled.cpp
  core/print/print/test_integer_text_disabled.cpp
  core/print/print/test_pointer_64bit_disabled.cpp
  core/print/print/test_syntax_switches_disabled.cpp
)
add_dependencies(test_print_config_matrix xr)
target_compile_features(test_print_config_matrix PRIVATE cxx_std_20)
target_compile_definitions(
  test_print_config_matrix PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_COMPILE_DEFINITIONS>
)
target_include_directories(
  test_print_config_matrix
  PRIVATE core/print/print $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
          "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Eigen"
)
