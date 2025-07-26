cmake_minimum_required(VERSION 3.11)

if(NOT TARGET xr)
    message(FATAL_ERROR "Need LibXR to build.")
endif()

# 1. 递归查找所有 .cpp 文件
file(GLOB_RECURSE XRUSB_ALL_CPP "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

# 2. 递归查找所有目录（包括根目录）
file(GLOB_RECURSE XRUSB_ALL_PATHS
    LIST_DIRECTORIES true
    "${CMAKE_CURRENT_LIST_DIR}/*"
)

# 3. 过滤出所有是目录的路径，并加入 include path
set(XRUSB_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
foreach(path ${XRUSB_ALL_PATHS})
    if(IS_DIRECTORY "${path}")
        list(APPEND XRUSB_INCLUDE_DIRS "${path}")
    endif()
endforeach()
list(REMOVE_DUPLICATES XRUSB_INCLUDE_DIRS)

# 4. 添加到 target
target_include_directories(xr PUBLIC ${XRUSB_INCLUDE_DIRS})
target_sources(xr PRIVATE ${XRUSB_ALL_CPP})
