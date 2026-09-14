"""Extract complete production definitions for deterministic I2C HAL seams.

Method bodies, Operation and ErrorCode come from production source; fixture
types and platform/helper hooks model the exercised control flow. These tests
do not replace complete-driver SDK builds or hardware acceptance. Missing or
ambiguous definitions fail generation; no copied driver body is maintained.
"""
import argparse
from pathlib import Path
import re


def definition(text, signature, is_type=False, optional=False):
    matches = list(re.finditer(re.escape(signature) + r"[^;{}]*\{", text))
    if optional and not matches:
        return ""
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {signature}, found {len(matches)}")
    match = matches[0]
    depth = 1
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for token in tokens.finditer(text, match.end()):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():token.end()] + (";\n" if is_type else "\n")
    raise ValueError(f"Unterminated definition of {signature}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    def read(path):
        return (args.root / path).read_text(encoding="utf-8")
    op = read("src/core/rw/operation.hpp")
    (args.output / "i2c_operation.inc").write_text(
        definition(read("src/core/libxr_def.hpp"), "enum class ErrorCode", True)
        + definition(op, "template <typename Args>\nclass Operation", True), encoding="utf-8")
    stm = read("driver/st/stm32_i2c.cpp")
    ch = read("driver/ch/ch32_i2c.cpp")
    hpm = read("driver/hpm/hpm_i2c.cpp")
    # Select the supported SDK branch, not the separate no-SDK fallback.
    hpm = hpm[:hpm.index('\n#else\n\nextern "C" void libxr_hpm_i2c_process_interrupt(LibXRHpmI2cType* ptr)')]
    groups = {
        "stm32_i2c_runtime.inc": (
            definition(stm, "struct I2CFilterState", True)
            + "".join(definition(stm, sig) for sig in [
                "static I2CFilterState CaptureI2CFilterState(",
                "static void RestoreI2CFilterState(",
                "static void RecoverAfterBlockTimeout("])
            + "".join(definition(stm, f"ErrorCode STM32I2C::{name}(") for name in ["Read", "Write", "MemRead", "MemWrite"])
            + "".join(definition(stm, f'extern "C" void HAL_I2C_{name}Callback(') for name in [
                "MasterRxCplt", "MasterTxCplt", "MemRxCplt", "MemTxCplt", "Error"])),
        "ch32_i2c_runtime.inc": (
            definition(ch, "void CH32I2C::ApplyConfig(", optional=args.control)
            + "".join(definition(ch, sig) for sig in [
                "ErrorCode CH32I2C::SetConfig(", "void CH32I2C::RecoverAfterImmediateFailure(",
                "void CH32I2C::AbortTransfer(", "void CH32I2C::TxDmaIRQHandler(",
                "void CH32I2C::RxDmaIRQHandler("])
            + "".join(definition(ch, f"ErrorCode CH32I2C::{name}(") for name in ["Read", "Write", "MemRead", "MemWrite"])),
        "hpm_i2c_start.inc": "".join(definition(hpm, f"ErrorCode HPMI2C::{name}(") for name in [
            "StartReadAsync", "StartWriteAsync", "StartMemReadAsync", "Read", "Write", "MemRead"]),
    }
    for name, text in groups.items():
        (args.output / name).write_text("// Generated from current production source.\n" + text, encoding="utf-8")


if __name__ == "__main__":
    main()
