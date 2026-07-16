# UART Formal-to-C++ Refinement Map

This document binds model actions to the current C++ worktree. A model result is
not implementation evidence unless every action used by the claim is either mapped
to code or listed as an explicit platform contract.

Status values:

- `mapped`: the action has a concrete C++ linearization point.
- `partial`: the model action is coarser than the current C++ path or still lacks an
  implementation-level checker.
- `contract`: the action is supplied by a platform backend and must be verified there.
- `open`: the action is absent from the current implementation or the mapping is not
  yet established.

## SerializedService

| TLA+ action | C++ linearization point | Status |
| --- | --- | --- |
| `Publish(p)` | `SerializedService::Invoke`: `state_.fetch_or(events, release)` | mapped |
| `ClaimAndTake(p)` | successful `compare_exchange_weak(observed, OWNER_BIT, acquire)`, with the pre-CAS `observed` event bits becoming the first snapshot | mapped |
| `Take(p)` | `state_.exchange(OWNER_BIT, acq_rel)` after a failed release CAS | mapped |
| `Handle(p)` | `handler(snapshot)` in the winning invocation's context | mapped |
| `ReleaseRetry(p)` | failed final `compare_exchange_strong`, with event bits returned in `expected` | mapped |
| `ReleaseEmpty(p)` | successful `compare_exchange_strong(OWNER_BIT, 0, release, acquire)` | mapped |

The TLA+ module checks a finite three-publisher protocol. The GenMC harness executes
the real header under RC11 with three pthread publishers and one handler-reentrant
publication. Neither result is a cutoff proof for an arbitrary number of publishers.
The fused CAS is the first snapshot's linearization point. Events published after it
remain in the shared word for a later `Take(p)`, even if the first handler has not started.
The coarser TX models retain split claim/take actions as a conservative scheduling
over-approximation rather than an exact mapping of this first snapshot boundary.

## Write Publication

| Protocol step | C++ location | Status |
| --- | --- | --- |
| reserve producer ownership | `WritePort::operator()` / `WritePort::Stream::Acquire()` | partial |
| publish payload bytes | `queue_data_->PushBatch(...)` | partial |
| arm operation state | `WriteOperation::MarkAsRunning()` | mapped |
| allocate record identity | `WritePort::BeginSubmission()` | mapped |
| publish metadata | `queue_info_->Push(WriteInfoBlock)` | partial |
| publish WRITE notification | `UartDmaTxModel::Submit()` -> `SerializedService::Invoke(WRITE)` | mapped |

`UartTxPipeline.tla` splits payload publication, metadata publication, and WRITE
notification, including publication while a service owner is active. It treats each
payload as one abstract token; the real SPSC queue's release/acquire edge still requires
an implementation-level checker using the real queue.

WritePort producer ownership is independent of TX service ownership. A callback reached
through `Finish()` may run while another direct or Stream producer owns the port; a
re-entrant write then returns `BUSY` and is not implicitly queued. Deterministic tests
pin the direct-current-submit success case and the Stream, retained-RETRY-head, and
same-round CONFIG-drain `BUSY` boundaries. Callback retry policy remains an application
contract rather than a property of `SerializedService`.

## TX Service

| TLA+ action | C++ location | Status |
| --- | --- | --- |
| service owner context | lambda captured by the `Invoke()` call that wins ownership | partial |
| choose and stage queue head | `UartDmaTxModel::StageNextPending()` / `StagePending()` | partial |
| retain RETRY record | `UartDmaTxModel::TryStartPending()` on `RETRY` | mapped |
| advance an older RETRY head from a later submitter | submission-id match selects the synchronous `SubmitContext`; a mismatch progresses contextlessly and completes through the queued operation | mapped |
| publish durable TX retry | `UartHardwareGate::TryEnterTxStart()` sets `TX_START_PENDING` before claiming | mapped in STM32, CH32, and ESP DMA backends |
| start pending DMA candidate | `StartCandidate(buffers_.PendingBuffer(), ...)` | partial |
| release active terminal | `UartDmaTxModel::ReleaseActive()` | mapped |
| complete queued operation | `WritePort::Finish(...)` | mapped |

