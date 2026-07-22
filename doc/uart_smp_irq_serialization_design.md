# UART SMP IRQ Serialization Policy

## Status

This document is the current design and implementation baseline for the UART
concurrency review. The abstract IRQ handoff protocol and its composition with the UART
business model have passed their positive TLC configurations and targeted negative
controls. ST, CH, and ESP backend source correspondence and build matrices have been
checked separately. Hardware behavior, vendor MMIO timing, cache coherency, and future
non-ESP SMP adapters remain platform proof limits.

The design deliberately separates the reusable UART service from the additional IRQ
admission required by a same-object SMP backend:

```text
single core or AMP owner core
  -> DirectPolicy
  -> SerializedService
  -> active/pending UART model

same-object SMP
  -> IrqSerializedPolicy in the backend
  -> the same SerializedService
  -> the same active/pending UART model
```

DirectPolicy does not bypass serialization. It only omits IRQ-domain masking and raw-IRQ
admission. The common UART model does not contain `PENDING_FILLING`, `PROMOTING`,
`TX_STARTING`, a Direct-only handoff protocol, or another fine-grained owner.

## Fixed decisions

1. The UART TX business model keeps the old publication order:

   ```text
   copy payload -> write pending length -> publish pending valid
   ```

2. One active DMA and one preloaded pending slot are retained.
3. RX byte delivery remains one ISR/DMA producer feeding the existing SPSC queue.
4. Every target publishes WRITE, COMPLETE, ERROR, and CONFIG to the same per-UART
   `SerializedService`.
5. Single-core targets and AMP targets use DirectPolicy: no extra IRQ mask or raw-IRQ
   admission is added around that service.
6. Only a backend that exposes one UART object to multiple cores adds the SMP IRQ policy.
7. The UART, TX-DMA, and any IRQ that can touch the protected hardware state are routed
   to one IRQ core and form one non-reentrant IRQ domain.
8. A normal caller may run on any core and may itself be an ISR.
9. The policy never disables global interrupts and never waits for another context.
10. All C++ atomic state is `uint32_t`.
11. CONFIG and runtime recovery keep their business phases, but their model and hardware
    transitions execute through the same SMP policy. They do not introduce a second
    arbitration owner.
12. DirectPolicy backends keep their existing vendor SDK IRQ/HAL entry path. They do not
    add an admission wrapper or replace `HAL_UART_IRQHandler()`, `HAL_DMA_IRQHandler()`,
    or an equivalent vendor handler.
13. IRQ admission before hardware-status access applies only to an IrqSerializedPolicy
    backend whose ISR entry is registered by LibXR or by the application.

## State

Every UART service uses one atomic arbitration word:

```text
bit 31       OWNER
bits 0..30   coalescible level-triggered events
```

Typical event bits are `WRITE`, `COMPLETE`, `ERROR`, `CONFIG`, and an asynchronous
CONFIG-stop completion. Payload and event counts are not stored in this word. WRITE is
rechecked from `WritePort`; CONFIG reads the latest stored configuration; one active DMA
makes COMPLETE coalescible.

DirectPolicy uses this word without touching IRQ enable state. IrqSerializedPolicy wraps
the same word with the platform IRQ-domain mask. The IRQ controller's enabled state is
hardware state, not another C++ atomic flag. There is no `RESTORING`, `IRQ_PENDING`,
Direct-only deferred-terminal state, or mask reference count.

## Required SMP backend operations

Only an IrqSerializedPolicy backend supplies the following operations:

```text
MaskIrqDomain()
SynchronizeIrqMask()
RestoreIrqDomain()
ServiceOwnedIrqSource()
```

`MaskIrqDomain()` is idempotent. After `SynchronizeIrqMask()` returns, a newly admitted
handler cannot read or acknowledge the protected hardware status before passing the
owner admission point. An IRQ that was already accepted may still execute, but the
LibXR/application-owned entry also masks first and then competes for the same owner.

`RestoreIrqDomain()` is also idempotent. It may race with a newer owner. This is allowed:
the newer owner already masked before claiming, and an IRQ admitted by a delayed restore
must mask and check the owner before touching the protected hardware state.

