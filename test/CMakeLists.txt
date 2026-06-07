add_compile_options(-g)

file(GLOB_RECURSE XR_TEST_BASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp"
)
list(SORT XR_TEST_BASE_SOURCES)

file(GLOB_RECURSE XR_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
)
list(SORT XR_TEST_RUNTIME_SOURCES)

set(XR_TEST_HARNESS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/harness/main.cpp"
)
set(XR_TEST_MATRIX_TOOL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/harness/test_matrix_tool.cpp"
)

add_executable(test
  ${XR_TEST_HARNESS_SOURCES}
  ${XR_TEST_BASE_SOURCES}
  ${XR_TEST_RUNTIME_SOURCES}
)
add_dependencies(test xr)

target_link_libraries(test PUBLIC xr)

target_include_directories(
  test
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/harness"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

add_executable(test_matrix_tool
  ${XR_TEST_MATRIX_TOOL_SOURCES}
)
add_dependencies(test_matrix_tool xr)

target_link_libraries(test_matrix_tool PUBLIC xr)

target_include_directories(
  test_matrix_tool
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/harness"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

file(GLOB_RECURSE XR_TEST_BINDING_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/binding/*.cpp"
)
list(SORT XR_TEST_BINDING_SOURCES)

list(PREPEND XR_TEST_BINDING_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/harness/binding_main.cpp"
)

add_executable(test_binding ${XR_TEST_BINDING_SOURCES})
add_dependencies(test_binding xr)

target_link_libraries(test_binding PUBLIC xr)

target_include_directories(
  test_binding
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/harness"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

add_subdirectory(verify)
add_subdirectory(measure)
