cmake_minimum_required(VERSION 3.16)

set(LIBXR_SINGLE_CORE
    OFF
    CACHE BOOL "" FORCE
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD OFF)
project(libxr_uart_loopback)
