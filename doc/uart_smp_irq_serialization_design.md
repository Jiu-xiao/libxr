# UART DMA Concurrency and SMP IRQ Serialization

## Status and scope

This document defines the current concurrency contract implemented by
[`UartDmaModel`](../src/driver/model/uart_dma_model.hpp),
[`UartDirectPolicy`](../src/driver/model/uart_execution_policy.hpp), and
[`UartIrqSerializedPolicy`](../src/driver/model/uart_execution_policy.hpp). It covers
the STM32, CH32, and ESP AHB-GDMA UART backends in this tree.

The design has one per-UART `SerializedService`. Direct and SMP policies differ only in
how they admit hardware IRQ work; they do not select different TX/CONFIG state machines:

```text
WRITE / COMPLETE / ERROR / CONFIG / stop completion
                         |
                         v
          DirectPolicy or IrqSerializedPolicy
                         |
                         v
               one SerializedService
                         |
                         v
                    UartDmaModel
```

This is not a hardware-acceptance report. Source review, deterministic tests, formal
models, sanitizer runs, and cross-toolchain builds each cover a different layer. None of
them proves interrupt latency, cache/DMA coherency, vendor MMIO behavior under load, or
end-to-end operation on a board.

The current scope deliberately excludes:

- an ESP FIFO UART class, an `ESP32UART` compatibility alias, and a runtime DMA/FIFO
  switch;
- STM32 H5/U5/U3/N6/H7RS linked-list GPDMA RX;
- a same-object SMP adapter for STM32 or CH32;
- an external scheduler, work queue, owner thread, or bounded `max_rounds` policy.

## Vocabulary

- **service owner**: the caller that wins the single `SerializedService` owner bit and
  drains coalesced UART events in its current call stack;
- **active**: the retained TX buffer whose record has successfully started in hardware;
- **pending**: one payload copied into the other local TX buffer but not yet promoted;
- **public record**: metadata and payload still owned by `WritePort` queues;
- **control transaction**: CONFIG or runtime ERROR recovery;
- **hardware quiescence**: the backend has stopped the old TX/RX generation, classified
  its final TX terminal state, completed destructive cleanup, and guarantees that no old
  terminal callback can appear later;
- **IRQ admission**: acquiring the same service owner before an SMP raw ISR reads or
  acknowledges protected hardware status.

The owner is an execution right, not a CPU affinity. Whoever acquires it runs the
service locally. There is no migration to an IRQ core.

## One serialized owner

`SerializedService` stores one owner bit and coalescible event bits in one atomic
`uint32_t`:

```text
bit 31       OWNER
bits 0..30   level-triggered event facts
```

For `UartDmaModel`, the event facts are `WRITE`, `COMPLETE`, `ERROR`, `CONFIG`,
`STOP_DONE`, and the internal `CONTROL_READY`. They do not carry counts or payloads.
Durable state lives in the WritePort queues, the retained active/pending buffers, the
configuration payload, and the control phase.

There is no second owner, generation counter, or external completion owner. Old-terminal
identity comes from hardware quiescence plus the typed terminal result before a retained
buffer is reused.

A caller that observes an owner only merges its event and returns. The current owner
drains a snapshot, then uses a no-new-event CAS to release. Publication before that CAS
makes the release fail and remains with the current owner; publication after it sees an
owner-free word and may become the next owner. Reentrant HAL callbacks therefore do not
recurse into the UART state machine.

The service is non-waiting but not time-bounded. It does not spin waiting for another
context, sleep, or require a scheduler, but an owner can keep draining while publishers
continue to add work. Any backend that allows an ISR to acquire the owner must make all
reachable work ISR-safe and non-blocking.

## TX record lifecycle

The common TX path is:

```text
WritePort public queues
  -> copy payload into pending buffer
  -> publish pending_valid
  -> promote pending to active
  -> publish complete active state
  -> StartDmaTx(active)
  -> finish the Write Operation with STARTED or FAILED
  -> later COMPLETE releases the retained active buffer
```

The Write Operation reports the result of starting the record. It is not the physical
wire-completion notification. A later whole-transfer `COMPLETE` only retires the active
buffer and allows the next pending record to start.

