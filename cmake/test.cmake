if(_xr_system MATCHES "linux")
  message(STATUS "LibXR union test build.")
else()
  message(FATAL_ERROR "Only linux can build union test.")
endif()

project(test)

add_compile_options(-g)

set(TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp")

file(GLOB TEST_CASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/test/test_*.cpp"
)
list(SORT TEST_CASE_SOURCES)
list(APPEND TEST_SOURCES ${TEST_CASE_SOURCES})

add_executable(test ${TEST_SOURCES})
add_dependencies(test xr)

target_link_libraries(test PUBLIC xr)

target_include_directories(
  test
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/Eigen
)
