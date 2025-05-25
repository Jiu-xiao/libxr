file(
    GLOB ${PROJECT_NAME}_DRIVER_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

target_sources(${PROJECT_NAME}
    PRIVATE ${${PROJECT_NAME}_DRIVER_SOURCES})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

find_library(WPA_CLIENT_LIB wpa_client)

if(WPA_CLIENT_LIB)
    target_compile_definitions(xr PUBLIC HAVE_WPA_CLIENT)
    target_link_libraries(xr PUBLIC ${WPA_CLIENT_LIB})
endif()
