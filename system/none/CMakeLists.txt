file(
  GLOB ${PROJECT_NAME}_SYSTEM_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)
list(SORT ${PROJECT_NAME}_SYSTEM_SOURCES)

target_sources(${PROJECT_NAME}
  PRIVATE ${${PROJECT_NAME}_SYSTEM_SOURCES}
)

target_include_directories(${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(${PROJECT_NAME}
  PUBLIC LIBXR_NOT_SUPPORT_MUTI_THREAD=1
)
message(STATUS "Multi-thread support is disabled for system/none.")
