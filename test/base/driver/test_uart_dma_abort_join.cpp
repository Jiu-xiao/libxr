#include <atomic>
#include <cstdint>
#include <thread>

#include "model/uart_dma_abort_join.hpp"
#include "test.hpp"

namespace
{

using AbortJoin = LibXR::UartDmaAbortJoin;
using Direction = AbortJoin::Direction;
using Phase = AbortJoin::Phase;

constexpr uint32_t TX = AbortJoin::Mask(Direction::TX);
constexpr uint32_t RX = AbortJoin::Mask(Direction::RX);
constexpr uint32_t BOTH = TX | RX;

void FinishApplied(AbortJoin& join)
{
  ASSERT(join.IsQuiescent());
  join.MarkApplied();
  ASSERT(join.GetPhase() == Phase::APPLIED);
  join.Finish();
  ASSERT(join.GetPhase() == Phase::IDLE);
}

void TestZeroDirectionQuiescesAfterLaunchBoundary()
{
  AbortJoin join;
  join.Begin(0U);
  ASSERT(join.GetPhase() == Phase::LAUNCHING);
  ASSERT(!join.TryQuiescent());
  ASSERT(join.EndLaunch());
  FinishApplied(join);
}

void TestSynchronousStopsWaitForLaunchBoundary()
{
  AbortJoin join;
  join.Begin(BOTH);

  ASSERT(!join.CompleteStopped(Direction::TX));
  ASSERT(!join.CompleteStopped(Direction::RX));
  ASSERT(!join.Pending(Direction::TX));
  ASSERT(!join.Pending(Direction::RX));
  ASSERT(!join.AsyncStopArmed(Direction::TX));
  ASSERT(!join.AsyncStopArmed(Direction::RX));
  ASSERT(join.GetPhase() == Phase::LAUNCHING);

  ASSERT(join.EndLaunch());
  FinishApplied(join);
}

void RunAsynchronousOrder(Direction first, Direction second)
{
  AbortJoin join;
  join.Begin(BOTH);
  ASSERT(join.ArmAsyncStop(Direction::TX));
  ASSERT(join.ArmAsyncStop(Direction::RX));
  ASSERT(!join.EndLaunch());

  ASSERT(!join.FinishAsyncStopIrq(first, true));
  ASSERT(!join.Pending(first));
  ASSERT(!join.AsyncStopArmed(first));
  ASSERT(join.Pending(second));
  ASSERT(join.AsyncStopArmed(second));

  ASSERT(join.FinishAsyncStopIrq(second, true));
  FinishApplied(join);
}

void TestAsynchronousStopsInBothOrders()
{
  RunAsynchronousOrder(Direction::TX, Direction::RX);
  RunAsynchronousOrder(Direction::RX, Direction::TX);
}

void TestEnabledDmaRetainsArmedObligation()
{
  AbortJoin join;
  join.Begin(TX);
  ASSERT(join.ArmAsyncStop(Direction::TX));
  ASSERT(!join.EndLaunch());

  ASSERT(!join.FinishAsyncStopIrq(Direction::TX, false));
  ASSERT(join.Pending(Direction::TX));
  ASSERT(join.AsyncStopArmed(Direction::TX));
  ASSERT(join.GetPhase() == Phase::STOPPING);

  ASSERT(join.FinishAsyncStopIrq(Direction::TX, true));
  FinishApplied(join);
}

void TestMixedSynchronousAndAsynchronousStops()
{
  AbortJoin join;
  join.Begin(BOTH);
  ASSERT(!join.CompleteStopped(Direction::TX));
  ASSERT(join.ArmAsyncStop(Direction::RX));
  ASSERT(!join.EndLaunch());

  ASSERT(join.FinishAsyncStopIrq(Direction::RX, true));
  FinishApplied(join);
}

void TestAsyncCompletionDuringLaunchWaitsForOuterCalls()
{
  AbortJoin join;
  join.Begin(BOTH);
  ASSERT(join.ArmAsyncStop(Direction::TX));
  ASSERT(join.ArmAsyncStop(Direction::RX));

  ASSERT(!join.FinishAsyncStopIrq(Direction::TX, true));
  ASSERT(!join.FinishAsyncStopIrq(Direction::RX, true));
  ASSERT(join.GetPhase() == Phase::LAUNCHING);
  ASSERT(join.EndLaunch());
  FinishApplied(join);
}

void TestConcurrentDirectionCompletionHasOneQuiescentPublisher()
{
  constexpr uint32_t ITERATIONS = 2000U;
  for (uint32_t iteration = 0U; iteration < ITERATIONS; ++iteration)
  {
    AbortJoin join;
    join.Begin(BOTH);
    ASSERT(join.ArmAsyncStop(Direction::TX));
    ASSERT(join.ArmAsyncStop(Direction::RX));
    ASSERT(!join.EndLaunch());

    std::atomic<uint32_t> quiescent_publishers{0U};
    auto complete = [&](Direction direction)
    {
      if (join.FinishAsyncStopIrq(direction, true))
      {
        quiescent_publishers.fetch_add(1U, std::memory_order_relaxed);
      }
    };

    std::thread tx(complete, Direction::TX);
    std::thread rx(complete, Direction::RX);
    tx.join();
    rx.join();

    ASSERT(quiescent_publishers.load(std::memory_order_relaxed) == 1U);
    FinishApplied(join);
  }
}

}  // namespace

void test_uart_dma_abort_join()
{
  TestZeroDirectionQuiescesAfterLaunchBoundary();
  TestSynchronousStopsWaitForLaunchBoundary();
  TestAsynchronousStopsInBothOrders();
  TestEnabledDmaRetainsArmedObligation();
  TestMixedSynchronousAndAsynchronousStops();
  TestAsyncCompletionDuringLaunchWaitsForOuterCalls();
  TestConcurrentDirectionCompletionHasOneQuiescentPublisher();
}