The protected interrupt sources must be level-sensitive, or otherwise remain pending
and be retriggered when restored, until the owned ISR acknowledges their hardware status.
An entry that loses owner admission does not read or clear the status.

DirectPolicy does not implement these operations. Its backend retains the existing SDK
handler; HAL callbacks publish facts into the same `SerializedService` used by Write and
CONFIG.

## Normal caller protocol

DirectPolicy performs only the `SerializedService::Invoke()` portion below. An
IrqSerializedPolicy caller first performs the IRQ-domain mask and synchronization:

Pseudocode:

```text
Invoke(event):
  MaskIrqDomain()
  SynchronizeIrqMask()

  old = state.fetch_or(event, release)

  if old contains OWNER:
    return HANDLED_BY_CURRENT_OWNER

  repeatedly CAS the observed event word to OWNER:
    success -> this caller owns the consumed event snapshot
    observes OWNER -> return HANDLED_BY_CURRENT_OWNER
    observes no remaining event -> return ALREADY_HANDLED

  Drain(snapshot)
```

For IrqSerializedPolicy, the mask happens before the event publication and before the owner decision. If the
caller observes an owner, that owner is responsible for the later restore. If the old
owner released first, the caller observes an owner-free word and can compete itself.

This ordering has one explicit progress assumption: a caller that has completed the
hardware mask must eventually execute its atomic publication/claim step. The protocol
does not lose work if that caller is preempted, but the UART IRQ domain remains masked
until it resumes. A backend that requires a hard mask-latency bound must protect this
short mask-to-RMW window with an appropriate local preemption rule and measure it; the
abstract protocol proves eventual progress under fairness, not a cycle bound.

CAS failure never means "the ISR exited". It only means the atomic word changed. The
caller reloads the returned value and either retries, hands off to the observed owner, or
detects that another owner already consumed the event.

## Direct callback and IrqSerializedPolicy-owned IRQ protocols

A DirectPolicy backend retains its vendor IRQ/HAL entry. After the vendor handler has
read and cleared its status, its callback invokes COMPLETE or ERROR on the same service.
If StartDmaTx is still executing under that owner, the callback only merges the event;
the existing owner finishes the STARTED/FAILED transition before processing it.

This protocol applies only where LibXR or the application already registers the ISR. It
does not require wrapping or replacing a vendor HAL handler used by a DirectPolicy
backend. The admission point precedes the first protected status read in that owned ISR:

```text
IrqEntry():
  MaskIrqDomain()
  SynchronizeIrqMask()

  try to CAS an owner-free state to OWNER, consuming any queued event snapshot

  if an OWNER is observed:
    return without reading or clearing hardware status

  ServiceOwnedIrqSource()
    -> read and clear the source status
    -> publish COMPLETE/ERROR/STOP_DONE into the already-owned state

  Drain(the consumed snapshot and callback events)
```

Mask-before-observe is mandatory. Masking only after observing another owner is unsafe:
the owner can release and restore between the observation and the late mask, leaving a
pending level IRQ permanently disabled with no remaining restorer.

An owned IRQ entry that loses does not need an `IRQ_PENDING` software bit. Its unchanged
hardware status is the durable pending fact. The owner it observed restores the domain
after release, and the hardware/controller admits the IRQ again.

## Owner drain and release

The owner handles snapshots with the UART business priority:

```text
CONFIG > ERROR > COMPLETE > WRITE/progress
```

Reentrant callbacks only OR another event into the state word. They do not recurse into
a second handler. This is also the complete DirectPolicy handoff for a terminal callback
that fires before `StartDmaTx()` returns; no `start_active`, `submit_active`, or
`deferred_terminal` state is added.

Release is:

```text
expected = OWNER

if CAS(OWNER -> 0, release/acquire) succeeds:
  RestoreIrqDomain()
  return

new events were published while OWNER remained set:
  exchange state to OWNER
  handle the new snapshot
  retry release
```

The CAS is the no-lost-wakeup boundary. A publication ordered before it makes the CAS
fail and remains with the owner. A publication ordered after it sees an owner-free word
and can claim ownership itself.

