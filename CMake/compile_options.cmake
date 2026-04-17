include(CheckCXXCompilerFlag)

set(LIBXR_COMMON_COMPILE_OPTIONS -Wno-psabi)

check_cxx_compiler_flag("-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
                        LIBXR_HAS_FMACRO_PREFIX_MAP)
if(LIBXR_HAS_FMACRO_PREFIX_MAP)
  list(APPEND LIBXR_COMMON_COMPILE_OPTIONS
       -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.)
endif()
