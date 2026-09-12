#!/usr/bin/env python3
"""Test CH32 shared IRQ archive retention, instance topology and PMA policy.

Only the WCH-specific ISR attribute is removed for host execution. Production
logic is compiled into an archive and linked against separate weak BSP handlers.
These tests do not replace target compilation or physical USB/CAN verification.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(args, **kwargs):
    p = subprocess.run(args, capture_output=True, text=True, **kwargs)
    if p.returncode:
        raise RuntimeError(' '.join(map(str, args))+'\n'+p.stdout+p.stderr)
    return p.stdout


MAIN = r'''
#include "ch32_usbcan_shared.hpp"
#include <cstdio>
using namespace LibXR::CH32UsbCanShared;
extern "C" void USB_HP_CAN1_TX_IRQHandler();
extern "C" void USB_LP_CAN1_RX0_IRQHandler();
extern "C" unsigned default_hits;
static unsigned usb_hits, rx_hits, tx_hits, sequence;
static void Usb() { ++usb_hits; sequence=sequence*10+1; }
static void Rx() { ++rx_hits; sequence=sequence*10+2; }
static void Tx() { ++tx_hits; sequence=sequence*10+3; }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    constexpr bool USE_USB=TEST_APP!=2;
    constexpr bool USE_CAN1=TEST_APP==2 || TEST_APP==3;
    constexpr bool USE_CAN2=TEST_APP==4;
    CHECK(K_HAS_CAN2==bool(TEST_DUAL));
    CHECK(usb_can_share_enabled() && usb_can_irq_share_enabled());
    CHECK(!can1_active() && !usb_pma_configured.load());
    CHECK(usb_pma_limit_bytes()==512);
    if (USE_USB) register_usb_irq(&Usb);
    if (USE_CAN1) { register_can1_rx0(&Rx); register_can1_tx(&Tx); }
    can1_inited.store(USE_CAN1); can2_inited.store(USE_CAN2);
    CHECK(can1_active()==USE_CAN1);
    CHECK(usb_pma_limit_bytes()==(USE_CAN2?256:USE_CAN1?384:512));
    sequence=0; USB_LP_CAN1_RX0_IRQHandler();
    CHECK(sequence==(USE_USB?(USE_CAN1?12:1):2));
    sequence=0; USB_HP_CAN1_TX_IRQHandler();
    CHECK(sequence==(USE_USB?(USE_CAN1?13:1):3));
    CHECK(default_hits==0);
    CHECK(usb_hits==(USE_USB?2:0));
    CHECK(rx_hits==(USE_CAN1?1:0) && tx_hits==(USE_CAN1?1:0));
    // Both CAN instances, or CAN2 alone, reserve the upper 256 PMA bytes.
    can2_inited.store(true);
    CHECK(usb_pma_limit_bytes()==(TEST_DUAL?256:USE_CAN1?384:512));
    can2_inited.store(USE_CAN2);
    usb_pma_configured.store(true);
    register_usb_irq(nullptr);
    CHECK(can1_active()==USE_CAN1 && usb_pma_configured.load());
    USB_LP_CAN1_RX0_IRQHandler(); USB_HP_CAN1_TX_IRQHandler();
    CHECK(usb_hits==(USE_USB?2:0));
    CHECK(rx_hits==(USE_CAN1?2:0) && tx_hits==(USE_CAN1?2:0));
    register_can1_rx0(nullptr); register_can1_tx(nullptr);
    CHECK(!can1_active());
    USB_LP_CAN1_RX0_IRQHandler(); USB_HP_CAN1_TX_IRQHandler();
    CHECK(default_hits==0);
    std::puts("PASS"); return 0;
}
'''
STARTUP = r'''
extern "C" { unsigned default_hits=0; }
extern "C" __attribute__((weak,noinline)) void USB_HP_CAN1_TX_IRQHandler(){++default_hits;}
extern "C" __attribute__((weak,noinline)) void USB_LP_CAN1_RX0_IRQHandler(){++default_hits;}
'''
EMPTY = r'''
#include "ch32_usbcan_shared.hpp"
int main(){
 using namespace LibXR::CH32UsbCanShared;
 auto callback=+[](){};
 register_usb_irq(callback); register_can1_rx0(callback); register_can1_tx(callback);
 can1_inited.store(true);can2_inited.store(true);
 if(can1_active() || usb_can_share_enabled() || usb_can_irq_share_enabled())return 1;
 if(usb_pma_limit_bytes()!=512)return 2;
 register_usb_irq(nullptr);return 0;
}
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo',required=True,type=Path)
    ap.add_argument('--cxx',default=os.environ.get('CXX','g++'))
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    results=[]
    source=(args.repo/'driver/ch/ch32_usbcan_shared.cpp').read_text()
    source=source.replace('__attribute__((interrupt("WCH-Interrupt-fast")))','')
    with tempfile.TemporaryDirectory(prefix='ch32-shared-irq-') as tmp:
        work=Path(tmp)
        (work/'libxr_def.hpp').write_text('#pragma once\n#define XR_TEST_STR_(x) #x\n#define DEF2STR(x) XR_TEST_STR_(x)\n')
        (work/'shared.cpp').write_text(source)
        (work/'startup.cpp').write_text(STARTUP)
        for family in ('CH32V20x_D6','CH32V20x_D8','CH32V20x_D8W','CH32V30x_D8','CH32V30x_D8C'):
            dual=family=='CH32V30x_D8C'
            # A common SDK can declare CAN2 even on a single-CAN target.
            (work/'test_device.hpp').write_text('#define RCC_APB1Periph_USB 1\n#define CAN1 1\n#define CAN2 1\n#define '+family+' 1\n')
            for lto in (False,True):
                flags=[args.cxx,'-std=c++20','-ffunction-sections','-fdata-sections','-I'+str(work),
                       '-I'+str(args.repo/'driver/ch'),'-DLIBXR_CH32_CONFIG_FILE=test_device.hpp',
                       '-DTEST_DUAL='+str(int(dual))]
                flags+=['-O2','-flto'] if lto else ['-O0']
                run(flags+['-c',str(work/'shared.cpp'),'-o',str(work/'shared.o')])
                archive=work/'libshared.a'
                if archive.exists():archive.unlink()
                run(['ar','rcs',str(archive),str(work/'shared.o')])
                run(flags+['-c',str(work/'startup.cpp'),'-o',str(work/'startup.o')])
                for app in ((1,2,3,4) if dual else (1,2,3)):
                    exe=work/f'check-{family}-{int(lto)}-{app}'
                    run(flags+['-DTEST_APP='+str(app),'-x','c++','-','-c','-o',str(work/'main.o')],input=MAIN)
                    run(flags+[str(work/'startup.o'),str(work/'main.o'),str(archive),'-Wl,--gc-sections','-o',str(exe)])
                    run([str(exe)])
                    symbols=run(['nm',str(exe)])
                    for name in ('USB_HP_CAN1_TX_IRQHandler','USB_LP_CAN1_RX0_IRQHandler'):
                        row=next(x.split() for x in symbols.splitlines() if x.endswith(' '+name))
                        if row[-2].lower()!='t':raise RuntimeError('ISR is not strong: '+str(row))
                    result={'family':family,'dual_can':dual,'lto':lto,'application':('usb','can1','both','usb-can2-only')[app-1],'pass':True}
                    results.append(result);print(json.dumps(result),flush=True)
        for macros in ('','#define RCC_APB1Periph_USB 1\n','#define CAN1 1\n'):
            (work/'test_device.hpp').write_text(macros)
            flags=[args.cxx,'-std=c++20','-O2','-I'+str(work),'-I'+str(args.repo/'driver/ch'),'-DLIBXR_CH32_CONFIG_FILE=test_device.hpp']
            run(flags+[str(work/'shared.cpp'),'-x','c++','-','-o',str(work/'empty')],input=EMPTY)
            run([str(work/'empty')]);results.append({'no_sharing_macros':macros.splitlines(),'pass':True})
    summary={'pass':True,'cases':len(results),'results':results}
    if args.output:args.output.write_text(json.dumps(summary,indent=2)+'\n')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