Restore intentionally occurs after the successful owner release. A newer caller may
mask and claim before an older owner performs its delayed restore. That does not permit
concurrent protected access: an IRQ admitted by the delayed restore masks first, sees the
new owner, and exits without acknowledging hardware. The new owner performs the final
restore when it releases. The cost is at most an extra rejected IRQ entry for that race,
not another software state.

## One concrete race

```text
owner A drains its last event
  -> CAS releases OWNER

caller B masks the IRQ domain
  -> publishes WRITE
  -> claims OWNER

old owner A is delayed, then restores the IRQ domain
  -> a pending IRQ enters
  -> owned IRQ entry masks first
  -> observes owner B
  -> returns without clearing status

owner B drains WRITE
  -> releases OWNER
  -> restores the IRQ domain
  -> pending IRQ re-enters and can become owner
```

No `RESTORING` bit is needed because the owned IRQ entry never performs protected work before
owner admission and never acknowledges a losing interrupt.

## CONFIG and ERROR placement

CONFIG remains a control transaction:

1. store the latest complete configuration;
2. publish `CONFIG` through the SMP policy;
3. fix the old queue prefix when CONFIG is admitted;
4. stop the old TX/RX generation synchronously or enter an asynchronous STOPPING phase;
5. release owner while waiting for a real stop-completion event;
6. apply the latest configuration after quiescence;
7. restart RX and rescan post-boundary writes.

The owner is not held while hardware completes an asynchronous stop. The persistent
CONFIG phase blocks new starts; any later caller or stop IRQ can acquire owner and resume
the transaction.

`StartDmaTx() == FAILED` remains record-local and does not request CONFIG. Runtime
DMA/UART ERROR follows the backend recovery contract and does not modify framing.

### Composition rules checked by the business model

The service consumes each event snapshot with the following rules:

```text
CONFIG > ERROR > COMPLETE > WRITE/progress
```

- CONFIG consumes the rest of the same old-generation snapshot. Synchronous stop enters
  the fixed-prefix drain immediately. Asynchronous stop enters `STOPPING`, releases
  OWNER, and resumes only after a real `STOP_DONE` carrier acquires OWNER.
- ERROR absorbs a simultaneous COMPLETE. The software active record is released once;
  the preloaded pending record is preserved for normal progress.
- Events seen while CONFIG is stopping may be consumed as reminders because the
  persistent phase and hardware stop join are authoritative. CONFIG completion always
  republishes WRITE so a consumed post-boundary notification cannot strand its record.
- The CONFIG boundary is the metadata queue length captured at CONFIG admission. The
  preloaded head owns payload outside the data queue and is drained by metadata only;
  each remaining old record consumes one metadata item and its matching payload. Records
  whose metadata appears after the boundary are not included.

The bounded composition model covers both synchronous and asynchronous stop, COMPLETE,
ERROR, coalesced ERROR|COMPLETE, CONFIG combined with a terminal snapshot, and a writer
interleaved at every publication step. Its positive safety and liveness configurations
explore `84,810` generated / `38,019` distinct states. Five fault configurations expose
the expected failures for wrong CONFIG priority, failure to absorb COMPLETE into ERROR,
a moving CONFIG boundary, omission of the mandatory WRITE rescan, and retaining OWNER
while waiting for asynchronous stop.

### Metadata-visible before `Submit()`

WritePort publishes in this order:

```text
payload -> arm Operation -> metadata -> call backend WriteFun/Submit
```

Consequently, a CONFIG completion rescan may consume and start a post-boundary record
after its metadata is visible but before that producer reaches `Submit()`. This is an
allowed asynchronous completion path, not a request-identity shortcut:

- the record's own Operation was armed before metadata publication;
- the owner removes that exact metadata record and completes it through `Finish()`;
- the later `Submit()` finds no synchronous candidate and returns `PENDING`;
- no `submission_id` is needed and the record is completed at most once.

The TLC early-consume witness confirms that this ordering is reachable. It does not
change WritePort producer admission: a rejected `Write()` still returns `BUSY` before
publishing any record or event.

## RX boundary

Ordinary `Read()` and `ClearQueuedData()` do not enter this SMP owner. RX data delivery
continues through SPSC. The IRQ policy is used only around IRQ/HAL/hardware lifecycle
work that shares state with TX start, CONFIG, or destructive recovery.

