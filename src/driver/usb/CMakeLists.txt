cmake_minimum_required(VERSION 3.11)

if(NOT TARGET xr)
  message(FATAL_ERROR "Need LibXR to build.")
endif()

# Recursively collect translation units; file changes trigger reconfigure.
file(GLOB_RECURSE XRUSB_ALL_CPP CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

# Recursively collect include directories, including the root directory.
file(GLOB_RECURSE XRUSB_ALL_PATHS LIST_DIRECTORIES true
     "${CMAKE_CURRENT_LIST_DIR}/*")

set(XRUSB_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
foreach(path ${XRUSB_ALL_PATHS})
  if(IS_DIRECTORY "${path}")
    list(APPEND XRUSB_INCLUDE_DIRS "${path}")
  endif()
endforeach()
list(REMOVE_DUPLICATES XRUSB_INCLUDE_DIRS)

target_include_directories(xr PUBLIC ${XRUSB_INCLUDE_DIRS})
target_sources(xr PRIVATE ${XRUSB_ALL_CPP})
