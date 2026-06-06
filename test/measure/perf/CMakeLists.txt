add_executable(bench_linux_shared_topic
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_linux_shared_topic.cpp"
)
add_dependencies(bench_linux_shared_topic xr)

target_link_libraries(bench_linux_shared_topic PUBLIC xr)

target_include_directories(
  bench_linux_shared_topic
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC "${CMAKE_SOURCE_DIR}/lib/Eigen"
)
