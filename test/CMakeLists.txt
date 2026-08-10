if(NOT _xr_system STREQUAL "linux")
  message(FATAL_ERROR "The current test tree only supports LIBXR_SYSTEM=linux.")
endif()

add_compile_options(-g)

file(GLOB_RECURSE XR_TEST_BASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp"
)
list(SORT XR_TEST_BASE_SOURCES)

file(GLOB_RECURSE XR_TEST_RUNTIME_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
)
list(SORT XR_TEST_RUNTIME_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_STDIO_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_stdio/*.cpp"
)
list(SORT XR_TEST_LINUX_STDIO_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_DATABASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_database/*.cpp"
)
list(SORT XR_TEST_LINUX_DATABASE_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_SHM_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_shm/*.cpp"
)
list(SORT XR_TEST_LINUX_SHM_SOURCES)

file(GLOB_RECURSE XR_TEST_LINUX_BENCH_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/linux_bench/*.cpp"
)
list(SORT XR_TEST_LINUX_BENCH_SOURCES)

set(XR_TEST_HARNESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/framework/main.cpp")

add_executable(
  test
  ${XR_TEST_HARNESS_SOURCES}
  ${XR_TEST_BASE_SOURCES}
  ${XR_TEST_RUNTIME_SOURCES}
  ${XR_TEST_LINUX_STDIO_SOURCES}
  ${XR_TEST_LINUX_DATABASE_SOURCES}
  ${XR_TEST_LINUX_SHM_SOURCES}
  ${XR_TEST_LINUX_BENCH_SOURCES}
)
add_dependencies(test xr)

target_link_libraries(test PUBLIC xr)

add_executable(
  linux_uart_test
  standalone/linux_uart_main.cpp
  runtime/driver/test_linux_uart.cpp
)
add_dependencies(linux_uart_test xr)
target_link_libraries(linux_uart_test PUBLIC xr)
add_dependencies(test linux_uart_test)

target_include_directories(
  test
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/framework"
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

target_include_directories(
  linux_uart_test
  PUBLIC $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)

add_library(test_stm32_timebase_sampling OBJECT driver/st/test_stm32_timebase_sampling.cpp)
target_compile_features(test_stm32_timebase_sampling PRIVATE cxx_std_20)
target_include_directories(
  test_stm32_timebase_sampling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../driver/st
)
add_dependencies(test test_stm32_timebase_sampling)

set(MSPM0_FAKE_SDK_COMMON
    "${CMAKE_CURRENT_SOURCE_DIR}/driver/mspm0/fake_sdk/common"
)
set(MSPM0_FAKE_SDK_G3519
    "${CMAKE_CURRENT_SOURCE_DIR}/driver/mspm0/fake_sdk/g3519"
)
set(MSPM0_FAKE_SDK_G3507
    "${CMAKE_CURRENT_SOURCE_DIR}/driver/mspm0/fake_sdk/g3507"
)

add_executable(
  test_mspm0_dma_dispatcher
  driver/mspm0/test_mspm0_dma_dispatcher.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0/mspm0_dma_dispatcher.cpp
)
target_compile_features(test_mspm0_dma_dispatcher PRIVATE cxx_std_20)
target_compile_definitions(
  test_mspm0_dma_dispatcher
  PRIVATE FAKE_MSPM0_G3519=1
          LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0x3FU
)
target_include_directories(
  test_mspm0_dma_dispatcher
  PRIVATE ${MSPM0_FAKE_SDK_COMMON}
  PRIVATE ${MSPM0_FAKE_SDK_G3519}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0"
  PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
add_dependencies(test test_mspm0_dma_dispatcher)

add_executable(
  test_mspm0_dma_dispatcher_external
  driver/mspm0/test_mspm0_dma_dispatcher.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0/mspm0_dma_dispatcher.cpp
)
target_compile_features(test_mspm0_dma_dispatcher_external PRIVATE cxx_std_20)
target_compile_definitions(
  test_mspm0_dma_dispatcher_external
  PRIVATE FAKE_MSPM0_G3519=1
          LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0x3FU
          LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER=1
)
target_include_directories(
  test_mspm0_dma_dispatcher_external
  PRIVATE ${MSPM0_FAKE_SDK_COMMON}
  PRIVATE ${MSPM0_FAKE_SDK_G3519}
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0"
  PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
)
add_dependencies(test test_mspm0_dma_dispatcher_external)

set(MSPM0_UART_TEST_DRIVER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0/mspm0_dma_dispatcher.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0/mspm0_uart.cpp"
)

function(add_mspm0_uart_test target source fake_device_dir)
  add_executable(
    ${target}
    ${source}
    ${MSPM0_UART_TEST_DRIVER_SOURCES}
  )
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_include_directories(
    ${target}
    PRIVATE ${MSPM0_FAKE_SDK_COMMON}
    PRIVATE ${fake_device_dir}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0"
    PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  )
  target_link_libraries(${target} PRIVATE xr)
endfunction()

add_mspm0_uart_test(
  test_mspm0_uart_g3507
  driver/mspm0/test_mspm0_uart_g3507.cpp
  "${MSPM0_FAKE_SDK_G3507}"
)
target_compile_definitions(
  test_mspm0_uart_g3507 PRIVATE LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0U
)

add_mspm0_uart_test(
  test_mspm0_uart_g3507_extend
  driver/mspm0/test_mspm0_uart_g3507_extend.cpp
  "${MSPM0_FAKE_SDK_G3507}"
)
target_compile_definitions(
  test_mspm0_uart_g3507_extend
  PRIVATE LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0U
)

add_mspm0_uart_test(
  test_mspm0_uart_g3519
  driver/mspm0/test_mspm0_uart_g3519.cpp
  "${MSPM0_FAKE_SDK_G3519}"
)
target_compile_definitions(
  test_mspm0_uart_g3519
  PRIVATE FAKE_MSPM0_G3519=1
          LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0x3FU
)

add_dependencies(
  test
  test_mspm0_uart_g3507
  test_mspm0_uart_g3507_extend
  test_mspm0_uart_g3519
)

get_target_property(
  MSPM0_COMPILE_CONTRACT_XR_INCLUDES xr INTERFACE_INCLUDE_DIRECTORIES
)
set(MSPM0_COMPILE_CONTRACT_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/driver/mspm0/compile_cases/compile_contract_case.cpp"
)
set(MSPM0_COMPILE_CONTRACT_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/mspm0_compile_contract"
)
file(MAKE_DIRECTORY "${MSPM0_COMPILE_CONTRACT_ROOT}")
file(WRITE "${MSPM0_COMPILE_CONTRACT_ROOT}/summary.tsv"
     "case\tdevice\texpected\tresult\tdiagnostic\n"
)

function(
  check_mspm0_compile_contract case_name device case_definition expected_success
  expected_diagnostic
)
  set(case_fake_device
      "${CMAKE_CURRENT_SOURCE_DIR}/driver/mspm0/fake_sdk/${device}"
  )
  set(case_includes
      "${MSPM0_FAKE_SDK_COMMON}"
      "${case_fake_device}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../driver/mspm0"
      ${MSPM0_COMPILE_CONTRACT_XR_INCLUDES}
  )
  set(case_definitions
      "-D${case_definition}=1"
      "-DLIBXR_DEFAULT_SCALAR=${LIBXR_DEFAULT_SCALAR}"
      "-DXR_LOG_MESSAGE_MAX_LEN=${XR_LOG_MESSAGE_MAX_LEN}"
  )
  if(device STREQUAL "g3519")
    list(APPEND case_definitions
         "-DFAKE_MSPM0_G3519=1"
         "-DLIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0x3FU"
    )
  else()
    list(APPEND case_definitions "-DLIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK=0U")
  endif()

  set(previous_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  try_compile(
    case_compiled
    "${MSPM0_COMPILE_CONTRACT_ROOT}/${case_name}"
    SOURCES "${MSPM0_COMPILE_CONTRACT_SOURCE}"
    CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=20"
                "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
                "-DINCLUDE_DIRECTORIES=${case_includes}"
    COMPILE_DEFINITIONS ${case_definitions}
    OUTPUT_VARIABLE case_output
  )
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${previous_try_compile_target_type}")

  file(WRITE "${MSPM0_COMPILE_CONTRACT_ROOT}/${case_name}.log" "${case_output}")
  if(expected_success)
    if(NOT case_compiled)
      message(FATAL_ERROR
              "MSPM0 compile-contract ${case_name} unexpectedly failed:\n${case_output}"
      )
    endif()
    set(case_result "PASS")
    set(case_diagnostic "compiled")
  else()
    if(case_compiled)
      message(FATAL_ERROR
              "MSPM0 compile-contract ${case_name} unexpectedly compiled"
      )
    endif()
    string(REPLACE "|" ";" expected_diagnostic_list "${expected_diagnostic}")
    foreach(required_diagnostic IN LISTS expected_diagnostic_list)
      string(FIND "${case_output}" "${required_diagnostic}" diagnostic_position)
      if(diagnostic_position EQUAL -1)
        message(FATAL_ERROR
                "MSPM0 compile-contract ${case_name} failed for the wrong reason; "
                "expected '${required_diagnostic}':\n${case_output}"
        )
      endif()
    endforeach()
    set(case_result "PASS")
    set(case_diagnostic "${expected_diagnostic}")
  endif()

  file(APPEND "${MSPM0_COMPILE_CONTRACT_ROOT}/summary.tsv"
       "${case_name}\t${device}\t${expected_success}\t${case_result}\t${case_diagnostic}\n"
  )
endfunction()

check_mspm0_compile_contract(
  positive_tx_buffer_main_g3507 g3507 MSPM0_CASE_POSITIVE_TX_BUFFER_MAIN_G3507
  TRUE "compiled"
)
check_mspm0_compile_contract(
  positive_tx_buffer_main_g3519 g3519 MSPM0_CASE_POSITIVE_TX_BUFFER_MAIN_G3519
  TRUE "compiled"
)
check_mspm0_compile_contract(
  positive_extend_g3507 g3507 MSPM0_CASE_POSITIVE_EXTEND_G3507 TRUE "compiled"
)
check_mspm0_compile_contract(
  positive_extend_rx_c_array g3519 MSPM0_CASE_POSITIVE_EXTEND_RX_C_ARRAY TRUE
  "compiled"
)
check_mspm0_compile_contract(
  positive_extend_rx_std_array g3519 MSPM0_CASE_POSITIVE_EXTEND_RX_STD_ARRAY TRUE
  "compiled"
)
check_mspm0_compile_contract(
  positive_extend_uart7 g3519 MSPM0_CASE_POSITIVE_EXTEND_UART7 TRUE "compiled"
)
check_mspm0_compile_contract(
  positive_logical_name_physical_uart7 g3519
  MSPM0_CASE_POSITIVE_LOGICAL_NAME_PHYSICAL_UART7 TRUE "compiled"
)
check_mspm0_compile_contract(
  negative_tx_plain_c_array g3507 MSPM0_CASE_NEGATIVE_TX_PLAIN_C_ARRAY FALSE
  "no matching function for call to|MSPM0UARTTxStorage"
)
check_mspm0_compile_contract(
  negative_tx_plain_std_array g3507 MSPM0_CASE_NEGATIVE_TX_PLAIN_STD_ARRAY FALSE
  "no matching function for call to|MSPM0UARTTxStorage"
)
check_mspm0_compile_contract(
  negative_tx_pointer g3507 MSPM0_CASE_NEGATIVE_TX_POINTER FALSE
  "no matching function for call to|MSPM0UARTTxStorage"
)
check_mspm0_compile_contract(
  negative_tx_const g3507 MSPM0_CASE_NEGATIVE_TX_CONST FALSE
  "no matching function for call to|MSPM0UARTTxStorage"
)
check_mspm0_compile_contract(
  negative_tx_struct g3507 MSPM0_CASE_NEGATIVE_TX_STRUCT FALSE
  "no matching function for call to|MSPM0UARTTxStorage"
)
check_mspm0_compile_contract(
  negative_tx_zero g3507 MSPM0_CASE_NEGATIVE_TX_ZERO FALSE
  "must not be empty"
)
check_mspm0_compile_contract(
  negative_tx_odd g3507 MSPM0_CASE_NEGATIVE_TX_ODD FALSE
  "must contain two equal blocks"
)
check_mspm0_compile_contract(
  negative_tx_oversize g3507 MSPM0_CASE_NEGATIVE_TX_OVERSIZE FALSE
  "block exceeds DMA size"
)
check_mspm0_compile_contract(
  negative_rx_pointer g3519 MSPM0_CASE_NEGATIVE_RX_POINTER FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_const g3519 MSPM0_CASE_NEGATIVE_RX_CONST FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_volatile g3519 MSPM0_CASE_NEGATIVE_RX_VOLATILE FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_const_std_array g3519 MSPM0_CASE_NEGATIVE_RX_CONST_STD_ARRAY FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_volatile_std_array g3519 MSPM0_CASE_NEGATIVE_RX_VOLATILE_STD_ARRAY
  FALSE "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_multidimensional g3519 MSPM0_CASE_NEGATIVE_RX_MULTIDIMENSIONAL FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_wide_element g3519 MSPM0_CASE_NEGATIVE_RX_WIDE_ELEMENT FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_zero g3519 MSPM0_CASE_NEGATIVE_RX_ZERO FALSE "must not be empty"
)
check_mspm0_compile_contract(
  negative_rx_odd g3519 MSPM0_CASE_NEGATIVE_RX_ODD FALSE
  "must contain two halves"
)
check_mspm0_compile_contract(
  negative_rx_oversize g3519 MSPM0_CASE_NEGATIVE_RX_OVERSIZE FALSE
  "ring exceeds DMA size"
)
check_mspm0_compile_contract(
  negative_rx_span g3519 MSPM0_CASE_NEGATIVE_RX_SPAN FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_vector g3519 MSPM0_CASE_NEGATIVE_RX_VECTOR FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_string g3519 MSPM0_CASE_NEGATIVE_RX_STRING FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_enum g3519 MSPM0_CASE_NEGATIVE_RX_ENUM FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_rx_struct g3519 MSPM0_CASE_NEGATIVE_RX_STRUCT FALSE
  "no matching function for call to|MSPM0UARTRxDmaStorage"
)
check_mspm0_compile_contract(
  negative_binding_wrong_direction g3507 MSPM0_CASE_NEGATIVE_BINDING_WRONG_DIRECTION
  FALSE
  "not a UART TX binding"
)
check_mspm0_compile_contract(
  negative_binding_wrong_tx_owner g3519 MSPM0_CASE_NEGATIVE_BINDING_WRONG_TX_OWNER
  FALSE "named TX DMA resource belongs to another UART"
)
check_mspm0_compile_contract(
  negative_binding_wrong_rx_owner g3519 MSPM0_CASE_NEGATIVE_BINDING_WRONG_RX_OWNER
  FALSE "named RX DMA resource belongs to another UART"
)
check_mspm0_compile_contract(
  negative_binding_same_channel g3519 MSPM0_CASE_NEGATIVE_BINDING_SAME_CHANNEL FALSE
  "require distinct DMA channels"
)
check_mspm0_compile_contract(
  negative_binding_g3519_uart1_extend g3519
  MSPM0_CASE_NEGATIVE_BINDING_G3519_UART1_EXTEND FALSE "not Extend-capable"
)
check_mspm0_compile_contract(
  negative_binding_full_marker_false g3519
  MSPM0_CASE_NEGATIVE_BINDING_FULL_MARKER_FALSE FALSE
  "requires a full-channel DMA binding"
)
check_mspm0_compile_contract(
  negative_binding_basic_rx_channel g3519
  MSPM0_CASE_NEGATIVE_BINDING_BASIC_RX_CHANNEL FALSE
  "requires a full DMA channel"
)
check_mspm0_compile_contract(
  negative_binding_half_unsupported g3507
  MSPM0_CASE_NEGATIVE_BINDING_HALF_UNSUPPORTED FALSE
  "does not support a half-transfer interrupt"
)
check_mspm0_compile_contract(
  negative_binding_missing_dispatcher g3519
  MSPM0_CASE_NEGATIVE_BINDING_MISSING_DISPATCHER FALSE
  "requires the shared DMA IRQ dispatcher binding"
)
check_mspm0_compile_contract(
  negative_main_binding_missing_dispatcher g3519
  MSPM0_CASE_NEGATIVE_MAIN_BINDING_MISSING_DISPATCHER FALSE
  "requires the shared DMA IRQ dispatcher binding"
)
check_mspm0_compile_contract(
  negative_binding_missing_dma g3507 MSPM0_CASE_NEGATIVE_BINDING_MISSING_DMA FALSE
  "DMA_MISSING_CHAN_ID"
)
check_mspm0_compile_contract(
  negative_main_g3507_uart0 g3507 MSPM0_CASE_NEGATIVE_MAIN_G3507_UART0 FALSE
  "physical Extend UART must use the MSPM0 Extend DMA path"
)
check_mspm0_compile_contract(
  negative_extend_g3507_uart1 g3507 MSPM0_CASE_NEGATIVE_EXTEND_G3507_UART1 FALSE
  "not Extend-capable"
)
check_mspm0_compile_contract(
  negative_main_g3519_uart0 g3519 MSPM0_CASE_NEGATIVE_MAIN_G3519_UART0 FALSE
  "physical Extend UART must use the MSPM0 Extend DMA path"
)
check_mspm0_compile_contract(
  negative_main_g3519_uart7 g3519 MSPM0_CASE_NEGATIVE_MAIN_G3519_UART7 FALSE
  "physical Extend UART must use the MSPM0 Extend DMA path"
)

add_custom_target(mspm0_compile_contract)
add_dependencies(test mspm0_compile_contract)

file(GLOB_RECURSE XR_TEST_PRINT_CONFIG_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/print_config/*.cpp"
)
list(SORT XR_TEST_PRINT_CONFIG_SOURCES)

add_library(test_print_config_matrix OBJECT ${XR_TEST_PRINT_CONFIG_SOURCES})
add_dependencies(test_print_config_matrix xr)

target_compile_features(test_print_config_matrix PRIVATE cxx_std_20)
target_compile_definitions(
  test_print_config_matrix PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_COMPILE_DEFINITIONS>
)

target_include_directories(
  test_print_config_matrix
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/print_config"
  PRIVATE $<TARGET_PROPERTY:xr,INTERFACE_INCLUDE_DIRECTORIES>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/Eigen
)
