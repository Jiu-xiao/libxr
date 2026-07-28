#include <array>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/uart_concurrency_stress_test.hpp"
#include "driver/uart_loopback_test.hpp"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This hardware runner currently supports ESP32-S3 only."
#endif

#if !LIBXR_ESP_UART_HAS_AHB_GDMA
#error "ESP32-S3 UART loopback requires the UHCI/AHB-GDMA backend."
#endif

namespace
{

constexpr uart_port_t kUart = UART_NUM_2;
constexpr int kTxPin = 1;
constexpr int kRxPin = 2;
constexpr size_t kRxQueueSize = 16384U;
constexpr size_t kMaxFrameSize = 4095U;
// ESP32UartDma uses this value for both the public byte queue and each DMA-buffer half.
// Two max-size frames make the batch-depth-two case independent of owner scheduling.
constexpr size_t kTxBufferSize = kMaxFrameSize * 2U;
constexpr uint32_t kTxQueueDepth = 48U;
constexpr uint32_t kOperationTimeoutMs = 2000U;
constexpr uint32_t kRxQuietTimeMs = 10U;
constexpr uint32_t kCrossCoreTimeoutMs = 120000U;
constexpr uint32_t kStressDurationMs = 12000U;
constexpr uint32_t kStressWorkerReadyTimeoutMs = 5000U;
constexpr uint32_t kStressWorkerStopTimeoutMs = 5000U;
constexpr uint32_t kStressWrapperTimeoutMs = 10000U;
constexpr uint32_t kStressApiCallTimeoutMs = 250U;
constexpr uint32_t kStressProgressTimeoutMs = 4000U;
constexpr uint32_t kStressConfigQuietTimeMs = 500U;
constexpr uint32_t kStressBarrierConfigTimeoutMs = 5000U;
constexpr uint32_t kStressBarrierMarkerTimeoutMs = 10000U;
constexpr uint32_t kStressMinAcceptedWrites = 16U;
constexpr uint32_t kStressMinAcceptedConfigurations = 8U;
constexpr uint32_t kStressMinObservedRxBytes = 512U;
constexpr uint32_t kStressTaskStackSize = 8192U;

constexpr EventBits_t kWriterWrapperDone = EventBits_t{1U << 0U};
constexpr EventBits_t kConfiguratorWrapperDone = EventBits_t{1U << 1U};

constexpr LibXR::UART::Configuration kInitialConfig = {
    921600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};

constexpr std::array<LibXR::UART::Configuration, 4> kConfigurations = {{
    {115200U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
    {921600U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
    {2000000U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
    {5000000U, LibXR::UART::Parity::NO_PARITY, 8U, 1U},
}};

constexpr std::array<size_t, 9> kFrameSizes = {1U,   31U,   32U,   511U, 512U,
                                               513U, 1023U, 2047U, 4095U};

alignas(16) std::array<uint8_t, kMaxFrameSize * 2U> g_tx_scratch{};
alignas(16) std::array<uint8_t, kMaxFrameSize * 2U> g_rx_scratch{};

struct SuiteResult
{
  bool idle_config = false;
  bool loopback = false;
  uint32_t completed_cases = 0U;

  [[nodiscard]] bool Passed() const { return idle_config && loopback; }
};

void PrintLoopbackResult(int caller_core, uint32_t baud, size_t frame_size,
                         uint8_t batch_depth,
                         const LibXRTest::UartLoopbackTestResult& result)
{
  std::printf(
      "[UART_LOOPBACK_CASE] core=%d baud=%" PRIu32 " frame=%u batch=%u rounds=%" PRIu32
      "/2 bytes=%u elapsed_us=%" PRIu64 " status=%s error=%d failed_round=%" PRIu32
      " failed_batch=%u mismatch=%u unexpected_rx=%u\n",
      caller_core, baud, static_cast<unsigned>(frame_size),
      static_cast<unsigned>(batch_depth), result.completed_rounds,
      static_cast<unsigned>(result.verified_bytes), result.elapsed_us,
      result.Passed() ? "PASS" : LibXRTest::UartLoopbackFailureName(result.failure),
      static_cast<int>(result.error), result.failed_round,
      static_cast<unsigned>(result.failed_batch),
      static_cast<unsigned>(result.mismatch_offset),
      static_cast<unsigned>(result.unexpected_rx_bytes));
  std::fflush(stdout);
}

SuiteResult RunSuite(LibXR::UART& uart)
{
  SuiteResult suite;
  const int caller_core = xPortGetCoreID();

  const LibXRTest::UartIdleReconfigureTestCase idle_case = {
      .configurations = std::span<const LibXR::UART::Configuration>(kConfigurations),
      .transitions = 8U,
      .transition_timeout_ms = kOperationTimeoutMs,
      .retry_interval_ms = 1U,
  };
  const auto idle_result = LibXRTest::RunUartIdleReconfigureTest(uart, idle_case);
  suite.idle_config = idle_result.Passed();
  std::printf("[UART_IDLE_CONFIG] core=%d transitions=%" PRIu32 "/8 busy_retries=%" PRIu32
              " elapsed_us=%" PRIu64 " status=%s error=%d failed_transition=%" PRIu32
              "\n",
              caller_core, idle_result.completed_transitions, idle_result.busy_retries,
              idle_result.elapsed_us, idle_result.Passed() ? "PASS" : "FAIL",
              static_cast<int>(idle_result.error), idle_result.failed_transition);
  std::fflush(stdout);
  if (!suite.idle_config)
  {
    return suite;
  }

  for (size_t config_index = 0U; config_index < kConfigurations.size(); ++config_index)
  {
    for (const size_t frame_size : kFrameSizes)
    {
      for (const uint8_t batch_depth : {uint8_t{1U}, uint8_t{2U}})
      {
        const LibXRTest::UartLoopbackTestCase test_case = {
            .uart_config = kConfigurations[config_index],
            .frame_size = frame_size,
            .rounds = 2U,
            .operation_timeout_ms = kOperationTimeoutMs,
            .rx_quiet_time_ms = kRxQuietTimeMs,
            .pattern_seed = 0x53A30000U ^ static_cast<uint32_t>(config_index * 257U) ^
                            static_cast<uint32_t>(frame_size),
            .batch_depth = batch_depth,
        };

        const auto result = LibXRTest::RunUartLoopbackTest(
            uart, test_case, {g_tx_scratch.data(), g_tx_scratch.size()},
            {g_rx_scratch.data(), g_rx_scratch.size()});
        PrintLoopbackResult(caller_core, test_case.uart_config.baudrate, frame_size,
                            batch_depth, result);
        if (!result.Passed())
        {
          return suite;
        }
        suite.completed_cases++;
      }
    }
  }

  suite.loopback = true;
  return suite;
}

struct CrossCoreContext
{
  LibXR::UART* uart = nullptr;
  TaskHandle_t waiter = nullptr;
  SuiteResult result{};
};

void CrossCoreTask(void* argument)
{
  auto* context = static_cast<CrossCoreContext*>(argument);
  context->result = RunSuite(*context->uart);
  xTaskNotifyGive(context->waiter);
  vTaskDelete(nullptr);
}

struct StressWorkerContext
{
  LibXRTest::UartConcurrentConfigStressTest* test = nullptr;
  EventGroupHandle_t done_group = nullptr;
  EventBits_t done_bit = 0U;
  std::atomic<bool> wrapper_returned{false};
};

struct StressDirectionResult
{
  bool event_group_created = false;
  bool writer_task_created = false;
  bool configurator_task_created = false;
  bool wrappers_completed = false;
  LibXRTest::UartConcurrentConfigStressResult stress{};
  bool barrier_ran = false;
  LibXRTest::UartStressTrafficBarrierResult barrier{};
  bool recovery_ran = false;
  LibXRTest::UartLoopbackTestResult recovery{};

  [[nodiscard]] bool StressPassed() const
  {
    return event_group_created && writer_task_created && configurator_task_created &&
           wrappers_completed && stress.Passed();
  }

  [[nodiscard]] bool RecoveryPassed() const
  {
    return barrier_ran && barrier.Passed() && recovery_ran && recovery.Passed();
  }

  [[nodiscard]] bool Passed() const { return StressPassed() && RecoveryPassed(); }
};

void StressWriteTask(void* argument)
{
  auto* context = static_cast<StressWorkerContext*>(argument);
  context->test->RunWriteWorker();

  const EventGroupHandle_t done_group = context->done_group;
  const EventBits_t done_bit = context->done_bit;
  auto* const wrapper_returned = &context->wrapper_returned;
  (void)xEventGroupSetBits(done_group, done_bit);
  wrapper_returned->store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void StressConfigTask(void* argument)
{
  auto* context = static_cast<StressWorkerContext*>(argument);
  context->test->RunConfigWorker();

  const EventGroupHandle_t done_group = context->done_group;
  const EventBits_t done_bit = context->done_bit;
  auto* const wrapper_returned = &context->wrapper_returned;
  (void)xEventGroupSetBits(done_group, done_bit);
  wrapper_returned->store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

bool WaitForStressWrappers(EventGroupHandle_t done_group, EventBits_t expected_bits,
                           const StressWorkerContext& writer_context, bool writer_created,
                           const StressWorkerContext& configurator_context,
                           bool configurator_created)
{
  if (expected_bits == 0U)
  {
    return true;
  }

  const TickType_t timeout_ticks = pdMS_TO_TICKS(kStressWrapperTimeoutMs);
  const TickType_t start_tick = xTaskGetTickCount();
  const EventBits_t completed_bits =
      xEventGroupWaitBits(done_group, expected_bits, pdFALSE, pdTRUE, timeout_ticks);
  if ((completed_bits & expected_bits) != expected_bits)
  {
    return false;
  }

  while ((writer_created &&
          !writer_context.wrapper_returned.load(std::memory_order_acquire)) ||
         (configurator_created &&
          !configurator_context.wrapper_returned.load(std::memory_order_acquire)))
  {
    if (static_cast<TickType_t>(xTaskGetTickCount() - start_tick) >= timeout_ticks)
    {
      return false;
    }
    taskYIELD();
  }
  return true;
}

[[noreturn]] void FailStopForLiveStressContext(
    uint32_t direction, int writer_core, int configurator_core,
    LibXRTest::UartConcurrentConfigStressTest& stress_test, EventGroupHandle_t done_group,
    EventBits_t expected_bits)
{
  const EventBits_t completed_bits =
      done_group == nullptr ? 0U : xEventGroupGetBits(done_group);
  std::printf(
      "[UART_CONFIG_STRESS_WRAPPER_TIMEOUT] direction=%" PRIu32
      " writer_core=%d config_core=%d state=0x%08" PRIX32 " wrapper_bits=0x%08" PRIX32
      " expected_bits=0x%08" PRIX32 " action=FAIL_STOP\n",
      direction, writer_core, configurator_core, stress_test.State(),
      static_cast<uint32_t>(completed_bits), static_cast<uint32_t>(expected_bits));
  std::fflush(stdout);

  // Keep this call stack, the stress object, and both wrapper contexts alive.
  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

void PrintStressResult(uint32_t direction, int writer_core, int configurator_core,
                       UBaseType_t task_priority, const StressDirectionResult& result)
{
  const auto& counters = result.stress.counters;
  std::printf(
      "[UART_CONFIG_STRESS] direction=%" PRIu32
      " writer_core=%d config_core=%d priority=%u duration_ms=%" PRIu32
      " event_group=%s writer_task=%s config_task=%s wrappers=%s status=%s"
      " failure=%s error=%d state=0x%08" PRIX32 " writes=%" PRIu32 "/%" PRIu32
      " write_bytes=%" PRIu32 " write_busy=%" PRIu32 " write_full=%" PRIu32
      " max_write_call_us=%" PRIu32 " boundaries=0x%03" PRIX32 " batches=0x%02" PRIX32
      " configs=%" PRIu32 "/%" PRIu32 " config_busy=%" PRIu32
      " max_config_call_us=%" PRIu32 " rx_bytes=%" PRIu32 " elapsed_us=%" PRIu64 "\n",
      direction, writer_core, configurator_core, static_cast<unsigned>(task_priority),
      kStressDurationMs, result.event_group_created ? "PASS" : "FAIL",
      result.writer_task_created ? "PASS" : "FAIL",
      result.configurator_task_created ? "PASS" : "FAIL",
      result.wrappers_completed ? "PASS" : "FAIL",
      result.StressPassed() ? "PASS" : "FAIL",
      LibXRTest::UartConcurrentConfigStressFailureName(result.stress.failure),
      static_cast<int>(result.stress.error), result.stress.final_state,
      counters.accepted_writes, counters.write_attempts, counters.accepted_write_bytes,
      counters.write_busy_retries, counters.write_full_retries,
      counters.max_write_call_us, counters.required_boundary_length_mask,
      counters.completed_batch_depth_mask, counters.accepted_configurations,
      counters.config_attempts, counters.config_busy_retries, counters.max_config_call_us,
      counters.observed_rx_bytes, result.stress.elapsed_us);
  std::fflush(stdout);
}

void PrintStressBarrierResult(uint32_t direction, const StressDirectionResult& result)
{
  std::printf("[UART_CONFIG_STRESS_BARRIER] direction=%" PRIu32
              " status=%s failure=%s error=%d config_busy=%" PRIu32
              " marker_busy=%" PRIu32 " marker_full=%" PRIu32
              " discarded_prefix=%u trailing=%u elapsed_us=%" PRIu64 "\n",
              direction, result.barrier_ran && result.barrier.Passed() ? "PASS" : "FAIL",
              result.barrier_ran
                  ? LibXRTest::UartStressTrafficBarrierFailureName(result.barrier.failure)
                  : "NOT_RUN",
              static_cast<int>(result.barrier.error), result.barrier.config_busy_retries,
              result.barrier.marker_busy_retries, result.barrier.marker_full_retries,
              static_cast<unsigned>(result.barrier.discarded_prefix_bytes),
              static_cast<unsigned>(result.barrier.unexpected_trailing_bytes),
              result.barrier.elapsed_us);
  std::fflush(stdout);
}

void PrintStressRecoveryResult(uint32_t direction, const StressDirectionResult& result)
{
  std::printf(
      "[UART_CONFIG_STRESS_RECOVERY] direction=%" PRIu32
      " frame=%u batch=2 rounds=%" PRIu32 "/2 bytes=%u elapsed_us=%" PRIu64
      " status=%s failure=%s error=%d failed_round=%" PRIu32
      " failed_batch=%u mismatch=%u unexpected_rx=%u\n",
      direction, static_cast<unsigned>(kMaxFrameSize), result.recovery.completed_rounds,
      static_cast<unsigned>(result.recovery.verified_bytes), result.recovery.elapsed_us,
      result.recovery_ran && result.recovery.Passed() ? "PASS" : "FAIL",
      result.recovery_ran ? LibXRTest::UartLoopbackFailureName(result.recovery.failure)
                          : "NOT_RUN",
      static_cast<int>(result.recovery.error), result.recovery.failed_round,
      static_cast<unsigned>(result.recovery.failed_batch),
      static_cast<unsigned>(result.recovery.mismatch_offset),
      static_cast<unsigned>(result.recovery.unexpected_rx_bytes));
  std::fflush(stdout);
}

StressDirectionResult RunStressDirection(LibXR::UART& uart, uint32_t direction,
                                         int writer_core, int configurator_core)
{
  StressDirectionResult result;
  const LibXRTest::UartConcurrentConfigStressCase stress_case = {
      .configurations = std::span<const LibXR::UART::Configuration>(kConfigurations),
      .max_write_size = kMaxFrameSize,
      .duration_ms = kStressDurationMs,
      .worker_ready_timeout_ms = kStressWorkerReadyTimeoutMs,
      .worker_stop_timeout_ms = kStressWorkerStopTimeoutMs,
      .api_call_timeout_ms = kStressApiCallTimeoutMs,
      .progress_timeout_ms = kStressProgressTimeoutMs,
      .retry_interval_ms = 1U,
      .rx_drain_interval_ms = 1U,
      .config_burst_size = 3U,
      .config_fairness_quiet_ms = kStressConfigQuietTimeMs,
      .max_write_attempts = 50000U,
      .max_config_attempts = 50000U,
      .min_accepted_writes = kStressMinAcceptedWrites,
      .min_accepted_configurations = kStressMinAcceptedConfigurations,
      .min_observed_rx_bytes = kStressMinObservedRxBytes,
      .write_seed = 0x53A3C101U ^ (direction * 0x9E3779B9U),
      .config_seed = 0xC0F1C001U ^ (direction * 0x85EBCA6BU),
  };
  LibXRTest::UartConcurrentConfigStressTest stress_test(
      uart, stress_case, {g_tx_scratch.data(), g_tx_scratch.size()});

  EventGroupHandle_t done_group = xEventGroupCreate();
  result.event_group_created = done_group != nullptr;
  StressWorkerContext writer_context = {
      .test = &stress_test,
      .done_group = done_group,
      .done_bit = kWriterWrapperDone,
  };
  StressWorkerContext configurator_context = {
      .test = &stress_test,
      .done_group = done_group,
      .done_bit = kConfiguratorWrapperDone,
  };

  const UBaseType_t task_priority = uxTaskPriorityGet(nullptr);
  BaseType_t writer_create = pdFAIL;
  BaseType_t configurator_create = pdFAIL;
  if (done_group != nullptr)
  {
    writer_create =
        xTaskCreatePinnedToCore(StressWriteTask, "uart_stress_w", kStressTaskStackSize,
                                &writer_context, task_priority, nullptr, writer_core);
    configurator_create = xTaskCreatePinnedToCore(
        StressConfigTask, "uart_stress_c", kStressTaskStackSize, &configurator_context,
        task_priority, nullptr, configurator_core);
  }
  result.writer_task_created = writer_create == pdPASS;
  result.configurator_task_created = configurator_create == pdPASS;

  if (!result.event_group_created || !result.writer_task_created ||
      !result.configurator_task_created)
  {
    stress_test.Abort(LibXR::ErrorCode::FAILED);
  }
  result.stress = stress_test.RunCoordinator();

  EventBits_t expected_wrapper_bits = 0U;
  if (result.writer_task_created)
  {
    expected_wrapper_bits |= kWriterWrapperDone;
  }
  if (result.configurator_task_created)
  {
    expected_wrapper_bits |= kConfiguratorWrapperDone;
  }
  result.wrappers_completed =
      done_group == nullptr ||
      WaitForStressWrappers(done_group, expected_wrapper_bits, writer_context,
                            result.writer_task_created, configurator_context,
                            result.configurator_task_created);
  if (!result.wrappers_completed)
  {
    FailStopForLiveStressContext(direction, writer_core, configurator_core, stress_test,
                                 done_group, expected_wrapper_bits);
  }

  PrintStressResult(direction, writer_core, configurator_core, task_priority, result);
  if (done_group != nullptr)
  {
    vEventGroupDelete(done_group);
  }

  const LibXRTest::UartStressTrafficBarrierCase barrier_case = {
      .final_config = kInitialConfig,
      .config_timeout_ms = kStressBarrierConfigTimeoutMs,
      .marker_timeout_ms = kStressBarrierMarkerTimeoutMs,
      .retry_interval_ms = 1U,
      .rx_quiet_time_ms = kRxQuietTimeMs,
  };
  result.barrier_ran = true;
  result.barrier = LibXRTest::RetireUartStressTraffic(
      uart, barrier_case, {g_rx_scratch.data(), g_rx_scratch.size()});
  PrintStressBarrierResult(direction, result);

  if (result.barrier.Passed())
  {
    const LibXRTest::UartLoopbackTestCase recovery_case = {
        .uart_config = kInitialConfig,
        .frame_size = kMaxFrameSize,
        .rounds = 2U,
        .operation_timeout_ms = kOperationTimeoutMs,
        .rx_quiet_time_ms = kRxQuietTimeMs,
        .pattern_seed = 0xA11CE000U ^ (direction * 0x27D4EB2DU),
        .batch_depth = 2U,
    };
    result.recovery_ran = true;
    result.recovery = LibXRTest::RunUartLoopbackTest(
        uart, recovery_case, {g_tx_scratch.data(), g_tx_scratch.size()},
        {g_rx_scratch.data(), g_rx_scratch.size()});
  }
  PrintStressRecoveryResult(direction, result);

  std::printf(
      "[UART_CONFIG_STRESS_DIRECTION_FINAL] direction=%" PRIu32
      " writer_core=%d config_core=%d stress=%s barrier=%s recovery=%s all=%s\n",
      direction, writer_core, configurator_core, result.StressPassed() ? "PASS" : "FAIL",
      result.barrier_ran && result.barrier.Passed() ? "PASS" : "FAIL",
      result.RecoveryPassed() ? "PASS" : "FAIL", result.Passed() ? "PASS" : "FAIL");
  std::fflush(stdout);
  return result;
}

const char* TestStatus(bool ran, bool passed)
{
  if (!ran)
  {
    return "SKIP";
  }
  return passed ? "PASS" : "FAIL";
}

}  // namespace

extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit();

  std::printf(
      "\n[UART_LOOPBACK_START] board=ESP32S3 uart=%d tx=%d rx=%d "
      "constructor_core=%d\n",
      static_cast<int>(kUart), kTxPin, kRxPin, xPortGetCoreID());
  std::fflush(stdout);

  static LibXR::ESP32UartDma uart(kUart, kTxPin, kRxPin,
                                  LibXR::ESP32UartDma::PIN_NO_CHANGE,
                                  LibXR::ESP32UartDma::PIN_NO_CHANGE, kRxQueueSize,
                                  kTxBufferSize, kTxQueueDepth, kInitialConfig);

  const SuiteResult same_core_result = RunSuite(uart);

  static CrossCoreContext cross_core_context = {
      &uart,
      xTaskGetCurrentTaskHandle(),
      {},
  };
  BaseType_t create_result = pdFAIL;
  if (same_core_result.Passed())
  {
    create_result =
        xTaskCreatePinnedToCore(CrossCoreTask, "uart_loop_core1", 8192U,
                                &cross_core_context, tskIDLE_PRIORITY + 2U, nullptr, 1);
  }

  bool cross_core_completed = false;
  SuiteResult cross_core_result{};
  if (create_result == pdPASS)
  {
    cross_core_completed =
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kCrossCoreTimeoutMs)) == 1U;
    if (cross_core_completed)
    {
      cross_core_result = cross_core_context.result;
    }
  }

  const bool controls_passed =
      same_core_result.Passed() && cross_core_completed && cross_core_result.Passed();
  bool stress_direction_0_ran = false;
  bool stress_direction_1_ran = false;
  StressDirectionResult stress_direction_0{};
  StressDirectionResult stress_direction_1{};
  if (controls_passed)
  {
    stress_direction_0_ran = true;
    stress_direction_0 = RunStressDirection(uart, 0U, 0, 1);
    if (stress_direction_0.RecoveryPassed())
    {
      stress_direction_1_ran = true;
      stress_direction_1 = RunStressDirection(uart, 1U, 1, 0);
    }
  }

  const bool all_passed = controls_passed && stress_direction_0_ran &&
                          stress_direction_0.Passed() && stress_direction_1_ran &&
                          stress_direction_1.Passed();
  std::printf(
      "[UART_LOOPBACK_FINAL] board=ESP32S3 uart=%d tx=%d rx=%d "
      "same_core=%s same_cases=%" PRIu32 " cross_core=%s cross_cases=%" PRIu32
      " task_create=%s stress_0=%s stress_0_recovery=%s stress_1=%s"
      " stress_1_recovery=%s all=%s\n",
      static_cast<int>(kUart), kTxPin, kRxPin,
      same_core_result.Passed() ? "PASS" : "FAIL", same_core_result.completed_cases,
      cross_core_completed && cross_core_result.Passed() ? "PASS" : "FAIL",
      cross_core_result.completed_cases, create_result == pdPASS ? "PASS" : "FAIL",
      TestStatus(stress_direction_0_ran, stress_direction_0.StressPassed()),
      TestStatus(stress_direction_0_ran, stress_direction_0.RecoveryPassed()),
      TestStatus(stress_direction_1_ran, stress_direction_1.StressPassed()),
      TestStatus(stress_direction_1_ran, stress_direction_1.RecoveryPassed()),
      all_passed ? "PASS" : "FAIL");
  std::fflush(stdout);

  while (true)
  {
    vTaskSuspend(nullptr);
  }
}
