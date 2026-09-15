# USB device core and endpoint ownership

This directory implements the event-driven USB 2.0 device path. The controller owns
one serialized software execution domain; it does not create a USB service thread.
USB hardware, class callbacks, and ordinary callers can provide progress events.
Each logical endpoint has one application/class owner; callers serialize submissions.

## Transmit

An IN endpoint owns two fixed CPU-accessible DATA buffers and at most one prepared
DATA block. The producer is `Callback<Endpoint::TxFill&>`:

```cpp
void Fill(bool in_isr, Endpoint::TxFill& fill)
{
  // Buffer() is valid because the state machine already established capacity.
  // Fill the bytes, then register their stable storage before any RW scope ends.
  const size_t length = Produce(fill.Buffer());
  if (length != 0) fill.SetSize(length);
}
```

No `SetSize` means no data, not failure. `SetSize(0)` explicitly supplies a normal
zero-length transfer and is valid only when `CanStart()` is true. A zero-length
transfer is active until its real hardware completion; length zero does not mean
idle. It uses no DATA storage, but only one DATA block can be prepared behind it.

`RequestTx(in_isr)` is a retained progress doorbell. An idle endpoint fills/starts
one block and can immediately prefill the alternate. A completion retires the old
transfer, starts prepared DATA, and permits another fill. There is no transfer
queue, nullable writable-buffer acquisition, or software role derived from a
controller DATA toggle. A producer returning no data is not polled for retries.

CDC Write admission and completion remain RW concepts. A whole Write finishes
when accepted into stable endpoint storage, including PREPARED storage, not when
the host application reads it. Queue settlement must follow `fill.SetSize()`.
CDC owns its not-yet-started termination intent; the endpoint never appends an
unrequested Bulk ZLP merely because a block length is a packet-size multiple.

## Receive

`ArmReceive(length)` authorizes one bounded receive into endpoint-owned storage.
Completion (including a real zero-length completion) retains the current result.
The class must explicitly rearm after consumption; a callback returning or an
unused hardware bank does not authorize the next request. `ReceiveResult()` is
used only within the controller-owned synchronous scope. Memory capacity, receive
extent, packet size, and a class message boundary are different quantities.

## Core and lifecycle

`DeviceCore<Capabilities>` has explicit compile-time `SPEEDS`, `BOS`, and `BUS_TIME`
policies. Optional BOS and bus-time state use separate empty/selected feature
bases; disabled dispatch does not rely on LTO. Negotiated speed/configuration are
runtime facts. Class composition stays non-templated at this boundary.

EP0 is a separate single-request machine, without ordinary endpoint prewrite.
The core owns data/status sequencing and actual success/abort notifications.
The legacy class request result is an adapter; `read_zlp`/`write_zlp` no longer
instruct a class to drive control handshakes. OUT stages are delivered whole and
bounded. Handler failures do not receive a successful status stage.

Descriptors/resources are planned during initialization. Data endpoints are not
activated by a descriptor read. Configuration changes apply before successful
status. Suspend preserves the session; reset/deconfiguration discard old EP work
without clearing unconsumed upstream RW data. Endpoint halt/clear-halt never
blindly replays an interrupted transfer.

DFU owns one immutable Flash job/data area. A first busy GETSTATUS must complete
before destructive work can start through the ordinary-context Timer. The slot
remains occupied until USB-side result accounting. Reset cancels unstarted work
or stops after the currently claimed noncancelable Flash call. The application
retains the final jump decision (`TryConsumeAppLaunch` /
`TryConsumeBootloaderLaunch`). Registered classes have initialization lifetime;
the Timer does not make Flash erase/code-fetch limitations disappear.

## Backend migration

- Endpoint `Config`'s fourth field is logical transfer-buffer capacity, not the old
  hardware/software `double_buffer` boolean. Supply zero for supplied capacity,
  or the class's declared maximum logical block size.
- Replace backend Configure/Close/Transfer overrides with `ConfigureHardware`,
  `CloseHardware`, `StartHardware(RawData,size_t)`, and hardware halt methods.
  `HardwareBuffer()` is the controller-accessible arena; `TransferBuffer()` is
  the current CPU destination/source, not an arbitrary next writable bank.
- `TransferMultiBulk` and controller-driven software `SwitchBuffer` are removed.
  Classes use a fixed owned block plus the producer/receive contract. Backends
  segment a block and report one logical completion.
- Use `EndpointPool::InterruptScope` around a complete raw USB IRQ batch. Capture
  data/completion and clear old hardware sources before it permits class work.
  `HardwareScope` protects short start/stop/register handoffs only, never a class
  callback or Flash operation. DMA stop/quiescence is still a hardware obligation.
- STM32 BSP USB IRQ handlers should call
  `LibXR::STM32USBDevice::IRQHandler(&hpcd_...)` instead of directly calling
  `HAL_PCD_IRQHandler`. This wraps the unchanged vendor HAL and closes the
  raw-IRQ versus rearm handoff. CH32 and ESP wrappers are inside their backend.
  External BSP/generator files are not modified by this branch.
- PMA Bulk uses hardware single buffering initially; software TX prewrite remains.
  CH32 ordinary DMA paths do not reserve the opposite direction for hardware banks.
- A bus-time frame value `0xffff` means the backend cannot supply a frame counter;
  microframe `0xff` means unavailable. No historical SOF callbacks are invented.
  The inspected CH32V203 USBFS SDK lacks a device-SOF interrupt selector, so that
  path does not promise periodic notifications. Other enabled sources are wired.

## Scope and validation limits

SuperSpeed/H417, Bulk Streams, and a general deferred EP0 reply API are not enabled
here. A SuperSpeed capability is rejected at compile time instead of advertising
unimplemented descriptors/link operations. These require a separate completed
backend/profile integration; USB2 BOS is not the same thing as SuperSpeed support.

Durable tests run real Endpoint, DeviceCore, CDC, HID, DAP, UAC and DFU code with
controlled hardware/Flash boundaries. They cover prewrite, exact zero completion,
reentry, multi-core publication, retained RX, control stages, reset/cancellation,
HS/FS descriptors, integer audio frames and single execution of DAP commands.
They do not certify physical bus timing, cache/MMIO behavior, or USB compliance.
STM32/CH32 real-SDK translation units and an ESP-IDF S3 component are additionally
cross-compiled. Actual boards and the migrated BSP IRQ entry remain validation
requirements before merging this branch.
