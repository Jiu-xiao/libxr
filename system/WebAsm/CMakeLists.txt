file(
    GLOB ${PROJECT_NAME}_SYSTEM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

target_sources(${PROJECT_NAME}
    PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_compile_definitions(${PROJECT_NAME} PUBLIC LIBXR_NOT_SUPPORT_MUTI_THREAD=1)
message("Not support multi thread.")

if(NOT LIBXR_PRINTF_BUFFER_SIZE)
    set(LIBXR_PRINTF_BUFFER_SIZE 1024)
endif()

if(NOT XR_LOG_MESSAGE_MAX_LEN)
    set(LIBXR_PRINTF_BUFFER_SIZE 256)
endif()
