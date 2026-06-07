file(GLOB XR_BENCH_LINUX_SHARED_TOPIC_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(SORT XR_BENCH_LINUX_SHARED_TOPIC_SOURCES)

add_executable(bench_linux_shared_topic ${XR_BENCH_LINUX_SHARED_TOPIC_SOURCES})
add_dependencies(bench_linux_shared_topic xr)
add_dependencies(bench_linux_shared_topic test_matrix_validate)

target_link_libraries(bench_linux_shared_topic PUBLIC xr)

target_include_directories(
  bench_linux_shared_topic
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC "${CMAKE_SOURCE_DIR}/lib/Eigen"
)
