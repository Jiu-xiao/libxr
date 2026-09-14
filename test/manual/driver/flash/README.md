# Flash 手动测试 / Manual Flash test

在调用方指定的专用区域内，检查擦除、顺序写入和完整回读。每轮写入一组位置相关数据，再擦除并写入它的反码，确认旧数据不会残留。正常结束时再擦除一次。

Check erase, sequential programming and complete readback within a caller-reserved region. Each round programs a position-dependent pattern, erases it and programs its bitwise inverse to check that old data does not remain. Erase once more on successful completion.

## 准备区域 / Prepare the region

测试会破坏指定区域原有的数据。调用方必须保留一个可整体擦除的合法区域，确保不包含程序、配置或其他需要保存的内容。内部擦写规则应一致，不要跨越行为不同的区域。

The test destroys existing data in the selected region. Reserve a legally whole-erasable range containing no firmware, configuration or other data to preserve. Use compatible erase/program rules throughout the range; do not cross into a region with different behavior.

`MinEraseSize()` 和 `MinWriteSize()` 只用于排除明显的对齐错误，不能证明实际扇区边界、区域独立性或写入批次是否合法。这些条件仍由调用方根据设备和区域确认。

`MinEraseSize()` and `MinWriteSize()` reject obvious alignment errors; they do not establish physical sector boundaries, erase isolation or legal write batches. The caller must verify these conditions for the actual device and region.

提供一块独占的 RAM 缓冲区，其大小作为每次 `Write()` 和 `Read()` 的长度。大小应为合法写入批次，并能整除测试区域；地址须满足后端要求的 RAM 对齐。测试不分配内存，缓冲区内容会被改写。

Supply an exclusive RAM buffer whose size is used for every `Write()` and `Read()`. It must be a legal programming batch that divides the region size, with the RAM alignment required by the backend. The test allocates no memory and overwrites the buffer contents.

## 调用 / Usage

把 `test/` 和 `test/manual/driver/` 加入应用的头文件搜索路径，从设备允许的普通执行上下文调用。例如，由应用提供已经确认的区域和擦除态：

Add `test/` and `test/manual/driver/` to the application's include paths and call from a normal execution context supported by the device. Have the application supply the verified region and erased state:

```cpp
#include "flash/test_flash.hpp"

void RunFlashTest(LibXR::Flash& flash, size_t offset, size_t size,
                  LibXR::RawData buffer, std::optional<uint8_t> erased_value)
{
  LibXR::Test::TestFlash(flash, offset, size, buffer, erased_value);
}
```

`offset` 相对于传入的 Flash 对象，不是固定物理地址。`erased_value` 没有默认值：若该区域擦除后每字节确定为 `0x00` 或 `0xFF`，就显式传入对应值；未知、未定义或不是重复字节时传 `std::nullopt`，跳过空白值比较。测试不会把一次读取的结果当作正确擦除态。

`offset` is relative to the supplied Flash object, not a fixed physical address. `erased_value` has no default. Explicitly pass the documented byte, such as `0x00` or `0xFF`, when the erased region consists of that repeated value. Pass `std::nullopt` for unknown, undefined or nonuniform erased contents to skip blank comparison. The test never learns the expected erased state from a read.

## 测试步骤 / Test sequence

1. 整体擦除区域；提供了擦除值时逐字节检查。
   Erase the entire region; compare every byte if an erased value was supplied.
2. 按地址递增写入位置相关数据，全部写完后完整回读比较。
   Program a position-dependent pattern in ascending order, then compare full readback.
3. 再擦除，按同样顺序写入反码并完整回读。
   Erase again, program the inverted pattern in the same order and compare full readback.
4. 所有轮次结束后再次擦除；提供了擦除值时检查最终空白状态。
   Erase after all rounds; verify the final blank state if an erased value was supplied.

每个编程单元在一次擦除后只写一次。回读前会用错误值覆盖工作缓冲区，以发现未执行或只完成部分复制的 `Read()`。失败立即触发始终生效的 `TEST_ASSERT`，不会重试或恢复原数据。

Each programming unit is written once per erase cycle. Before reading, the work buffer is filled with incorrect bytes to detect missing or partial copies by `Read()`. Failure immediately triggers the always-active `TEST_ASSERT`; the test neither retries nor restores old data.

最后一个可选参数 `iterations` 默认为 1，总共发出 **`2 × iterations + 1` 次区域擦除请求**。默认就是 3 次；不要照搬其他外设的千轮压测次数。传 `nullopt` 时，只能确认最后的擦除调用成功，不能声称核对过空白读值。

The optional final argument, `iterations`, defaults to 1, for **`2 × iterations + 1` region erase requests** in total: 3 by default. Do not copy thousand-round settings from other peripheral tests. With `nullopt`, final erase success means the call succeeded, not that blank contents were verified.

这项测试不推断子区域可独立擦除，也不测试逆序编程、未擦除重写、跨不同规则区域、掉电恢复或擦写寿命。CI 不自动运行这项破坏性测试。

This test does not infer independently erasable subregions or test reverse programming, rewriting without erase, crossings between incompatible regions, power-loss recovery or endurance. CI does not run this destructive test automatically.