`SubmitContext::submission_id` restricts only which synchronous caller may receive a
direct return value. It no longer restricts state-machine progression: a later caller
may start an older retained head, whose result is delivered through that record's own
queued operation. That `Finish()` may execute while the later caller still owns the
WritePort producer lock, so its callback must handle `BUSY` as described above.
`UartTxPipeline.tla` separately represents pending-only candidate staging, active
ownership after a successful commit, private copy before pending publication, RETRY
preservation, the backend-start/software-commit window, and an abstract per-record
finish count. Its three-record cutoff is not a proof for arbitrary queue length, and
the pipeline, combined control, CONFIG-drain,
and hardware models have not been proved as one refinement composition. The TX
refinement therefore remains `partial`.

## TX Pipeline

| TLA+ action | C++ location | Status |
| --- | --- | --- |
| `WriteCopyPayload` | `queue_data_->PushBatch(...)` and pre-metadata `MarkAsRunning()` | partial |
| `WritePublishMetadata` | `queue_info_->Push(WriteInfoBlock)` | partial |
| `WriteRaise` | `UartDmaTxModel::Submit()` | mapped |
| queue-head copy and private completion | `StageNextPending()` -> `StagePending()` -> `PopPayload(buffers_.PendingBuffer(), ...)` | partial |
| `PublishPending` | `pending_valid_ = true` | mapped |
| `SelectPending` | `TryStartPending()` peeks the metadata for the staged pending buffer | partial |
| `BackendRetry` | `StartCandidate(...) == RETRY` with `pending_valid_` retained | mapped |
| `BackendStarted` | platform DMA enable before software commits queue ownership | contract |
| `CommitStarted` | `TryStartPending()` clears pending, pops metadata, flips the pending block, and delivers the result/`Finish()` | partial; the action property binds the new active record to the sole finish-count delta |
| `BackendFailed` | `TryStartPending()` clears pending, pops metadata, delivers `FAILED`, and requests CONFIG without flipping the active block | mapped |
| terminal releases active without resolving its operation again | `ReleaseActive()` | mapped |

The finish counter proves the abstract queue record is resolved at most once. The
`CompletionIdentityMatchesActiveRecord` action property additionally checks that an
active-ownership commit resolves that same record rather than another queued operation.
`FailedDoesNotFlipActiveBlock` checks that resolving a start as `FAILED` does not commit
the staged pending block as active.
It does not prove the complete `WritePort` BLOCK timeout/semaphore protocol or arbitrary
user callback behavior.

## TX Start/Commit Window

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| `ServiceClaim` | current `SerializedService` owner entering TX progression | mapped |
| `TxGateClaim` / `TxGateRelease` | backend `TryEnterTxStart()` / `LeaveTxStart()` | mapped in STM32, CH32, and ESP DMA backends |
| `BackendStarted` / `BackendFailed` | backend result returned by `StartDmaTx(...)` | contract |
| `PublishConfig` | hardware-gate dispatcher or caller publishes CONFIG into the current service owner | partial |
| `PublishComplete` / `PublishError` | IRQ path publishes a terminal event into the current service owner | partial |
| `CommitStarted` | queue/pending ownership update after `StartCandidate()` returns | mapped |
| `CommitFailedState` | failed queue/pending ownership update before result delivery | partial; the model coarsens several owner-local mutations into one action |
| `FailureResolveDirect` | matching `SubmitContext::result = FAILED` path | mapped |
| `FailureEnterCallback` / `FailureReturnFromCallback` | call and return boundary of contextless `WritePort::Finish(..., FAILED, info)` | partial; arbitrary callback behavior remains outside this model |
| `PublishFailureConfig` | `RequestConfig(in_isr)` after direct result delivery or `Finish()` returns | mapped |
| `ServiceTake` / `ServiceHandle` | later owner round applies CONFIG or one authoritative terminal snapshot | mapped |

