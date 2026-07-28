idf_component_register(SRCS "main.cpp" PRIV_REQUIRES driver)

set(LIBXR_HARDWARE_TEST_BUILD
    ON
    CACHE BOOL "" FORCE
)
set(LIBXR_NO_EIGEN
    ON
    CACHE BOOL "" FORCE
)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp32.cmake)
target_link_libraries(${COMPONENT_LIB} PRIVATE xr_hardware_test_support)
target_compile_options(
  ${COMPONENT_LIB} PRIVATE -Werror -Wno-error=missing-field-initializers
)
