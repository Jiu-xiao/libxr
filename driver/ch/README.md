# CH32 CAN / FSDEV contracts

`CH32USBDeviceFS` is the legacy PMA-backed USBD controller. It is not
`CH32USBOtgFS` or `CH32USBOtgHS`; those controllers use separate endpoint memory
and do not participate in this PMA policy.

The family rules in this directory are validated against WCH's legacy
`ch32v20x.h` D6/D8/D8W resource model (CH32V203/CH32V208) and
`ch32v30x.h` D8/D8C resource model (CH32V303/305/307/317). The newer
CH32V205 uses a separate `ch32v205.h`, dual DMA and different RCC APIs; these
legacy tables do not claim CH32V205 support merely because its product name also
contains "V20x".

WCH common headers are resource supersets. In particular, `CH32V30x_D8C`
covers V305 together with V307/V317, while the exact parts expose different UART
counts and packages expose different pins. A symbol in the common header is not
by itself a promise that an application may instantiate that peripheral on every
part/package in the macro family.

## Device capabilities

The common WCH headers may declare resources that are absent on a selected
family. `ch32_can_caps.hpp` treats CH32V20x D6/D8/D8W and CH32V30x D8 (V303) as
single-CAN. CH32V30x D8C uses both CAN instances. This family-level distinction
does not claim that every package exposes all peripheral pins.

CAN1 TX and RX0 use the USB-named shared vectors on both single- and dual-CAN
families. CAN2 has separate vectors. Creating only CAN2 must not register CAN1
callbacks, but it must enable CAN1's clock for the shared filter registers.

## PMA and initialization order

The WCH *CH32F/V20x_V30x_V31x Reference Manual*, V2.5, section 21.3.3,
printed pages 353–354, specifies a shared 512-byte memory region:

| Configured CAN resources | FSDEV PMA budget |
| --- | ---: |
| No CAN | 512 bytes |
| CAN1 | 384 bytes |
| CAN1 and CAN2 | 256 bytes |
| CAN2 without a CAN1 object | 256 bytes (backend conservative reservation) |

The budget includes the endpoint buffer descriptor table. It is checked on the
first allocation, not only after a host bus reset.

The manual explicitly describes the 384-byte CAN1 partition and the 256-byte
dual-controller partition. It does not document "CAN2-only application objects"
as a separate PMA mode. This backend still reserves 256 bytes whenever CAN2 is
active: CAN2 uses the upper shared filter-bank partition through the CAN1 filter
master, so treating it as the dual-controller footprint avoids allowing USB PMA
to overlap a region whose safety is not documented. This is intentionally
conservative rather than a claim that the manual specifies a separate CAN2-only
partition.

Construct every CAN object that the application will use **before** calling
`CH32USBDeviceFS::Init()`, or otherwise configuring its endpoints. Creating a CAN
object after PMA allocation fails through `REQUIRE` in both Debug and Release.
Stopping USB is not a way around this requirement: endpoint objects retain PMA
addresses across Stop/Start, so their allocation reservation remains active.

This backend does not provide dynamic reallocation of PMA after adding CAN.
Application initialization is expected to be serialized. The rule does not
restrict applications that use only OTG FS/HS and CAN.

PMA exhaustion is also a fatal invariant in both Debug and Release. Endpoint
configuration must never continue with an address outside the topology-specific
512/384/256-byte budget.

## Shared-vector lifetime

Callback registration is out of line in `ch32_usbcan_shared.cpp`, so a static
archive link must retain the object containing both shared ISRs. A weak handler
in the startup file must not satisfy the vectors in the final image.

CAN publishes its callbacks before enabling its interrupt source. FSDEV prepares
its endpoints and publishes its callback before enabling IRQs or the pull-up.
FSDEV Stop masks its interrupt sources before unregistering the callback. It
keeps the shared NVIC lines enabled while CAN1 uses them. CAN2-only operation
does not keep CAN1's shared vectors active.

## Regression checks

`tools/check_ch32_shared_irq.py --repo .` checks real archive extraction against
weak startup handlers, with and without LTO, callback dispatch, CAN2-only state,
and 512/384/256-byte PMA policy. It removes only the WCH-specific interrupt
attribute for host execution; it does not simulate bus traffic.

Target compilation and physical USB/CAN traffic are separate acceptance steps.
Passing these checks does not validate the generic `USB::CDCUart` queue, external
CAN transceivers, or USB OTG controllers.
