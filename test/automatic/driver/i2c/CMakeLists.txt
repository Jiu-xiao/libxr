set(_i2c_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(_i2c_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_i2c_generated
    "${_i2c_generated_dir}/i2c_operation.inc"
    "${_i2c_generated_dir}/stm32_i2c_runtime.inc"
    "${_i2c_generated_dir}/ch32_i2c_runtime.inc"
    "${_i2c_generated_dir}/hpm_i2c_start.inc"
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT ${_i2c_generated}
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate_i2c_cases.py"
          "${_i2c_root}" "${_i2c_generated_dir}"
  DEPENDS generate_i2c_cases.py "${_i2c_root}/src/core/rw/operation.hpp"
          "${_i2c_root}/src/core/libxr_def.hpp" "${_i2c_root}/driver/st/stm32_i2c.cpp"
          "${_i2c_root}/driver/ch/ch32_i2c.cpp" "${_i2c_root}/driver/hpm/hpm_i2c.cpp"
  VERBATIM
)
add_custom_target(i2c_runtime_sources DEPENDS ${_i2c_generated})
foreach(case IN ITEMS ch32_i2c_runtime stm32_i2c_runtime hpm_i2c_start)
  add_executable(test_${case} test_${case}.cpp)
  add_dependencies(test_${case} i2c_runtime_sources)
  target_include_directories(
    test_${case} PRIVATE "${_i2c_generated_dir}" "${_i2c_root}/driver/st"
  )
  add_test(NAME test_${case} COMMAND test_${case})
  set_tests_properties(test_${case} PROPERTIES TIMEOUT 10)
endforeach()
foreach(filter IN ITEMS LEGACY_FILTER NO_FILTER)
  string(TOLOWER "${filter}" suffix)
  set(target "test_stm32_i2c_${suffix}")
  add_executable(${target} test_stm32_i2c_runtime.cpp)
  add_dependencies(${target} i2c_runtime_sources)
  target_include_directories(
    ${target} PRIVATE "${_i2c_generated_dir}" "${_i2c_root}/driver/st"
  )
  target_compile_definitions(${target} PRIVATE TEST_${filter})
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES TIMEOUT 10)
endforeach()