Only the service owner may stage payload, promote pending, start hardware, pop metadata,
or finish a Write Operation. The payload is removed from the public byte queue while its
metadata remains at the metadata-queue head. A successful start or record-local start
failure consumes that exact metadata record. `queue_info_->Size() == 1` identifies the
only synchronous candidate; no `submission_id` is needed.

`StartDmaTx() == FAILED` guarantees that no transfer and no later terminal callback exist
for that attempt. The model finishes that record as failed and may proceed to the next
one. It does not start CONFIG or runtime recovery.

The model publishes complete active state before calling `StartDmaTx()`. If the backend
raises a synchronous callback from inside that call, the callback only merges an event.
The current owner completes the STARTED/FAILED transition before the next snapshot can
consume it.

## Event order and terminal retirement

Every service snapshot first performs an authoritative `COMPLETE` retirement prepass.
Only after that prepass does the normal business priority apply:

```text
COMPLETE retirement prepass
then CONFIG > ERROR > WRITE/progress
```

This order is intentional. If an old transfer actually completed before CONFIG or ERROR
recovery won the control boundary, it must be retired and must not be retransmitted.
During an active control phase, only control carriers advance that phase; ordinary WRITE
cannot stage or start another public record.

A backend must publish `COMPLETE` only for an authoritative whole-transfer completion.
A partial DMA prefix is not completion. When terminal sources are split across IRQs, the
backend must collect the old TX terminal fact after quiescence and before clearing or
reusing hardware state. Serializing source handlers alone does not join facts that were
latched in different sources.

## CONFIG and ERROR transactions

CONFIG admission is fixed by the common model:

```text
ValidateConfig(config)       // pure, no protected MMIO
  -> reserve the only CONFIG slot
  -> store the complete payload
  -> publish CONFIG
  -> run through the same service owner
```

A later concurrent CONFIG returns `BUSY` until the accepted transaction has completely
restarted the data path and released the gate. A Write that encounters a reserved or
pending CONFIG leaves the public record untouched and yields to CONFIG; it does not
self-republish WRITE while blocked.

CONFIG and ERROR deliberately retain both local TX records:

```text
active   -> stop old hardware, then restart the entire payload from byte zero
pending  -> keep the copied payload and its metadata position unchanged
public   -> leave all not-yet-staged records in WritePort
```

Restarting active from byte zero gives at-least-once wire semantics across CONFIG or
runtime ERROR. A prefix transmitted before the stop may appear again. This is a chosen
contract. An active record proven fully complete is retired by the COMPLETE prepass and
is not restarted.

The backend reports one typed stop result:

```text
UartDmaControlResult
  progress = PENDING | COMPLETED
  old_tx_terminal: UartOldTxTerminal = NONE | COMPLETE | ERROR
```

- `PENDING` means quiescence is not yet complete and carries no terminal
  classification. The backend must arrange a real future control carrier, such as an
  abort completion or hardware terminal IRQ.
- `COMPLETED` is the quiescence linearization point. Destructive cleanup is complete and
  no old callback can arrive later.
- `COMPLETE` asks the next service snapshot to retire active before restart.
- `NONE` means successful completion was not proven. `NONE` and `ERROR` retain active
  for restart. `ERROR` records why the old generation was not a successful completion;
  it does not fail or discard the retained record.

The model passes `active_tx` into the backend stop hook. A backend must not infer LibXR
active ownership from idle UART/DMA registers.

After an Advance hook returns `COMPLETED`, the common model publishes an internal
`CONTROL_READY`, plus `COMPLETE` when classified, into the same owner. This creates a
separate hardware-quiescent retirement snapshot without waiting for a Write, another
IRQ, or an external scheduler. Therefore two CONFIG calls do not require a transmission
between them: after the first accepted transaction reaches `COMPLETED`, its internal
continuation completes restart and reopens the single CONFIG slot.

The final order is:

```text
stop/apply old generation
  -> hardware quiescence and final terminal classification
  -> internal CONTROL_READY retirement snapshot
  -> backend CompleteConfig/CompleteRecovery restarts RX
  -> restart retained active TX when present
  -> release RX/TX/CONFIG admission
  -> rescan WRITE
```

