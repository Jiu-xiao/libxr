/**
 * @file test_base.hpp
 * @brief base/runtime 测试入口声明。 Base/runtime test entrypoint declarations.
 *
 * 作用 / Purpose:
 * 1. 收集主测试执行器要调用的入口符号。 Collect the callable entry symbols used by the main test runner.
 * 2. 把执行矩阵保持在一个显式声明边界上，避免文件搬动后悄悄脱离 runner。 Keep the execution matrix explicit at the declaration boundary so moved files do not silently disappear from the runner.
 */
void test_async();
void test_assert();
void test_linux_database_raw();
void test_linux_database_sequential();
void test_def();
void test_color();
void test_crc();
void test_float_encoder();
void test_event();
void test_flag();
void test_inertia();
void test_list();
void test_kinematic();
void test_mpmc_queue();
void test_object_pool();
void test_linux_stdio_print();
void test_message_packet();
void test_message_topic();
void test_queue();
void test_spsc_queue();
void test_lock_free_pool();
void test_rbt();
void test_ramfs();
void test_semaphore();
void test_mutex();
void test_stack();
void test_terminal_command();
void test_terminal_display();
void test_terminal();
void test_thread();
void test_timebase();
void test_timer();
void test_rw_runtime();
void test_pipe_runtime();
void test_message_runtime();
void test_app_framework_application();
void test_app_framework_hardware();
void test_database();
void test_logger();
void test_terminal_input();
void test_time();
void test_transform();
void test_double_buffer();
void test_type();
void test_string();
void test_cycle_value();
void test_pid();
void test_pipe();
void test_print();
void test_rw();
void test_cb();
void test_memory();
void test_linux_shm_topic();
