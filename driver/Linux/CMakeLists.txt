file(
    GLOB ${PROJECT_NAME}_DRIVER_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

target_sources(${PROJECT_NAME}
    PRIVATE ${${PROJECT_NAME}_DRIVER_SOURCES})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${CMAKE_CURRENT_LIST_DIR})

find_library(WPA_CLIENT_LIB wpa_client)

if(WPA_CLIENT_LIB)
    message(STATUS "wpa_client found.")
    target_compile_definitions(xr PUBLIC HAVE_WPA_CLIENT)
    target_link_libraries(xr PUBLIC ${WPA_CLIENT_LIB})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBNM REQUIRED libnm)
    target_compile_options(xr PUBLIC ${LIBNM_CFLAGS_OTHER})
    target_include_directories(xr PUBLIC ${LIBNM_INCLUDE_DIRS})
    target_link_libraries(xr PUBLIC ${LIBNM_LIBRARIES})
else()
    message(WARNING "wpa_client not found.")
    message(WARNING "Use `sudo apt install libwpa-client-dev libnm-dev` to install wpa_client and libnm.")
endif()