The RX/CONFIG gate remains closed until both backend completion and any retained active
restart have returned. Pending and public records remain preserved throughout.

## RX boundary

RX data does not enter the serialized TX service. It remains one direct producer feeding
the ReadPort SPSC queue. The complete RX/CONFIG gate orders position/descriptor access
and byte delivery against CONFIG and recovery:

```text
read/ack IRQ status when the backend requires it
  -> TryEnterRx()
  -> read DMA position or descriptor state
  -> move bytes into the SPSC queue
  -> LeaveRx()
```

If control wins first, `TryEnterRx()` fails and transition-window hardware bytes may be
dropped. If RX enters first, that fragment finishes and its release publishes a
`CONTROL_READY` carrier for the waiting control transaction. Existing bytes already in
the software queue are not cleared by CONFIG or recovery.

`ProcessRxInIrqSource()` joins RX gate release with every COMPLETE/ERROR fact read by the
same raw source. Independent IRQ sources still require backend aggregation before a
destructive stop/reset clears terminal status.

Circular, linked-list, and future RX adapters are data-path models, not different owner
algorithms. The ESP backend in this tree uses a linked AHB-GDMA descriptor ring. STM32
traditional DMA uses the circular NDTR/CNDTR model. STM32 linked-list GPDMA requires a
different RX adapter and is not accepted by the current backend.

## DirectPolicy

DirectPolicy still uses the one `SerializedService`. "Direct" means it does not add an
SMP IRQ-domain mask or require owner admission before a raw status read.

Two Direct integrations are valid:

1. A vendor handler reads/acknowledges hardware and invokes a LibXR callback, as in the
   STM32 HAL path.
2. LibXR or the application owns a raw ISR on a single owner core, reads/acknowledges the
   source through `InvokeIrq()`, then publishes the resulting facts, as in CH32 and
   single-core ESP AHB-GDMA targets.

On one core, an IRQ may preempt a normal caller but cannot execute hardware operations
simultaneously on another core. If the service already has an owner, the IRQ's facts are
merged and the interrupted owner consumes them after it resumes.

A post-vendor-handler callback is not sufficient for a same-object SMP backend: the
vendor handler has already touched hardware before LibXR sees the callback. DirectPolicy
therefore requires either a single-core execution domain or an AMP owner-core contract.
Each backend must document which ISR contexts may call `SetConfig()`; a caller that can
preempt a related vendor/raw IRQ after its hardware-status read is not admitted merely
because CONFIG itself uses the service owner.

## AMP reuse

An AMP system reuses DirectPolicy by assigning the UART object, UART IRQ, TX-DMA IRQ,
RX-DMA IRQ, HAL handles, and all direct hardware access to one owner core. Other cores do
not call the object directly; they use an IPC/proxy request that is executed on the
owner core.

This does not add another UART concurrency model. The owner core sees the same
single-core DirectPolicy behavior. IPC ordering, shared-memory placement, and remote
completion delivery are BSP contracts outside `UartDmaModel`.

The current STM32 dual-core and CH32H417 mapping is AMP owner-core plus IPC. This tree
does not validate a same-object cross-core use of either backend.

## IrqSerializedPolicy for same-object SMP

IrqSerializedPolicy is required when normal callers on multiple cores directly share
one UART object while its protected IRQs execute on a fixed IRQ core. Normal callers may
originate on any core. The backend must control the raw ISR entry before its first
protected status read.

Normal caller admission is:

```text
lock the IRQ-domain guard
  -> mask every protected UART/TX-DMA/RX-DMA source
  -> publish event and try to claim the service owner
  -> unlock the short guard
  -> owner drains; loser returns without waiting
```

Raw IRQ admission is:

```text
lock and mask the IRQ domain
  -> try to claim the same service owner
  -> loser returns without reading or clearing status
  -> winner unlocks the short guard
  -> read/ack protected status and produce event facts
  -> drain the same UART service
```

The protected source must remain pending or retrigger after restore when an IRQ entry
loses admission. Owner release and IRQ-domain restore share the matching short guard. A
new owner can mask and claim before an older owner performs a delayed restore; an IRQ
admitted by that stale restore masks first, sees the new owner, and exits without
touching status. No `RESTORING` or `IRQ_PENDING` state is required.

