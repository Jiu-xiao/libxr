#!/usr/bin/env python3
"""Check CH32 UART DMA tables against pinned WCH SDKs, without target hardware.

The executable only compares peripheral addresses and constants. No MCU register
is accessed. The SDK headers are real; only LibXR's unrelated umbrella is stubbed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(command, **kwargs):
    result = subprocess.run(command, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(" ".join(map(str, command)) + "\n" + result.stdout + result.stderr)
    return result.stdout


SOURCE = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "SDK_HEADER"
#include "SDK_RCC"
#include "SDK_DMA"
// Adversarial SDK surface: unrelated DMA2 names must not select DMA2 on V20x.
#ifdef TEST_DMA2_NAMES
#define DMA2_Channel3 ((DMA_Channel_TypeDef*)0x40020430U)
#define DMA2_Channel5 ((DMA_Channel_TypeDef*)0x40020458U)
#define DMA2_IT_TC5 0x10020000U
#define DMA2_IT_TC3 0x10000200U
#define DMA2_IT_HT3 0x10000400U
#endif
#include "ch32_uart_def.hpp"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
#define ROW(id, bank, tx, rx, irq) do { \
    CHECK(CH32_UART_RCC_PERIPH_MAP_DMA[id] == RCC_AHBPeriph_DMA##bank); \
    CHECK(CH32_UART_TX_DMA_CHANNEL_MAP[id] == DMA##bank##_Channel##tx); \
    CHECK(CH32_UART_RX_DMA_CHANNEL_MAP[id] == DMA##bank##_Channel##rx); \
    CHECK(CH32_UART_TX_DMA_IT_MAP[id] == DMA##bank##_IT_TC##tx); \
    CHECK(CH32_UART_RX_DMA_IT_TC_MAP[id] == DMA##bank##_IT_TC##rx); \
    CHECK(CH32_UART_RX_DMA_IT_HT_MAP[id] == DMA##bank##_IT_HT##rx); \
    CHECK(CH32_UART_IRQ_MAP[id] == irq); \
    std::printf("%u %u %u %u %u\n", unsigned(id), bank, tx, rx, unsigned(irq)); \
  } while (0)
int main() {
    constexpr unsigned N = CH32_UART_NUMBER;
#define LENGTH(table) CHECK(sizeof(table)/sizeof(table[0]) == N)
    LENGTH(CH32_UART_APB_MAP);
    LENGTH(CH32_UART_RCC_PERIPH_MAP);
    LENGTH(CH32_UART_RCC_PERIPH_MAP_DMA);
    LENGTH(CH32_UART_TX_DMA_CHANNEL_MAP);
    LENGTH(CH32_UART_RX_DMA_CHANNEL_MAP);
    LENGTH(CH32_UART_TX_DMA_IT_MAP);
    LENGTH(CH32_UART_RX_DMA_IT_TC_MAP);
    LENGTH(CH32_UART_RX_DMA_IT_HT_MAP);
    LENGTH(CH32_UART_IRQ_MAP);
    ROW(CH32_USART1, 1, 4, 5, USART1_IRQn);
    ROW(CH32_USART2, 1, 7, 6, USART2_IRQn);
    ROW(CH32_USART3, 1, 2, 3, USART3_IRQn);
#if defined(CH32V20x_D6) || defined(CH32V20x_D8) || defined(CH32V20x_D8W)
    CHECK(N == 4);
    ROW(CH32_UART4, 1, 1, 8, UART4_IRQn);
#ifdef CH32V20x_D6
    CHECK(UART4_IRQn == 61 && DMA1_Channel8_IRQn == 62);
#else
    CHECK(UART4_IRQn == 66 && DMA1_Channel8_IRQn == 67);
#endif
#else
    CHECK(N == 8);
    ROW(CH32_UART4, 2, 5, 3, UART4_IRQn);
    ROW(CH32_UART5, 2, 4, 2, UART5_IRQn);
    ROW(CH32_UART6, 2, 6, 7, UART6_IRQn);
    ROW(CH32_UART7, 2, 8, 9, UART7_IRQn);
    ROW(CH32_UART8, 2, 10, 11, UART8_IRQn);
#endif
    // Unchanged peripheral clock domains must retain their original indices.
    CHECK(CH32_UART_APB_MAP[CH32_USART1] == 2);
    CHECK(CH32_UART_RCC_PERIPH_MAP[CH32_USART1] == RCC_APB2Periph_USART1);
    CHECK(CH32_UART_RCC_PERIPH_MAP[CH32_USART2] == RCC_APB1Periph_USART2);
    CHECK(CH32_UART_RCC_PERIPH_MAP[CH32_USART3] == RCC_APB1Periph_USART3);
    CHECK(CH32_UART_RCC_PERIPH_MAP[CH32_UART4] == RCC_APB1Periph_UART4);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--v20-sdk', type=Path, required=True)
    parser.add_argument('--v30-sdk', type=Path, required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix='ch32-uart-mapping-') as tmp:
        work = Path(tmp)
        (work/'libxr.hpp').write_text('#pragma once\n#include <cstdint>\n#include <cstddef>\n#define XR_TEST_STR_(x) #x\n#define DEF2STR(x) XR_TEST_STR_(x)\n')
        for family, sdk, tag in [('CH32V20x_D6',args.v20_sdk,'ch32v20x'),
                                 ('CH32V20x_D8',args.v20_sdk,'ch32v20x'),
                                 ('CH32V20x_D8W',args.v20_sdk,'ch32v20x'),
                                 ('CH32V30x_D8',args.v30_sdk,'ch32v30x'),
                                 ('CH32V30x_D8C',args.v30_sdk,'ch32v30x')]:
            src = sdk/'EVT/EXAM/SRC'
            source = SOURCE.replace('SDK_HEADER',tag+'.h').replace('SDK_RCC',tag+'_rcc.h').replace('SDK_DMA',tag+'_dma.h')
            for adversarial in ([False,True] if tag=='ch32v20x' else [False]):
                command=[args.cxx,'-std=c++20','-O2','-fpermissive','-Wno-volatile','-Wno-attributes',
                         '-I'+str(work),'-I'+str(args.repo/'driver/ch'),
                         '-I'+str(src/'Peripheral/inc'),'-I'+str(src/'Core'),'-I'+str(src/'Debug'),
                         '-I'+str(sdk/'EVT/EXAM/USART/USART_DMA/User'),
                         '-D'+family,'-DLIBXR_CH32_CONFIG_FILE='+tag+'.h']
                if adversarial: command += ['-DTEST_DMA2_NAMES=1']
                target=work/(family+('-extra-dma2' if adversarial else ''))
                run(command+['-x','c++','-','-o',str(target)],input=source)
                rows=run([str(target)]).strip().splitlines()
                record={'family':family,'extra_dma2_names':adversarial,'uart_rows':[list(map(int,r.split())) for r in rows],'pass':True}
                results.append(record)
                print(json.dumps(record),flush=True)
    summary={'pass':True,'cases':len(results),'results':results}
    if args.output: args.output.write_text(json.dumps(summary,indent=2)+'\n')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
