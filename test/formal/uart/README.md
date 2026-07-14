# UART Formal Models

This directory contains bounded TLA+ models for the UART concurrency work. The
runner's case table is an exact allowlist: adding, removing, or forgetting to register
any `.cfg` file fails before TLC starts.

## Pinned Toolchains

### TLA+ / TLC

- TLA+ release: `v1.7.4`
- TLC version: `2.19`
- Download:
  `https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar`
- `tla2tools.jar` SHA-256:
  `936A262061C914694DFD669A543BE24573C45D5AA0FF20A8B96B23D01E050E88`

Java must be available as `java`. The scripts download the pinned jar into a user
cache when `TLA2TOOLS_JAR` is not set, and always verify its SHA-256 before running.

### GenMC

- GenMC version: `v0.17.0`
- Container image:
  `genmc/genmc@sha256:f9a74c13505b07a0cfcc96d5c46813d975543a081d56449cd529ba2a2f9d4791`
- Binary inside the image: `/usr/local/bin/genmc/genmc`

Docker must be available. The runners pull the exact digest when it is absent, verify
the local image digest and binary version, mount the repository read-only, and check
all harnesses under the RC11 memory model.

## Verification Set

### Model cutoffs and contracts

| Model | Finite cutoff | Assumptions and verified scope |
| --- | --- | --- |
| `SerializedService` | 3 one-shot publishers, 2 event bits, thread/ISR contexts | Checks unique ownership, durable coalesced events, owner-context callbacks, accounting, and termination under per-publisher weak fairness. It is not a cutoff proof for arbitrary publishers and carries no event payload/count. |
| `UartTxControl` | 3 one-shot writers, at most 2 CONFIG publications and DMA generations | Checks the coarse queue/service/hardware-gate control protocol, durable retry/deferred carriers, fixed CONFIG boundary, and writer publication during an owner round. The main configuration is safety-only. The focused CONFIG liveness configuration makes the finite request actor and internal CONFIG continuations weakly fair, so the request budget is exhausted and every published CONFIG obligation settles without wrapping generation identity. The model does not contain the active/pending buffer-copy pipeline. |
| `UartHardwareControl` | 2 ordered IRQ publishers on one logical IRQ core, at most 2 CONFIG generations | Checks scanner/CONFIG exclusion, mask-before-scan, abort admission, and ghost-generation status identity. The correct environment remasks before scanning, forbids late retired-generation status, and establishes `configQuiesced` only after a selected abort callback has settled. The liveness config weakly fairly schedules internal IRQ/scanner/CONFIG/abort continuations; requests and hardware-status arrivals remain environment choices. |
| `UartTxStartWindow` | 1 TX service round, 1 TX gate owner, 1 backend result, at most 1 external publication of each event kind plus 1 mandatory failure-CONFIG publication | Checks the focused interval from DMA start through hardware-gate release to software commit. CONFIG, COMPLETE, and ERROR paths may publish after gate release but before commit; CONFIG may also be published while a failed result is delivered. Only the service owner may mutate TX state. ERROR absorbs a coalesced COMPLETE. A backend failure explicitly passes through software commit, direct result delivery or a `Finish()` callback/return, and failure-CONFIG publication. The service owner and failure phase PC carry the continuation until CONFIG becomes persistent. This is a safety-only composition model; it does not include the full queue, retry, buffer-copy, generation, or platform protocol. |
| `UartTxPipeline` | 3 records, 2 DMA buffer blocks, one logical WritePort producer | Splits payload, metadata, WRITE, private copy, pending publication, DMA start, retry/failure, commit, and terminal actions. Safety uses record symmetry. Liveness uses no symmetry, assumes no backend `FAILED` result, weak fairness for protocol continuations/terminal publication, and strong fairness for a successful backend start. CONFIG, the hardware gate, DMA generations, callback bodies, and BLOCK waiter state are separate obligations. |
| `UartRxSpsc` | `MaxItems = 3`, one ISR producer, one one-byte reader, one clear operation | Assumes successful pushes and the ReadPort single-logical-consumer contract. Checks SPSC order, single consumer ownership, fixed clear snapshot, and post-snapshot preservation. An empty initial snapshot releases directly to `IDLE`; only a nonempty snapshot performs a post-drop Size observation before release. It does not cover queue-full policy, the complete ReadOperation lifecycle, or concurrent front-end consumers. Liveness uses no symmetry and weakly fairly schedules only already-started producer/read/clear continuations; the environment is not forced to initiate them. |
| `UartTxConfigDrain` | 4 records: active, pending, old queued, and one callback-published record | Assumes hardware is already quiesced. Checks that active is released without another Finish, pending consumes metadata only, ordinary queued work consumes metadata+payload, each operation finishes at most once, and only the first-attempt metadata prefix is failed. Safety uses record symmetry; liveness uses no symmetry and weak fairness for the finite drain continuation. |

