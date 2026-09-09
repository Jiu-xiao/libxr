# Timebase 测试 / Timebase tests

这组测试检查毫秒、微秒读数是否正常前进，以及时间戳跨过最大值后能否正确相减。

This group checks that millisecond and microsecond readings advance correctly and that timestamp subtraction works across wraparound.

`Sleep(100)` 实际可能睡得更久，所以测试不用固定的 100 ms 作为答案。它在每次读取 Timebase 前后读取 `steady_clock`，用这些读数确定实际经过时间的范围，再检查 Timebase 给出的时间差。

`Sleep(100)` may take longer than 100 ms, so the test does not use 100 ms as the expected result. It reads `steady_clock` before and after each Timebase reading to bound the actual elapsed time, then checks the Timebase difference against that range.

整数时间转换会舍去小数部分。测试为此保留微秒读数 2 μs、毫秒读数 1001 μs 的误差余量，计算理由写在对应断言旁。回绕检查使用固定输入和精确答案，不使用这个误差余量。

Integer time conversions discard fractional parts. The test allows 2 μs for microsecond readings and 1001 μs for millisecond readings; the calculation is explained beside the assertions. Wraparound checks use fixed inputs and exact answers, without this allowance.

按[测试说明](../../../README.md)构建后，在仓库根目录运行：

After building with the [test instructions](../../../README.md), run from the repository root:

```sh
script -q -e -c './build/test/test --case timebase' /dev/null
```