A backend may keep an independent RX data IRQ outside the TX domain when it cannot touch
the protected TX/configuration hardware state. CONFIG must still quiesce the RX DMA
lifecycle before changing descriptors or position state.

ESP GDMA treats transition-window RX bytes as discardable. `ApplyPendingConfig()` stops
and resets TX/RX GDMA, applies the UART payload, and leaves RX unarmed while the generic
model drains the fixed CONFIG TX prefix. `ReleaseConfigAdmission()` then remounts and
starts the RX descriptor ring immediately before opening the RX/CONFIG gate, so a
descriptor completed during CONFIG callback drain is not left waiting for an unrelated
future RX event.

## Platform contract

The SMP policy is valid only when the backend proves all of the following:

- all protected IRQs are routed to one core and cannot overlap each other;
- mask and restore can be invoked from every supported caller context and core;
- mask synchronization prevents a post-mask owned ISR from reading or acknowledging
  protected status before owner admission;
- losing IRQs do not acknowledge status and are retriggered after restore;
- a shared vector has a BSP-owned mask claim so one UART cannot restore a vector still
  masked by another device;
- the atomic word and protected non-atomic state reside in coherent shared memory;
- cache and DMA descriptor ownership follow the platform's explicit maintenance rules.

Architectural feasibility is not backend validation. ESP SMP and RP2xxx require concrete
mask/restore adapters. STM32 dual-core MCU families, CH32H417, and multicore HPM families
remain AMP owner/proxy targets unless a particular BSP intentionally exposes one object
to both cores.

## Concrete platform mapping

The policy and SDK-integration boundary is:

| Platform | Policy | Vendor SDK IRQ/HAL takeover |
|---|---|---|
| Single-core STM32, H5/U5, traditional H7 | DirectPolicy | None; keep CubeMX/HAL IRQ |
| STM32H745/H747/H755/H757 dual-core | AMP owner-core plus IPC | None |
| Single-core CH32 | DirectPolicy | None |
| CH32H417 | AMP owner-core plus IPC | None |
| Dual-core HPM families | AMP owner-core plus IPC | None |
| ESP32 | IrqSerializedPolicy | No SDK patch; use LibXR FIFO ISR |
| ESP32-S3 | IrqSerializedPolicy | No SDK patch; use LibXR GDMA ISR |
| ESP32-P4 | IrqSerializedPolicy | No SDK patch; GDMA route is statically selected |
| ESP32-H4/S31 preview targets | IrqSerializedPolicy | No SDK patch; backend build remains unverified |
| RP2040/RP2350 | IrqSerializedPolicy | No SDK patch; use application-registered ISR |
| Single-core ESP C3/C6 and similar targets | DirectPolicy | None |

This table classifies concurrency policy and IRQ integration. It does not claim that the
current circular RX data model supports H5/U5 GPDMA; linked-list RX remains a separate
data-model adapter.

### ESP-IDF SMP targets

The evidence must be split by IDF version and validation level:

| Target | IDF evidence | Current LibXR UART evidence |
|---|---|---|
| ESP32 | Dual-core supported target in IDF 5.5.2 | FIFO ISR entry inspected |
| ESP32-S3 | Dual-core, GDMA+UHCI supported target in IDF 5.5.2 | GDMA ISR entry inspected; prior object build passed |
| ESP32-P4 | Dual-core, GDMA+UHCI supported target in IDF 5.5.2 | GDMA route inspected; current full build is blocked by unrelated compatibility failures |
| ESP32-H4 | Dual-core preview target in IDF 5.5.2 | 5.5.2 selects FIFO because UHCI is absent; no LibXR build |
| ESP32-S31 | Dual-core GDMA+UHCI preview target in IDF master `055ba9d3` | Absent from local IDF 5.5.2; no LibXR build |

IDF master `055ba9d3` also gives ESP32-H4 GDMA+UHCI, so a future master-based LibXR
build would select the GDMA route instead of the 5.5.2 FIFO route. This version drift
does not change the admission algorithm: both routes register a LibXR-owned entry.