The TX pipeline models retry and backend-failure accounting. `UartTxStartWindow`
checks the focused start/gate-release/commit composition, while CONFIG prefix drain and
stale DMA/abort generations are modeled separately by `UartTxConfigDrain` and
`UartHardwareControl`. Passing all four does not by itself prove their C++ or platform
composition.

### Successful configurations

The following configurations must complete with exit code zero:

- `SerializedServiceCorrect.cfg` against `SerializedService.tla`
- `UartTxControl.cfg` against `UartTxControl.tla`
- `UartTxControlLiveness.cfg` against `UartTxControl.tla`
- `UartHardwareControl.cfg` against `UartHardwareControl.tla`
- `UartHardwareControlLiveness.cfg` against `UartHardwareControl.tla`
- `UartTxStartWindow.cfg` against `UartTxStartWindow.tla`
- `UartTxPipeline.cfg` against `UartTxPipeline.tla`
- `UartTxPipelineLiveness.cfg` against `UartTxPipeline.tla`
- `UartRxSpsc.cfg` against `UartRxSpsc.tla`
- `UartRxSpscLiveness.cfg` against `UartRxSpsc.tla`
- `UartTxConfigDrain.cfg` against `UartTxConfigDrain.tla`
- `UartTxConfigDrainLiveness.cfg` against `UartTxConfigDrain.tla`

The dedicated liveness configurations intentionally omit `SYMMETRY`. Their temporal
claims are conditional on the explicit fairness and environment assumptions summarized
above. `UartTxControlLiveness.cfg` intentionally forces its finite CONFIG publisher to
exhaust the two-publication budget; the other models do not assert that an unconstrained
external producer, request, IRQ, or DMA event must occur.

### Expected counterexamples

Every invariant counterexample below must exit with TLC 2.19 status `12`. The action
property counterexamples `UartTxControlMovingBoundary.cfg`,
`UartTxPipelineWrongCompletion.cfg`, and `UartTxPipelineOwnerPublication.cfg` must exit
with status `13`. Each focused configuration asks TLC to check one targeted property and
must emit only that configured signature plus the concrete-behavior marker. This pins a
specific counterexample; it does not claim that other, unconfigured properties would
remain true under the same injected fault.

- `SerializedServiceBroken.cfg`:
  `Invariant NoLostEvents is violated`
- `UartTxControlEphemeralRetry.cfg`:
  `Invariant TxWorkHasCarrier is violated`
- `UartTxControlMissingSelfDispatch.cfg`:
  `Invariant DeferredWorkHasDispatcher is violated`
- `UartTxControlBoundSubmitContext.cfg`:
  `Invariant TxWorkHasCarrier is violated`
- `UartTxControlMovingBoundary.cfg`:
  `Action property ConfigBoundaryDoesNotMove is violated`
- `UartTxControlSubmitAfterTake.cfg`:
  `Invariant NoSubmitAfterTakeWitness is violated`
- `UartTxControlOwnerRetake.cfg`:
  `Invariant NoOwnerRetakeWitness is violated`
- `UartHardwareControlNoRemask.cfg`:
  `Invariant ScanRequiresMask is violated`
- `UartHardwareControlLateStatus.cfg`:
  `Invariant StatusMatchesActiveGeneration is violated`
- `UartHardwareControlEarlyQuiesce.cfg`:
  `Invariant QuiescenceRequiresSettledAbort is violated`
- `UartHardwareControlAbortABA.cfg`:
  `Invariant OldAbortCannotEnterNewAdmission is violated`
- `UartTxStartWindowEarlyMutation.cfg`:
  `Invariant ExternalPathsArePublishOnly is violated`
- `UartTxStartWindowDoubleTerminal.cfg`:
  `Invariant ErrorAbsorbsComplete is violated`
- `UartTxStartWindowMissingFailureConfig.cfg`:
  `Invariant FailedHasConfigCarrier is violated`
- `UartTxStartWindowStartedPreCommitWitness.cfg`:
  `Invariant NoStartedPreCommitWindowWitness is violated`
- `UartTxStartWindowFailedConfigPreCommitWitness.cfg`:
  `Invariant NoFailedConfigPreCommitWindowWitness is violated`
- `UartTxStartWindowFailedCallbackPreConfigWitness.cfg`:
  `Invariant NoFailedCallbackPreConfigWindowWitness is violated`
- `UartTxPipelineEarlyPromote.cfg`:
  `Invariant PromotedPendingWasPublished is violated`
- `UartTxPipelineTerminalDoubleFinish.cfg`:
  `Invariant FinishAtMostOnce is violated`
- `UartTxPipelineRetryConsume.cfg`:
  `Invariant RetryKeepsRecordOwned is violated`
- `UartTxPipelineWrongCompletion.cfg`:
  `Action property CompletionIdentityMatchesActiveRecord is violated`
- `UartTxPipelineOwnerPublication.cfg`:
  `Action property NoWriterPublicationWhileOwnerWitness is violated`
