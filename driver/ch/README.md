# CH32 CAN / FSDEV contracts

`CH32USBDeviceFS` is the legacy PMA-backed USBD controller. It is not
`CH32USBOtgFS` or `CH32USBOtgHS`; those controllers use separate endpoint memory
and do not participate in this PMA policy.

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
| CAN2 without a CAN1 object | 256 bytes reserved, including the high filter banks |

The budget includes the endpoint buffer descriptor table. It is checked on the
first allocation, not only after a host bus reset.

Construct every CAN object that the application will use **before** calling
`CH32USBDeviceFS::Init()`, or otherwise configuring its endpoints. Creating a CAN
object after PMA allocation fails through `REQUIRE` in both Debug and Release.
Stopping USB is not a way around this requirement: endpoint objects retain PMA
addresses across Stop/Start, so their allocation reservation remains active.

This backend does not provide dynamic reallocation of PMA after adding CAN.
Application initialization is expected to be serialized. The rule does not
restrict applications that use only OTG FS/HS and CAN.

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
