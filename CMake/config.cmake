if(NOT TIMER_PRIORITY)
set(TIMER_PRIORITY "Thread::Priority::HIGH")
endif()

add_compile_definitions(LIBXR_TIMER_PRIORITY=${TIMER_PRIORITY})