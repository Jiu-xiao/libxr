from pathlib import Path
import argparse
import tempfile
import json
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK

ROOT = Path(__file__).resolve().parents[4]
HARNESS = Path(__file__).with_name('cortex_m_preemption.cpp')
parser = argparse.ArgumentParser(description='Run Cortex-M SlotPool preemption regression using real ARM instructions. Same scenario runs the origin/dev MPMC queue as the failing comparison.')
parser.add_argument('--compiler', required=True, help='Path to an Arm Cortex-M C++20 compiler')
parser.add_argument('--output-dir', type=Path, help='Directory for compiled ARM ELF files and results.json')
arguments = parser.parse_args()
WORK = arguments.output_dir or Path(tempfile.mkdtemp(prefix='libxr-slotpool-cortex-m-'))
WORK.mkdir(parents=True, exist_ok=True)
COMPILER = arguments.compiler
SENTINEL = 0x9000

FUNCTIONS = (
    'SpInit2Empty', 'SpInit2One', 'SpInit2Full', 'SpInit4TwoPublished', 'SpInit2FullWrap',
    'SpLowAcquire', 'SpLowWrite', 'SpLowPublish', 'SpLowTake', 'SpLowRead', 'SpLowRelease',
    'SpLowPut', 'SpLowGet', 'SpHighPut', 'SpHighGet', 'SpHighGetThenPut',
    'SpHighPutThenDrain3', 'SpHighDrain2',
    'MpmcInit2Full', 'MpmcInit4TwoPublished', 'MpmcLowPop', 'MpmcLowPush', 'MpmcHighPop',
    'MpmcHighPush', 'MpmcHighPopThenPush', 'MpmcHighPushThenDrain3',
)


def build(mode):
    args = [COMPILER, '-std=c++20', '-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16',
            '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti', '-fno-builtin',
            '-ffunction-sections', '-fdata-sections', '-nostdlib', '-g',
            '-O0' if mode == 'debug' else '-O2',
            '-DLIBXR_SINGLE_CORE=1', '-DLIBXR_SYSTEM_freertos=True',
            '-DLIBXR_DEBUG_BUILD', '-DLIBXR_DEV_ASSERT_BUILD',
            f'-I{ROOT}/src/core', f'-I{ROOT}/src/structure', f'-I{ROOT}/src/structure/queue',
            str(HARNESS), str(ROOT / 'src/structure/slot_pool.cpp'),
            str(ROOT / 'src/structure/queue/mpmc_queue_base.cpp'),
            '-Wl,--gc-sections', '-Wl,-e,SpInit2Empty', '-Wl,-Ttext,0x10000',
            '-Wl,-Tdata,0x20000000']
    for function in FUNCTIONS:
        args.append(f'-Wl,--undefined={function}')
    args += ['-o', str(WORK / f'{mode}.elf')]
    subprocess.run(args, check=True)


class Target:
    def __init__(self, elf_path):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.cpu.mem_map(0x8000, 0x40000)
        self.cpu.mem_map(0x20000000, 0x10000)
        with elf_path.open('rb') as stream:
            elf = ELFFile(stream)
            self.symbols = {sym.name: sym['st_value'] for sym in elf.get_section_by_name('.symtab').iter_symbols()}
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    self.cpu.mem_write(segment['p_vaddr'], segment.data())
        self.cpu.reg_write(UC_ARM_REG_SP, 0x2000FFF0)
        self.cpu.reg_write(UC_ARM_REG_PRIMASK, 0)
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)
        self.inject = None
        self.triggered = False
        self.interrupt_now = False
        self.mask_at_copy = None
        self.stopped_at_return = False
        self.active_high = False
        self.copy_address = self.symbols['_ZN5LibXR6Memory8FastCopyEPvPKvj'] & ~1

    def hook(self, cpu, address, size, data):
        if address == SENTINEL:
            self.stopped_at_return = True
            cpu.emu_stop()
        elif address == self.copy_address and self.inject and not self.triggered and not self.active_high:
            self.triggered = True
            self.mask_at_copy = cpu.reg_read(UC_ARM_REG_PRIMASK)
            if not self.mask_at_copy:
                self.interrupt_now = True
                cpu.emu_stop()

    def call(self, name):
        self.cpu.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
        self.cpu.reg_write(UC_ARM_REG_PC, self.symbols[name] | 1)
        self.stopped_at_return = False
        while True:
            self.cpu.emu_start(self.cpu.reg_read(UC_ARM_REG_PC) | 1, 0, count=2000000)
            if self.interrupt_now:
                self.interrupt_now = False
                context = self.cpu.context_save()
                self.cpu.reg_write(UC_ARM_REG_SP, self.cpu.reg_read(UC_ARM_REG_SP) - 256)
                self.active_high = True
                self.call(self.inject)
                self.active_high = False
                self.cpu.context_restore(context)
                self.stopped_at_return = False
                continue
            assert self.stopped_at_return, f'{name} failed to return'
            return

    def run_preempted(self, low, high):
        # 每个抢占场景独立判定注入点，清掉上一个场景的锁存。
        # Each preemption scenario arms its own injection; clear the previous
        # scenario's latches.
        self.triggered = False
        self.mask_at_copy = None
        self.inject = high
        self.call(low)
        if self.triggered and self.mask_at_copy:
            assert self.cpu.reg_read(UC_ARM_REG_PRIMASK) == 0, 'low call leaked PRIMASK'
            self.active_high = True
            self.call(high)
            self.active_high = False
        assert self.triggered, 'did not reach copy boundary'
        self.inject = None

    def write(self, name, value):
        self.cpu.mem_write(self.symbols[name], struct.pack('<I', value & 0xFFFFFFFF))

    def read(self, name, count=1, signed=False):
        values = struct.unpack('<' + ('i' if signed else 'I') * count, self.cpu.mem_read(self.symbols[name], count * 4))
        return values[0] if count == 1 else list(values)


