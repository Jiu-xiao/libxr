cmake_minimum_required(VERSION 3.11)

if(NOT TARGET xr)
message(FATAL_ERROR "Need LibXR to build.")
endif()

# Source File
file(
  GLOB XRUSB_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

target_sources(xr
  PRIVATE ${XRUSB_SOURCES})