`UartTxStartWindow.tla` isolates the interval in which hardware DMA has started and the
TX gate has been released, but C++ has not yet committed active software ownership.
CONFIG, COMPLETE, and ERROR may arrive in this interval, but they can only publish
service events; they cannot directly apply configuration or release active state.
After a failed software commit, the model explicitly follows direct result delivery or
the contextless `Finish()` callback and return before `RequestConfig()` publishes the
persistent CONFIG event. During that observable gap, the still-active service owner and
failure phase PC are the durable carrier. After publication, CONFIG has priority;
otherwise ERROR absorbs a coalesced COMPLETE. This is a focused one-round safety
composition, not a composition proof of the full queue, callback body, gate, generation,
and platform protocols.

## CONFIG Boundary

| TLA+ action | C++ location | Status |
| --- | --- | --- |
| publish latest CONFIG | backend `OnConfigRequested()` plus `ResumeConfig()` | partial |
| capture fixed metadata prefix | first `UartDmaTxModel::ApplyConfig()` attempt | mapped |
| quiesce hardware | backend `ApplyPendingConfig(in_isr)` | contract |
| stop active hardware | platform UART/DMA backend | contract |
| fail old pending/queued prefix | `ApplyConfig()` and `FailPublishedQueued()` | mapped |
| preserve post-boundary writes | fixed `config_prefix_count_` drain | mapped |
| republish WRITE after apply | unconditional `ServiceTx()` self-publication after successful `ApplyConfig()` | mapped |

`ApplyPendingConfig()` returning success must identify a real hardware quiescence
point. The abstract generation property is conditional on that platform contract.
`UartTxConfigDrain.tla` checks the four-record shape `active + pending + old queued +
callback-published new record`. It distinguishes pending metadata whose payload was
already removed, and verifies that the fixed prefix does not move while `Finish()`
callbacks append new work. That trace assumes the callback can acquire WritePort
producer ownership. If another producer is active, the callback may instead receive
`BUSY`; this model does not supply an automatic retry. The drain is a focused scenario,
not an arbitrary-length cutoff.

## CONFIG Abort Join

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| `RequestConfig` | `UartDmaTxModel::RequestConfig()` publishes the backend request before CONFIG | mapped |
| CONFIG claim retires old TX retry | `UartHardwareGate::TryEnterConfig()` clears `CONFIG_PENDING` and `TX_START_PENDING` in the successful same-word CAS after the model fixes its prefix | mapped |
| initialize both pending directions | `UartDmaAbortJoin::Begin()` publishes `LAUNCHING` and the complete pending mask before the first stop launch | mapped |
| `LaunchStop(d)` with stopped hardware | backend proves `EN == 0` and calls `CompleteStopped(d)` | mapped/contract |
| `ResolveAsyncStop(d)` with running hardware | backend finishes carrier MMIO, then `ArmAsyncStop(d)` publishes the armed bit | mapped/contract |
| `EndLaunch` | `EndLaunch()` changes `LAUNCHING` to `STOPPING`; only an empty pending/armed set may publish `QUIESCENT` | mapped |
| `StopIrqCheck(d)` with `EN=1` | `FinishAsyncStopIrq(d, false)` retains both pending and armed obligations | mapped/contract |
| `StopIrqCheck(d)` with `EN=0` | `FinishAsyncStopIrq(d, true)` clears both obligations and may publish `QUIESCENT` | mapped/contract |
| `ApplyConfig` inside the final stop IRQ | `DmaIRQHandler()` calls `ResumeConfig()` after its final readback and performs no later old-state access | mapped/contract |
| `ApplyConfig` | `UartDmaAbortJoin` reaches `QUIESCENT` only after `LAUNCHING` ends and both pending/armed sets are empty; backend register correspondence remains platform evidence | mapped/partial |
| `DrainAndRelease` | fixed-prefix drain, `OnConfigApplied()`, and unconditional WRITE rescan | mapped/partial |