The policy never intentionally disables all CPU interrupts. It masks only the IRQ domain
owned by that UART instance. Its progress proof assumes a caller that has masked the
domain eventually reaches its publication/claim step; a hard latency bound requires a
platform measurement and, if needed, a local preemption rule for that short window.

If an SDK clears status before calling LibXR, admission at the callback is too late. Such
an SDK path is valid only when the SDK independently serializes its handler and every
relevant hardware API across cores, or when the backend uses DirectPolicy under a
single-core/AMP contract.

## Platform mapping

### STM32

[`STM32UART`](../driver/st/stm32_uart.hpp) uses DirectPolicy and retains the CubeMX/HAL
IRQ path. It does not wrap or replace `HAL_UART_IRQHandler()` or
`HAL_DMA_IRQHandler()`. HAL callbacks publish facts into the same common service.

The current backend supports traditional Stream, Channel, and BDMA circular RX. It
rejects suspend/linked-list GPDMA families such as H5/U5/U3/N6/H7RS because CLLR/CBR1,
descriptor ownership, and suspend/reset semantics require another RX adapter.

The backend contract additionally requires:

- LibXR exclusively owns the UART/DMA data path and HAL handles after construction;
- application code does not directly start, stop, or abort those UART/DMA handles;
- the BSP assigns the related UART, TX-DMA, and RX-DMA IRQs the same NVIC preemption
  priority so their HAL handlers cannot nest each other; subpriorities may differ;
- `SetConfig()` is not called from this UART's HAL callbacks or from an ISR that can
  preempt its UART/TX-DMA/RX-DMA IRQ domain;
- Stream-DMA and UART completion vectors remain enabled and dispatch the matching HAL
  handlers whenever an asynchronous control stop needs them as carriers;
- the BSP keeps the UART kernel clock running while the UART is enabled.

For ACK-capable F0/L0/G0/G4/H7/C0/U0 UART IP, official reference-manual receive
sequences permit `UE -> DMAR -> RE` without polling `TEACK`/`REACK`. F1/F4 do not expose
those ACK flags and use `HAL_UART_Init()`. This establishes register-sequence legality,
not hardware-under-load behavior; loss of the UART kernel clock remains a BSP liveness
failure.

The STM32 M0/M0+ atomic fallback is one narrow exception to the no-global-mask rule. It
saves PRIMASK, masks local interrupts for one compiler atomic or HAL atomic register
RMW, then restores the saved PRIMASK. It never spans owner admission, a service handler,
or a DMA stop/restart transaction. CH32 and ESP do not use this fallback.

Dual-core STM32 variants are mapped as AMP owner-core plus IPC. This document does not
claim same-object SMP support for them.

### CH32

[`CH32UART`](../driver/ch/ch32_uart.hpp) uses DirectPolicy for the current V20x/V30x
backend and its V203/V307 BSPs. LibXR owns the UART/DMA ISR bodies, but single-core raw
status reads do not need SMP admission. Owner acquisition does not disable global
interrupts or mask the UART/DMA domain.

CONFIG and ERROR may disable this instance's TC/HT/TE/IDLE enables while stopping the
data path. That is hardware lifecycle control, not an owner lock. The BSP keeps the
related UART, TX-DMA, and RX-DMA IRQs on one owner core at the same preemption priority.
`SetConfig()` is not called from a related callback or an ISR that can preempt that IRQ
domain.

CH32H417 is mapped as AMP owner-core plus IPC. The V20x/V30x source and toolchain builds
do not validate H417 register, DMA, IRQ, or IPC integration.

### ESP AHB-GDMA

[`ESP32UartDma`](../driver/esp/esp_uart.hpp) is the only ESP UART class in this design.
The class is compiled only when both `SOC_AHB_GDMA_SUPPORTED` and `SOC_UHCI_SUPPORTED`
are true. In ESP-IDF 5.5.2, the exact target set is:

```text
ESP32-C3, ESP32-C5, ESP32-C6, ESP32-H2, ESP32-S3, ESP32-P4
```

The API is explicitly `ESP32UartDma`. There is no `ESP32UART` alias, FIFO class, or
runtime `enable_dma` selection. Targets outside the capability gate do not receive a
fallback UART class from this backend.

