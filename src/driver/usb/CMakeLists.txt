if(NOT TARGET ${PROJECT_NAME})
  message(FATAL_ERROR "Need LibXR to build.")
endif()

# Keep USB sources recursive so new class/runtime files are picked up automatically.
file(GLOB_RECURSE XRUSB_ALL_CPP CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
)
list(SORT XRUSB_ALL_CPP)

# Current USB code still uses short cross-subtree includes, so keep the required roots
# explicit instead of recursively exporting every future nested directory.
set(XRUSB_INCLUDE_DIRS
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/core
  ${CMAKE_CURRENT_LIST_DIR}/device
  ${CMAKE_CURRENT_LIST_DIR}/device/bos
  ${CMAKE_CURRENT_LIST_DIR}/device/cdc
  ${CMAKE_CURRENT_LIST_DIR}/device/dap
  ${CMAKE_CURRENT_LIST_DIR}/device/dfu
  ${CMAKE_CURRENT_LIST_DIR}/device/gsusb
  ${CMAKE_CURRENT_LIST_DIR}/device/hid
  ${CMAKE_CURRENT_LIST_DIR}/device/uac
)

target_include_directories(${PROJECT_NAME}
  PUBLIC ${XRUSB_INCLUDE_DIRS}
)

target_sources(${PROJECT_NAME}
  PRIVATE ${XRUSB_ALL_CPP}
)