- `UartRxSpscMovingBoundary.cfg`:
  `Invariant NoPostSnapshotLoss is violated`
- `UartRxSpscStaleReleaseEvent.cfg`:
  `Invariant PostSnapshotDataReturnsEvent is violated`
- `UartTxConfigDrainMovingBoundary.cfg`:
  `Invariant PostBoundarySurvives is violated`
- `UartTxConfigDrainPendingPayload.cfg`:
  `Invariant MetaDataAligned is violated`

The case table is also the exact `.cfg` allowlist. Before starting TLC, each runner
compares that table with every `.cfg` in this directory. An unregistered new file, a
missing file, a duplicate entry, an unexpected exit status, a different counterexample,
a parser/semantic/internal error, or a tool-version mismatch fails the runner.

The two `UartTxControl` `Witness` configurations and
`UartTxPipelineOwnerPublication.cfg` are expected-failing reachability checks, not
broken protocol variants. The control witnesses require concrete traces in which a
writer publishes after an existing owner has taken a snapshot and that owner later
takes the coalesced WRITE event. The pipeline witness independently requires payload or
metadata publication while its service owner is active. These checks prevent a guard
regression from silently excluding the interleavings used by the safety properties.

The three `UartTxStartWindow` witness configurations pin the hardware-start and failed
completion boundaries. They independently require a `Started + gate released + not
committed` trace, a `Failed + gate released + external CONFIG published + not committed`
trace, and a `Failed + software committed + Finish callback active + failure CONFIG not
yet published` trace. Separate configurations are necessary because the latter two are
alternative schedules. They keep future guard changes from silently removing any
central window.

`UartHardwareControlEarlyQuiesce.cfg` independently demonstrates that CONFIG cannot
publish its quiescence join while a selected abort callback can still enter. The
historical `UartHardwareControlAbortABA.cfg` is a compound invalid backend: it permits
both that early quiescence and a later early close so TLC can retain the old-admission
ABA counterexample. The correct configuration blocks at the earlier quiescence join;
generation values remain specification-only history rather than a required runtime
epoch.

`UartRxSpscStaleReleaseEvent.cfg` is different from the injected protocol faults. It
captures a real boundary in the current ReadPort implementation: an empty initial Size
observation releases directly to `IDLE`, while a nonempty snapshot has a separate final
Size observation and release store. Data pushed after either relevant observation is
preserved, but the release may leave the non-empty queue in `IDLE` rather than `EVENT`.
The expected counterexample documents that behavior; `PostSnapshotDataReturnsEvent` is
not a property claimed by the correct current implementation.

### GenMC verification set

The GenMC runner checks these explicit cases:

1. `common/atomic_frontend_probe.cpp` under RC11 must pass.
2. The same probe with `-DBROKEN_MEMORY_ORDER=1` must fail with
   `Safety violation`.
3. `genmc/serialized_service.cpp --check-liveness` must pass. The explored execution
   count is diagnostic and is not pinned by the runner.
4. `genmc/uart_hardware_gate.cpp` safety checking must pass. Blocked executions are
   allowed in this mode.
5. The same gate harness with `--check-liveness` must pass. This checks the bounded
   harness after owner release was made a single non-retrying atomic operation.

The gate harness is compiled with `-fno-threadsafe-statics`,
`-D_BITS_PTHREADTYPES_COMMON_H=1`, and the required libxr include paths.

GenMC is intentionally limited to the atomic frontend probe, `SerializedService`, and
the hardware-gate leaf harness. It does not verify the full TX model or the real
`WritePort` closure. A full TX harness was rejected because GenMC v0.17.0 reaches an
internal failure on dynamic non-atomic access. CBMC 6.10 was also rejected because its
derived g++ frontend cannot parse the libstdc++ atomic probe used by this codebase.

## Run

From the repository root on Windows PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File test/formal/uart/run_tlc.ps1
```

From a POSIX shell:

```sh
bash test/formal/uart/run_tlc.sh
```

Run the GenMC checks from the repository root with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File test/formal/uart/run_genmc.ps1
```

```sh
bash test/formal/uart/run_genmc.sh
```

Optional overrides:

```powershell
$env:TLA2TOOLS_JAR = "C:\tools\tla2tools-v1.7.4.jar"
test/formal/uart/run_tlc.ps1 -Workers 4
```

```sh
TLA2TOOLS_JAR=/opt/tla/tla2tools-v1.7.4.jar \
TLC_WORKERS=4 bash test/formal/uart/run_tlc.sh
```

TLC metadata is written under a unique temporary directory and removed when the
runner exits. No `states/` directory should be left in the worktree.

These are finite exhaustive checks under each model's stated cutoff and contracts.
They do not by themselves prove target MMIO sequences, DMA quiescence, cache
coherency, or all target-specific progress properties. The GenMC probes cover the
selected C++ atomic protocols under RC11; the TLA+ models cover the higher-level
finite control protocols.