The RX adapter is a linked AHB-GDMA descriptor ring. The UART, TX-GDMA, and RX-GDMA
entries are registered directly by LibXR with the actual `esp_intr_alloc()` API and
non-shared `LEVEL1 | INTRDISABLED` flags. IDF does not clear their status before entering
LibXR.

Policy selection is compile-time:

```text
(SOC_CPU_CORES_NUM > 1) && !CONFIG_FREERTOS_UNICORE
  -> IrqSerializedPolicy
otherwise
  -> DirectPolicy
```

On SMP, construction must run in a task pinned to one core so every allocated IRQ lands
on the same core. Normal APIs may still be called from other cores. The adapter uses a
per-instance `portMUX_TYPE` guard and official `esp_intr_disable()`/
`esp_intr_enable()` operations to mask and restore the complete UART/GDMA IRQ domain.
RX/UART error scanners also collect the TX source before recovery clears it; the stop
path scans TX before and after `gdma_stop()` before reset.

Normal GDMA EOF continues to advance the TX double buffer without waiting for physical
UART line idle. Only destructive CONFIG waits for UART `TX_DONE` plus an idle FSM. Each
`StartDmaTx()` clears the stale `TX_DONE` raw bit immediately before launching the new
GDMA generation. CONFIG then publishes its armed state and enables `TX_DONE` without
clearing the current raw bit, because that bit may be the only carrier that observes the
current generation becoming idle. Disarming disables and clears the source.

`gdma_stop()` issues a stop command but does not prove that the descriptor FSM has parked
before the backend's final EOF sample. The ESP backend does not poll that FSM from the
service or ISR path. If no EOF was observed, it returns `UartOldTxTerminal::NONE`: this
means completion is unproven, so the retained active payload may be replayed from byte
zero. The possible duplicate prefix is part of the selected at-least-once wire contract;
`NONE` must not be interpreted as proof that no byte was transmitted.

ESP-IDF tracing modes that invoke the non-shared handler while holding the interrupt
allocator lock are rejected at compile time because ISR admission calls
`esp_intr_disable()`. This is an ESP adapter integration restriction, not a second UART
owner.

On single-core targets selected by the same capability gate, the LibXR-owned raw ISR
uses DirectPolicy. `SetConfig()` must not be called from a higher-priority ISR that can
preempt a related UART/GDMA ISR after its status read, or from inside that unfinished
raw ISR path.

For the current ESP-IDF 5.5.2 source revision, inherited target compile commands with
`-Werror` compile both `esp_uart.cpp` and `esp_uart_dma.cpp` for all six targets above.
Full `xr` builds pass for C3, C6, and S3. The C5, H2, and P4 full builds stop in unrelated
I2C/SPI or Wi-Fi compatibility code, while their two UART objects pass. Separate API
probes compile the explicit DMA type and reject both the removed alias and a target
without the required capabilities. These results establish source/toolchain integration
only; no ESP hardware timing or runtime recovery result is claimed.

## Verification boundary

The retained evidence is intentionally layered:

1. source correspondence checks map owner, gate, terminal classification, and backend
   adapters to the concrete implementation;
2. deterministic C++ tests exercise control continuation, back-to-back CONFIG without a
   Write/IRQ carrier, gate lifetime, and typed terminal behavior;
3. TLA+/TLC models the accepted business invariants and uses broken variants as negative
   controls;
4. GenMC checks selected concrete atomic owner/gate operations and memory orders;
5. sanitizer and target `-Werror` builds check executable/compiler integration;
6. vendor manuals establish only named register-level contracts such as STM32 ACK
   sequencing.

Formal models do not include vendor HAL internals, MMIO timing, DMA/cache coherency,
interrupt-controller latency, user callback behavior, or board wiring. Build success is
not runtime acceptance. Exact run counts and hashes belong in the verification artifacts
for the corresponding source revision; this design document does not copy historical
counts forward.

Hardware acceptance remains required for each product BSP, including sustained traffic,
CONFIG and ERROR injection, independent IRQ-source races, asynchronous stop carriers,
cacheable DMA storage where applicable, and measured ISR/service latency.
