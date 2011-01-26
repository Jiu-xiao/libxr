# include(FindLibraryWithDebug)

if (ATLAS_INCLUDES AND ATLAS_LIBRARIES)
  set(ATLAS_FIND_QUIETLY TRUE)
endif (ATLAS_INCLUDES AND ATLAS_LIBRARIES)

find_path(ATLAS_INCLUDES
  NAMES
  cblas.h
  PATHS
  $ENV{ATLASDIR}/include
  ${INCLUDE_INSTALL_DIR}
)

find_file(ATLAS_LIB libatlas.so.3 PATHS /usr/lib $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
find_library(ATLAS_LIB atlas PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})

find_file(ATLAS_CBLAS libcblas.so.3 PATHS /usr/lib $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
find_library(ATLAS_CBLAS cblas PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})

find_file(ATLAS_LAPACK liblapack_atlas.so.3 PATHS /usr/lib $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
find_library(ATLAS_LAPACK lapack_atlas PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})

if(NOT ATLAS_LAPACK)
  find_file(ATLAS_LAPACK liblapack.so.3 PATHS /usr/lib/atlas $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
  find_library(ATLAS_LAPACK lapack PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
endif(NOT ATLAS_LAPACK)

find_file(ATLAS_F77BLAS libf77blas.so.3 PATHS /usr/lib $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
find_library(ATLAS_F77BLAS f77blas PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})

if(ATLAS_LIB AND ATLAS_CBLAS AND ATLAS_LAPACK AND ATLAS_F77BLAS)

  set(ATLAS_LIBRARIES ${ATLAS_LAPACK} ${ATLAS_CBLAS}  ${ATLAS_F77BLAS} ${ATLAS_LIB})
  
  # search the default lapack lib link to it
  find_library(ATLAS_REFERENCE_LAPACK lapack PATHS $ENV{ATLASDIR} ${LIB_INSTALL_DIR})
  if(ATLAS_REFERENCE_LAPACK)
    set(ATLAS_LIBRARIES ${ATLAS_LIBRARIES} ${ATLAS_REFERENCE_LAPACK})
  endif()
  
endif(ATLAS_LIB AND ATLAS_CBLAS AND ATLAS_LAPACK AND ATLAS_F77BLAS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ATLAS DEFAULT_MSG
                                  ATLAS_INCLUDES ATLAS_LIBRARIES)

mark_as_advanced(ATLAS_INCLUDES ATLAS_LIBRARIES)
