if(LIBXR_SYSTEM MATCHES "Linux")
message("LibXR union test build.")
else()
message(FATAL_ERROR "Only linux can build union test.")
endif()

project(test)

add_compile_options(-g)

file(
  GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")


add_executable(test ${TEST_SOURCES})

add_dependencies(test xr)

target_link_libraries(
    test
    PUBLIC xr
)

target_include_directories(
    test
    PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>/test
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/Eigen
)
