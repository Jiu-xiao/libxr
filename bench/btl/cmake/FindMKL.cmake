
if (MKL_INCLUDES AND MKL_LIBRARIES)
  set(CBLAS_FIND_QUIETLY TRUE)
endif (MKL_INCLUDES AND MKL_LIBRARIES)

find_path(MKL_INCLUDES
  NAMES
  cblas.h
  PATHS
  $ENV{MKLDIR}/include
  ${INCLUDE_INSTALL_DIR}
)


message(STATUS ${CMAKE_HOST_SYSTEM_PROCESSOR})

if(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64")

message(STATUS "64 bits")

find_library(MKL_LIBRARIES
  mkl_core
  PATHS
  $ENV{MKLLIB}
  /opt/intel/mkl/*/lib/em64t
  ${LIB_INSTALL_DIR}
)

if(MKL_LIBRARIES)
set(MKL_LIBRARIES ${MKL_LIBRARIES} mkl_intel_lp64 mkl_sequential guide pthread)
endif(MKL_LIBRARIES)

message(STATUS ${MKL_LIBRARIES})

else(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64")

message(STATUS "32 bits")

find_library(MKL_LIBRARIES
  mkl_core mkl_intel mkl_sequential guide pthread
  PATHS
  $ENV{MKLLIB}
  /opt/intel/mkl/*/lib/32
  ${LIB_INSTALL_DIR}
)

endif(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MKL DEFAULT_MSG
                                  MKL_INCLUDES MKL_LIBRARIES)

mark_as_advanced(MKL_INCLUDES MKL_LIBRARIES)
