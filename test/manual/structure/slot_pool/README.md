# SlotPool / MPMC Cortex-M 抢占回归（Unicorn）

用真实 ARM Cortex-M 指令（Unicorn 仿真，无 RTOS）验证两件事：

1. SlotPool 的任务上下文停在 `Acquire`、写入、`Publish`、`Take`、读取、
   `Release` 的每个中间点（含 `Put`/`Get` 内部 `Memory::FastCopy` 拷贝边界的
   中途注入）时，中断上下文的 `Put`/`Get` 都不被挡住：有空闲/就绪槽一定成功，
   `FULL`/`EMPTY` 只反映那一刻的真实状态；发布序号回绕后同一发布者仍然保序。
2. 同一场景下，origin/dev 的 Vyukov MPMC 队列（无临界区）返回假 `FULL`/假
   `EMPTY`：任务 `Pop` 被打断时中断的 `Push` 回绕到未完成槽位拿到 -14；任务
   `Push` 被打断时，已发布在后面的条目被 -13 隐藏。SlotPool 同场景不丢、不藏。

## 依赖

- `arm-none-eabi-g++`（Arm GNU 14.2.x，C++20）
- Python 3 + `unicorn` + `pyelftools`

## 运行

Docker（镜像 `ghcr.io/xrobot-org/docker-image-stm32:main`，仓库挂载到 `/src`）：

```bash
docker run --rm -v <repo-root>:/src \
  ghcr.io/xrobot-org/docker-image-stm32:main \
  bash -c 'pip install -q unicorn pyelftools && \
    python3 /src/test/manual/structure/slot_pool/run_slot_pool_preemption.py \
    --compiler arm-none-eabi-g++ --output-dir /tmp/slotpool-preemption'
```

裸机命令等价：脚本内部用 `--compiler` 指定的编译器把
`cortex_m_preemption.cpp`、`src/structure/slot_pool.cpp`、
`src/structure/queue/mpmc_queue_base.cpp` 编成 `-mcpu=cortex-m4 -mthumb`
`-nostdlib` 的 ELF（debug/release 两种优化级别），再用 Unicorn 逐场景执行。

输出为 JSON（stdout 与 `<output-dir>/results.json`），全部场景通过时
`status` 为 `PASS`：

- `intermediate_points`：六个中间点逐步注入中断，`Put`/`Get` 不被挡住、
  FULL/EMPTY 精确、回绕保序；
- `preempted_put_get`：在 `Put`/`Get` 的拷贝边界中途注入中断；
- `false_full_and_false_empty`：同一场景的 MPMC 对照，按预期出现 -14/-13。

MPMC 对照编译的就是当前仓库的 `mpmc_queue_base.cpp`；若上游 MPMC 之后加了
临界区，该对照场景需要换回未加临界区的基线源码才会继续复现假 FULL/EMPTY。