def slotpool_point_cases(target):
    # The low task stops between every pair of steps; the "interrupt" runs
    # between the steps and must never be blocked by the held slot.
    target.call('SpInit2Empty')
    target.call('SpLowAcquire')
    target.call('SpHighGet')
    result = dict(test='sp_point_after_acquire', high_get=target.read('sp_high_get_status', signed=True))
    assert result['high_get'] == -13, 'EMPTY must be accurate: nothing published yet'
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    result['high_put'] = target.read('sp_high_put_status', signed=True)
    assert result['high_put'] == 0, 'Put from the interrupt must succeed: a free slot exists'
    target.write('sp_low_value', 7)
    target.call('SpLowWrite')
    target.call('SpLowPublish')
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 9
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 7
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == -13

    target.call('SpInit2Empty')
    target.call('SpLowAcquire')
    target.write('sp_low_value', 7)
    target.call('SpLowWrite')
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    target.call('SpHighGet')
    result = dict(test='sp_point_after_write', high_put=target.read('sp_high_put_status', signed=True),
                  high_get=target.read('sp_high_get_status', signed=True), high_value=target.read('sp_high_get_value'))
    assert result['high_put'] == 0 and result['high_get'] == 0 and result['high_value'] == 9
    target.call('SpLowPublish')
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 7

    target.call('SpInit2Full')
    target.call('SpLowTake')
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 1001
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    assert target.read('sp_high_put_status', signed=True) == 0, 'Put must succeed after the interrupt released a slot'
    target.call('SpLowRead')
    assert target.read('sp_low_value') == 1000
    target.call('SpLowRelease')
    target.call('SpHighDrain2')
    result = dict(test='sp_point_after_take', high_get_value=target.read('sp_high_get_value'),
                  high_put=target.read('sp_high_put_status', signed=True),
                  drain=[target.read('sp_drain_status', 2, signed=True), target.read('sp_drain_value', 2)])
    assert result['drain'] == [[0, -13], [9, 0]], result['drain']

    target.call('SpInit2Full')
    target.call('SpLowTake')
    target.call('SpLowRead')
    assert target.read('sp_low_value') == 1000
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 1001
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    assert target.read('sp_high_put_status', signed=True) == 0
    target.call('SpLowRelease')
    target.call('SpHighDrain2')
    result = dict(test='sp_point_before_release', drain=[target.read('sp_drain_status', 2, signed=True), target.read('sp_drain_value', 2)])
    assert result['drain'] == [[0, -13], [9, 0]], result['drain']

    target.call('SpInit2Full')
    target.call('SpLowTake')
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    accurate_full = target.read('sp_high_put_status', signed=True)
    assert accurate_full == -14, 'FULL must be accurate while both slots are genuinely held'
    target.call('SpLowRead')
    target.call('SpLowRelease')
    target.call('SpHighPut')
    assert target.read('sp_high_put_status', signed=True) == 0, 'the pool must recover once the interrupted op releases'
    target.call('SpHighDrain2')
    result = dict(test='sp_accurate_full_then_recovery', accurate_full=accurate_full,
                  drain=[target.read('sp_drain_status', 2, signed=True), target.read('sp_drain_value', 2)])
    assert result['drain'] == [[0, 0], [1001, 9]], result['drain']

    target.call('SpInit2FullWrap')
    target.call('SpLowTake')
    target.call('SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 1001
    target.call('SpLowRead')
    result = dict(test='sp_wraparound_order', high_value=target.read('sp_high_get_value'), low_value=target.read('sp_low_value'))
    assert result['high_value'] == 1001 and result['low_value'] == 1000, 'sequence order must survive wraparound'


def slotpool_preemption_cases(target):
    # Interrupted Put (mid-write): the pre-published item stays visible to the
    # interrupt's Get; nothing is hidden by the in-flight slot.
    target.call('SpInit2One')
    target.write('sp_low_value', 1002)
    target.run_preempted('SpLowPut', 'SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 1000
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    assert target.read('sp_high_put_status', signed=True) == 0
    target.call('SpHighDrain2')
    result = dict(test='sp_interrupted_put_not_blocking', high_value=target.read('sp_high_get_value'),
                  drain=[target.read('sp_drain_status', 2, signed=True), target.read('sp_drain_value', 2)])
    assert result['drain'] == [[0, 0], [1002, 9]], result['drain']

    # Interrupted Get (mid-read): the other ready slot stays available to the
    # interrupt's Get and Put.
    target.call('SpInit2Full')
    target.run_preempted('SpLowGet', 'SpHighGet')
    assert target.read('sp_high_get_status', signed=True) == 0 and target.read('sp_high_get_value') == 1001
    assert target.read('sp_low_status', signed=True) == 0 and target.read('sp_low_value') == 1000
    target.write('sp_high_put_value', 9)
    target.call('SpHighPut')
    assert target.read('sp_high_put_status', signed=True) == 0
    target.call('SpHighDrain2')
    result = dict(test='sp_interrupted_get_not_blocking', low_value=target.read('sp_low_value'),
                  drain=[target.read('sp_drain_status', 2, signed=True), target.read('sp_drain_value', 2)])
    assert result['drain'] == [[0, -13], [9, 0]], result['drain']

    # Same scenario as mpmc_false_empty_hides_published: three published items
    # with one push in flight must all drain.
    target.call('SpInit4TwoPublished')
    target.write('sp_low_value', 1002)
    target.write('sp_high_put_value', 1003)
    target.run_preempted('SpLowPut', 'SpHighPutThenDrain3')
    assert target.read('sp_high_put_status', signed=True) == 0
    result = dict(test='sp_interrupted_put_hides_nothing', put=target.read('sp_high_put_status', signed=True),
                  drain_status=target.read('sp_drain_status', 3, signed=True),
                  drain_value=target.read('sp_drain_value', 3))
    assert result['drain_status'] == [0, 0, 0] and result['drain_value'] == [1000, 1001, 1003], result


def mpmc_comparison_cases(target):
    # Same scenario as sp_interrupted_get_not_blocking: MPMC returns a false
    # FULL when the interrupt's Push wraps onto the interrupted Pop cell.
    target.call('MpmcInit2Full')
    target.run_preempted('MpmcLowPop', 'MpmcHighPopThenPush')
    result = dict(test='mpmc_false_full_on_interrupted_pop', high_pop=target.read('mpmc_high_status', 1, signed=True),
                  high_push=target.read('mpmc_high_status', 2, signed=True)[1])
    assert result['high_pop'] == 0 and result['high_push'] == -14, result

    # Same scenario as sp_interrupted_put_hides_nothing: MPMC hides the later
    # published item behind the interrupted push cell.
    target.call('MpmcInit4TwoPublished')
    target.write('mpmc_low_value', 1002)
    target.write('mpmc_high_value', 1003)
    target.run_preempted('MpmcLowPush', 'MpmcHighPushThenDrain3')
    result = dict(test='mpmc_false_empty_hides_published', push=target.read('mpmc_high_status', 2, signed=True)[1],
                  drain_status=target.read('mpmc_high_status', 5, signed=True)[2:],
                  drain_value=target.read('mpmc_drain_value', 3))
    assert result['drain_status'] == [0, 0, -13], result


results = []
for mode in ('debug', 'release'):
    build(mode)
    elf = WORK / f'{mode}.elf'

    target = Target(elf)
    slotpool_point_cases(target)
    results.append(dict(mode=mode, variant='slotpool', test='intermediate_points', status='PASS'))

    target = Target(elf)
    slotpool_preemption_cases(target)
    results.append(dict(mode=mode, variant='slotpool', test='preempted_put_get', status='PASS'))

    target = Target(elf)
    mpmc_comparison_cases(target)
    results.append(dict(mode=mode, variant='mpmc', test='false_full_and_false_empty', status='PASS (fails as expected: baseline MPMC)'))

print(json.dumps(dict(status='PASS', cases=len(results), output_dir=str(WORK), results=results), indent=2))
(WORK / 'results.json').write_text(json.dumps(dict(status='PASS', cases=len(results), results=results), indent=2) + '\n')