`UartConfigAbortJoin.tla` independently enumerates stopped-before-launch, stop-between-
launch-and-arm, armed asynchronous completion, an EN-still-set IRQ retry, and both TX/RX
orders. Its negative configurations prove that early finalization, consuming a terminal
without an `EN=0` proof, retaining a retry for the drained old TX prefix, and omitting
the final WRITE rescan violate named invariants.
The stop-IRQ witness proves that applying CONFIG inside the final wrapper is reachable
and intentional after the wrapper's last old-state access. The model does not include
actual register definitions, NVIC routing, carrier generation, or DMA data movement.

`test_uart_dma_abort_join.cpp` executes the production atomic leaf for zero-direction,
sync/sync, both asynchronous orders, EN-still-enabled retry, mixed sync/async, completion
during launch, and concurrent two-direction completion. Backend tests must separately
pin stale status, readback/EN checks, and restore/remask scheduling.

## STM32 IRQ Scheduler And DMA Stop

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| `PubAtomicFetch(p)` | `irq_domain_state_.fetch_or(request | SCHEDULED, release)` | mapped |
| publisher kick | `kick_target(context)` only when the old word lacked `SCHEDULED` | mapped/contract |
| handler snapshot | `exchange(SCHEDULED, acq_rel)` | mapped |
| release/reacquire | release CAS from exactly `SCHEDULED` to zero; failure takes another exchange snapshot | mapped |
| old terminal with `EN=1` | armed DMA wrapper clears status, retains the join, and rearms carriers | mapped/contract |
| terminal with `EN=0` | wrapper may finish the join regardless of terminal generation | mapped/contract |
| wrapper remask | `DeferNormalIrq()` masks, marks deferred, then rechecks `AnyAsyncStopArmed()` and republishes RESTORE | mapped/contract |

`Stm32IrqScheduler.tla` checks the coalesced request scheduler independently of HAL.
`Stm32DmaAbortRestore.tla` composes one stale terminal wrapper, EN-authoritative stop,
early RESTORE consumption, a later wrapper remask, and the scheduler. The positive
models assume target-handler non-overlap and eventual BSP delivery of a scheduled kick.
They do not prove MMIO write completion, cache/shareability, shared-vector arbitration,
or that a specific Stream IP generates a fresh enabled terminal carrier after EN clear.

