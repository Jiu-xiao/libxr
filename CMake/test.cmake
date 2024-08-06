if(PLATFORM MATCHES "Linux")
message("LibXR union test build.")
else()
message(FATAL_ERROR "Only linux can build union test.")
endif()

project(test)

add_compile_options(-g)

add_executable(test test.cpp)

add_dependencies(test xr)

target_link_libraries(
    test
    PUBLIC xr
)

target_include_directories(
    test
    PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