For external interrupt sources, IDF documents that `esp_intr_disable()` and
`esp_intr_enable()` may be called from a core other than the allocation core. The 5.5.2
implementation disconnects and reconnects the source in the interrupt matrix while
holding its cross-core allocator spinlock. Therefore the admission mechanism is
architecturally available on all five dual-core targets above. Only ESP32 and ESP32-S3
currently have direct LibXR source/build evidence; the other rows are not backend
validation claims.

LibXR already owns the relevant ESP entries: the GDMA path registers `DmaTxIsrEntry` and
`DmaRxIsrEntry` with `esp_intr_alloc_intrstatus*()`, while the FIFO path registers
`UartIsrEntry` with `esp_intr_alloc()`. Admission can therefore be inserted before their
first status read without replacing an IDF handler.

The current GDMA and FIFO allocations are non-shared:
`ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED`. They must be created from a pinned
initialization task on SMP targets so UART, TX-DMA, and RX-DMA interrupt entries land on
one core. Separate TX/RX sources may use separate same-level interrupt lines on that
core; a hardware-shared source uses one LibXR entry to service both.

IDF 5.5.2 normally invokes a non-shared ISR without the allocator spinlock. When
`CONFIG_APPTRACE_SV_ENABLE` is enabled, its tracing wrapper also holds that lock, so this
configuration is rejected by the ESP UART header while the backend uses
`esp_intr_disable()` from ISR admission. Audited IDF master uses `CONFIG_ESP_TRACE_ENABLE`
for the same wrapper and installs the handler directly when `CONFIG_ESP_TRACE_LIB_NONE`
is selected. No IDF source patch is required.

Single-HP-core ESP targets use DirectPolicy. SMP ESP FIFO uses the same admission policy
at its LibXR-owned `UartIsrEntry`, but keeps its separate FIFO TX business model.

### RP2040 and RP2350

The Pico SDK states that NVIC enable APIs affect only the executing core, so an arbitrary
caller cannot disable the fixed IRQ core's NVIC through `irq_set_enabled()`. The adapter
instead places admission in the application-registered handler and masks the UART/DMA
peripheral sources:

- DMA channel IRQ enables use `hw_set_bits()`/`hw_clear_bits()` on the channel bit in
  `INTE`;
- UART interrupt masks can use the RP atomic MMIO alias for `UARTIMSC`;
- the shared DMA vector remains enabled, so unrelated DMA channels still dispatch.

RP-series atomic MMIO aliases are documented as cross-core atomic set/clear operations.
This makes the IRQ-domain mask/restore portion feasible on both RP2040 and RP2350.
RP2040 still needs its existing cross-core hardware-spinlock implementation for C++
`atomic<uint32_t>`; RP2350 can use processor atomics.

### AMP and single-core targets

STM32 dual-core MCU families, CH32H417, and the audited multicore HPM families use
separate core images and an owner/proxy contract. They do not share one C++ UART object
and therefore use DirectPolicy on the owner core. Ordinary single-core STM32, CH32,
MSPM0, HPM, and single-core ESP targets also use DirectPolicy.

These DirectPolicy backends keep their existing CubeMX/vendor IRQ functions. They do not
wrap or replace vendor HAL handlers for this policy.

## Verification plan

The independent model must cover:

1. an IRQ accepted before another core finishes masking;
2. two concurrent normal publishers;
3. publication against owner release;
4. a delayed old-owner restore after a new owner has claimed;
5. a losing IRQ leaving status pending and retriggering;
6. CONFIG/ERROR priority and asynchronous continuation;
7. shared-vector mask ownership as a separate BSP model.

Required negative controls are:

- publish before mask;
- clear/ack IRQ status before owner admission;
- unconditional owner release after a stale empty check;
- losing IRQ clears status or fails to remain retriggerable.

TLA+/TLC establishes only the abstract protocol. GenMC must then check the concrete
`uint32_t` C++ atomic operations and memory orders. Deterministic C++ tests, TSAN/ASAN,
real platform `-Werror` builds, and hardware timing/IRQ tests remain separate evidence
levels.

Current retained evidence is under
`runs/uart_irq_mask_handoff_audit_20260721/`: the IRQ handoff and UART composition TLC
suite has `14/14` expected outcomes, and the GenMC v0.17.0 RC11 admission harness has
`4/4`. These results validate the design interface, not the current unmodified backend
implementation.
