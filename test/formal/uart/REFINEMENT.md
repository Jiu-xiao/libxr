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
| `Claim(p)` | successful owner-bit `compare_exchange_weak(..., acquire)` | mapped |
| `Take(p)` | `state_.exchange(OWNER_BIT, acq_rel)` | mapped |
| `Handle(p)` | `handler(snapshot)` in the winning invocation's context | mapped |
| `ReleaseRetry(p)` | failed final `compare_exchange_strong`, with event bits returned in `expected` | mapped |
| `ReleaseEmpty(p)` | successful `compare_exchange_strong(OWNER_BIT, 0, release, acquire)` | mapped |

The TLA+ module checks a finite three-publisher protocol. The GenMC harness executes
the real header under RC11 with three pthread publishers and one handler-reentrant
publication. Neither result is a cutoff proof for an arbitrary number of publishers.

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
| choose queue head | `UartDmaTxModel::StartQueuedActive()` | partial |
| retain RETRY record | `StartQueuedActive()` / `PromoteAndStartPending()` on `RETRY` | mapped |
| advance an older RETRY head from a later submitter | submission-id match selects the synchronous `SubmitContext`; a mismatch progresses contextlessly and completes through the queued operation | mapped |
| publish durable TX retry | `UartHardwareGate::TryEnterTxStart()` sets `TX_START_PENDING` before claiming | mapped in model class only |
| start active DMA | backend `StartDmaTx(...)` | partial |
| stage pending buffer | `UartDmaTxModel::StageNextPending()` | partial |
| release active terminal | `UartDmaTxModel::ReleaseActive()` | mapped |
| complete queued operation | `WritePort::Finish(...)` | mapped |

`SubmitContext::submission_id` restricts only which synchronous caller may receive a
direct return value. It no longer restricts state-machine progression: a later caller
may start an older retained head, whose result is delivered through that record's own
queued operation. That `Finish()` may execute while the later caller still owns the
WritePort producer lock, so its callback must handle `BUSY` as described above.
`UartTxPipeline.tla` separately represents active/pending ownership,
private copy before pending publication, RETRY preservation, the backend-start/software-
commit window, and an abstract per-record finish count. Its three-record cutoff is not
a proof for arbitrary queue length, and the pipeline, combined control, CONFIG-drain,
and hardware models have not been proved as one refinement composition. The TX
refinement therefore remains `partial`.

## TX Pipeline

| TLA+ action | C++ location | Status |
| --- | --- | --- |
| `WriteCopyPayload` | `queue_data_->PushBatch(...)` and pre-metadata `MarkAsRunning()` | partial |
| `WritePublishMetadata` | `queue_info_->Push(WriteInfoBlock)` | partial |
| `WriteRaise` | `UartDmaTxModel::Submit()` | mapped |
| active copy | `PeekBatch(buffers_.ActiveBuffer(), ...)` | partial |
| pending copy and private completion | `StagePending()` payload copy/pop | partial |
| `PublishPending` | `pending_valid_ = true` | mapped |
| `BackendRetry` | `StartDmaTx(...) == RETRY` with ownership retained | mapped |
| `BackendStarted` | platform DMA enable before software commits queue ownership | contract |
| `CommitStarted` | queue pop, pending flip if needed, and direct result/`Finish()` | partial; the action property binds the new active record to the sole finish-count delta |
| terminal releases active without resolving its operation again | `ReleaseActive()` | mapped |

The finish counter proves the abstract queue record is resolved at most once. The
`CompletionIdentityMatchesActiveRecord` action property additionally checks that an
active-ownership commit resolves that same record rather than another queued operation.
It does not prove the complete `WritePort` BLOCK timeout/semaphore protocol or arbitrary
user callback behavior.

## TX Start/Commit Window

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| `ServiceClaim` | current `SerializedService` owner entering TX progression | mapped |
| `TxGateClaim` / `TxGateRelease` | backend `TryEnterTxStart()` / `LeaveTxStart()` | mapped in model class only |
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
| republish WRITE after apply | `ServiceTx()` after successful `ApplyConfig()` | mapped |

`ApplyPendingConfig()` returning success must identify a real hardware quiescence
point. The abstract generation property is conditional on that platform contract.
`UartTxConfigDrain.tla` checks the four-record shape `active + pending + old queued +
callback-published new record`. It distinguishes pending metadata whose payload was
already removed, and verifies that the fixed prefix does not move while `Finish()`
callbacks append new work. That trace assumes the callback can acquire WritePort
producer ownership. If another producer is active, the callback may instead receive
`BUSY`; this model does not supply an automatic retry. The drain is a focused scenario,
not an arbitrary-length cutoff.

## Hardware Control

| TLA+ action | C++ or platform location | Status |
| --- | --- | --- |
| request CONFIG priority | `UartHardwareGate::RequestConfig()` | mapped |
| ordinary IRQ claim | `UartHardwareGate::TryEnterIrq()` | mapped |
| mask complete normal IRQ domain | platform backend before `MarkIrqDeferred()` | open |
| publish deferred scan | `UartHardwareGate::MarkIrqDeferred()` | mapped |
| publisher self-dispatch | platform dispatch glue after `MarkIrqDeferred()` | open |
| deferred claim and consume | `UartHardwareGate::TryEnterDeferredIrq()` | mapped |
| re-mask, barrier, authoritative status scan | platform backend | open |
| restore authoritative IRQ mask | platform backend | open |
| TX-start claim | `UartHardwareGate::TryEnterTxStart()` | mapped in model class only |
| CONFIG claim and close | `TryEnterConfig()` / `TryLeaveConfig()` | mapped in model class only |
| abort callback admission within one CONFIG transaction | `OpenConfigIrqAdmission()` / `TryEnterConfigIrq()` | mapped in model class only |
| prevent an old callback from surviving into a later CONFIG admission | backend quiescence and claim-before-inspect rules below | contract |

STM32, CH32, and ESP backends have not been migrated to `UartHardwareGate`. Formal
results for this section are protocol results only and cannot be reported as backend
correctness.

The gate's admission bit and close CAS serialize callback entry against the current
CONFIG owner, but they do not identify the CONFIG generation. The TLA+ model now treats
`configQuiesced` as the join point and checks that a selected abort callback has already
settled before that state is published. Establishing the join is still a backend
contract; `OldAbortCannotEnterNewAdmission` therefore depends on these rules:

- an IRQ must claim the hardware gate before reading status, selecting a callback, or
  retaining COMPLETE, ERROR, or abort meaning;
- `ApplyPendingConfig()` may return true only after mask/barrier, stop, and status clear
  have made the old DMA generation quiescent;
- at most one abort transaction is outstanding, and an entered abort callback must
  leave the CONFIG IRQ sub-owner before resuming CONFIG;
- CONFIG admission cannot close and reopen while an already selected old abort callback
  can still enter; a late empty vector must re-read current status and do nothing.

Under this contract, the generation fields in `UartHardwareControl.tla` are ghost
history used to state the property, not runtime state that must be added to
`UartHardwareGate`. A backend that queues already-selected callbacks outside the gate
and cannot drain or cancel them needs an immutable generation token carried by the
callback instead.

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
- `UartTxPipelineLiveness.cfg` assumes finite publications, no backend start failure,
  fair retry dispatch, and eventual DMA terminal status.
- `UartTxStartWindow.cfg` is safety-only. It checks that a software-committed backend
  failure has a durable CONFIG carrier, not that the later platform CONFIG transaction
  must terminate.
- `UartTxConfigDrainLiveness.cfg` and `UartRxSpscLiveness.cfg` check progress only for
  already-started bounded transactions.

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
