add_executable(test_linux_shm_topic
  "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_linux_shm_topic.cpp"
)
add_dependencies(test_linux_shm_topic xr)

target_link_libraries(test_linux_shm_topic PUBLIC xr)

target_include_directories(
  test_linux_shm_topic
  PUBLIC "${CMAKE_SOURCE_DIR}/test/harness"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC "${CMAKE_SOURCE_DIR}/lib/Eigen"
)