## Hardware Control

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| request CONFIG priority | `UartHardwareGate::RequestConfig()` | mapped |
| ordinary IRQ claim | `UartHardwareGate::TryEnterIrq()` | mapped |
| mask complete normal IRQ domain | platform backend before `MarkIrqDeferred()` | mapped in STM32, CH32, and ESP DMA backends; IRQ-controller ownership remains a BSP contract |
| publish deferred scan | `UartHardwareGate::MarkIrqDeferred()` | mapped |
| publisher self-dispatch | platform dispatch glue after `MarkIrqDeferred()` | mapped in STM32, CH32, and ESP DMA backends; target-context delivery remains a BSP contract |
| deferred claim and consume | `UartHardwareGate::TryEnterDeferredIrq()` | mapped |
| re-mask, barrier, authoritative status scan | platform backend | mapped in STM32, CH32, and ESP DMA backends; physical MMIO completion remains a platform contract |
| restore authoritative IRQ mask | platform backend | mapped in STM32, CH32, and ESP DMA backends; shared-vector claim arbitration remains a BSP contract |
| TX-start claim | `UartHardwareGate::TryEnterTxStart()` | mapped in STM32, CH32, and ESP DMA backends |
| bind stack-local IRQ ownership context | successful `TryEnterIrq(context)` / `TryEnterDeferredIrq(context)` followed by local context initialization | mapped in STM32, CH32, and ESP DMA backends |
| atomically admit nested TX start | successful CAS in `TryEnterNestedTxStart(context)`; the pre-CAS word has exact IRQ ownership and no CONFIG/deferred pending bit, and the desired word consumes `TX_START_PENDING` | mapped in STM32, CH32, and ESP DMA backends |
| publish blocked nested TX retry | failed priority admission followed by `fetch_or(TX_START_PENDING, release)` while the outer IRQ context remains active | mapped in STM32, CH32, and ESP DMA backends |
| leave nested TX start | `LeaveNestedTxStart(context)` restores local depth 2 to 1 without releasing the atomic owner | mapped in STM32, CH32, and ESP DMA backends |
| release context-carrying outer IRQ | `LeaveIrq(context)` requires depth 1, releases `IRQ_ACTIVE`, returns pending actions, and invalidates the context | mapped in STM32, CH32, and ESP DMA backends |
| `NestedStart` / `FallbackStart` | platform `StartDmaTx(...)` after the matching gate admission | contract; the nested-admission model assumes `STARTED`, excludes `FAILED`, and the backend contract forbids `RETRY` after admission |
| CONFIG claim and close | `TryEnterConfig()` / `LeaveConfig()` | mapped in STM32, CH32, and ESP DMA backends |
| prevent an old callback from surviving into a later CONFIG admission | backend quiescence and claim-before-inspect rules below | contract |

Formal results for this section are protocol results only and cannot by themselves be
reported as backend correctness. `UartHardwareGate` keeps `CONFIG_ACTIVE` across an
asynchronous transaction but does not encode abort-direction admission. The backend
must admit only the selected abort DMA IRQs, keep ordinary IRQ and TX-start entries
excluded, and call `LeaveConfig()` only after hardware apply and the fixed TX-prefix
drain.

### Nested TX Admission

`UartNestedTxAdmission.tla` starts after an IRQ invocation has both acquired the
hardware gate and synchronously won the `SerializedService` owner. The successful
nested-admission action is the abstract linearization point of the same-word CAS. If
CONFIG or deferred IRQ publication is ordered first, admission fails and the still-live
outer IRQ call stack publishes `TX_START_PENDING` before it may release. If the CAS is
ordered first, the start is already admitted and a later high-priority publication waits
for the outer IRQ owner.

After the model fixes the CONFIG prefix, `ConfigClaim` maps to the successful
`TryEnterConfig()` same-word CAS. It clears the pre-claim TX retry because that candidate
is now irreversibly owned by the destructive CONFIG drain. The model's `Cancelled` state
means committed-to-drain, not callback-complete. A retry published after the CAS remains
ordered after the claim and is reported on CONFIG leave.

The context depth is ordinary `uint32_t` state owned by that synchronous call stack.
Inner leave only restores depth; it neither clears `IRQ_ACTIVE` nor dispatches actions.
Only outer leave returns the CONFIG/deferred/TX snapshot. An existing retry is consumed
by the successful nested CAS, so a later release cannot spuriously start the same
candidate again.

The retry bit represents one candidate managed by the UART's `SerializedService`. It is
not a count and does not support two independent TX-start publishers: a second publisher
that sets an already-set bit before CONFIG claim has no distinct carrier. That scenario
is outside the production integration contract.

The model assumes the IRQ invocation won the TX service and therefore legitimately
passes its context into the executing handler. The context is not an event payload. If
another thread already owns the service, an IRQ publication must return and invalidate
its context at outer leave; the eventual thread handler uses the ordinary TX claim.
That service/context wiring is used by the STM32, CH32, and ESP DMA backends and covered
by deterministic composition tests. Its refinement remains `partial` because the model
does not include arbitrary callback bodies, physical MMIO, BSP IRQ delivery, or the
complete service-owner selection protocol. The older `UartTxControl.tla` deliberately
retains the more conservative behavior in which an IRQ-held gate causes RETRY; it does
not model the optimized nested path.

## RX SPSC And Clear

| TLA+ action | C++ location | Status |
| --- | --- | --- |
| producer push | RX ISR/DMA callback -> `queue_data_->PushBatch(...)` | partial |
| producer notification | `ReadPort::ProcessPendingReads(in_isr)` | partial |
| clear claim | CAS from `IDLE`/`EVENT` to `CLEARING` | mapped |
| fixed clear snapshot | `queued_size = queue_data_->Size()` | mapped |
| empty-snapshot release | direct `busy_.store(IDLE)` when `queued_size == 0` | mapped |
| drop snapshot prefix | `PopBatch(nullptr, queued_size)` | partial |
| observe release queue state | final `queue_data_->Size()` on the nonempty path only | mapped |
| publish `EVENT` or `IDLE` | final `busy_.store(...)` | mapped |

`UartRxSpsc.tla` assumes ordinary reads and `ClearQueuedData()` are one logical
consumer and do not overlap; the ordinary-read path has no owner CAS that would map a
multi-consumer guarantee. The ISR producer may push throughout CLEARING, and the model
proves that data after the fixed snapshot is not removed. The model has the C++ empty
snapshot branch, which stores `IDLE` without a second Size observation, and the separate
nonempty drop/observe/store branch. It reproduces both Size-to-store races: a later byte
can remain queued while the final state is `IDLE` rather than `EVENT`. This does not lose
data; the next ordinary read checks the queue again under the single-consumer contract.

## Progress

- `SerializedService` terminates for its finite publisher set under its stated weak
  fairness assumptions.
- `UartTxControlLiveness.cfg` uses a finite two-publication CONFIG actor and weakly fair
  internal continuations; it checks that the request budget is exhausted and every
  published CONFIG obligation settles without wrapping generation identity.
- `UartHardwareControlLiveness.cfg` checks already-started IRQ, scanner, CONFIG, and
  abort continuations; it does not force external IRQ or CONFIG publication.
- `UartNestedTxAdmissionLiveness.cfg` forces its finite CONFIG and deferred publishers,
  weakly fairly schedules every internal continuation, and requires the candidate to
  settle either by a successful nested/fallback start or by CONFIG cancellation.
- `UartTxPipelineLiveness.cfg` assumes finite publications, no backend start failure,
  fair retry dispatch, and eventual DMA terminal status.
- `UartTxStartWindow.cfg` is safety-only. It checks that a software-committed backend
  failure has a durable CONFIG carrier, not that the later platform CONFIG transaction
  must terminate.
- `UartTxConfigDrainLiveness.cfg` and `UartRxSpscLiveness.cfg` check progress only for
  already-started bounded transactions.
- `UartConfigAbortJoin.cfg` is safety-only. It enumerates launch, arm, EN-still-set
  retry, and stopped outcomes but does not assert that hardware must eventually clear
  EN or deliver another carrier IRQ.
- The STM32 scheduler/restore configurations are safety-only. Their correct cases do
  not establish unbounded fairness or physical IRQ delivery.

## Verification Boundaries

- TLC checks the finite states in each committed `.cfg`; it does not check C++ memory
  ordering or MMIO behavior. Expected-failure configurations pin one selected property
  signature; they do not establish that the injected fault violates only that property.
- GenMC checks the real C++ files included by each harness under the selected RC11
  bound; environment shims and unsupported library facilities remain outside scope.
- Deterministic C++ tests preserve known counterexample schedules but do not enumerate
  all schedules.
- ASAN, UBSAN, and TSAN are final dynamic evidence only.
- Platform builds prove compilation, linkage, atomic-shim availability, and warning
  cleanliness. They do not prove IRQ affinity, masking, barriers, or DMA quiescence.
