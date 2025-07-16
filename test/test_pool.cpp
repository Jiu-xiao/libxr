#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

using Pool = LibXR::LockFreePool<int>;

// ---- 多线程测试共享数据 ----
constexpr int N = 10000;
static Pool pool_mt(32);
static volatile int push_sum = 0;
static volatile int pop_sum = 0;
static volatile int push_cnt = 0;
static volatile int pop_cnt = 0;
// 检查唯一性：每次 pop 检查是否重复
static volatile uint8_t pop_taken[N] = {0};

// ---- 线程参数结构体 ----
struct WriterArg
{
  int start;
  int end;
  Pool* pool;
};

struct ReaderArg
{
  Pool* pool;
};

// ---- Writer 线程任务 ----
void write_task(WriterArg arg)
{
  for (int i = arg.start; i < arg.end; ++i)
  {
    while (arg.pool->Put(i) != ErrorCode::OK)
    {
      LibXR::Thread::Yield();
    }
    __atomic_add_fetch(&push_sum, i, __ATOMIC_RELAXED);
    __atomic_add_fetch(&push_cnt, 1, __ATOMIC_RELAXED);
  }
}

// ---- Reader 线程任务 ----
void read_task(ReaderArg arg)
{
  int v = 0;
  while (__atomic_load_n(&pop_cnt, __ATOMIC_RELAXED) < N)
  {
    if (arg.pool->Get(v) == ErrorCode::OK)
    {
      ASSERT(v >= 0 && v < N);
      auto ret = __sync_lock_test_and_set(&pop_taken[v], 1);
      ASSERT(ret == 0);  // 只能被 pop 一次
      __atomic_add_fetch(&pop_sum, v, __ATOMIC_RELAXED);
      __atomic_add_fetch(&pop_cnt, 1, __ATOMIC_RELAXED);
    }
    else
    {
      LibXR::Thread::Yield();
    }
  }
}

}  // namespace

void test_lock_free_pool()
{
  // ---- 单线程基本功能测试 ----
  {
    static Pool pool(3);
    int tmp = 0;
    ASSERT(pool.Size() == 0);
    ASSERT(pool.EmptySize() == 3);
    ASSERT(pool.Put(1) == ErrorCode::OK);
    ASSERT(pool.Put(2) == ErrorCode::OK);
    ASSERT(pool.Put(3) == ErrorCode::OK);
    ASSERT(pool.Size() == 3);
    ASSERT(pool.EmptySize() == 0);
    ASSERT(pool.Put(4) == ErrorCode::FULL);
    ASSERT(pool.Get(tmp) == ErrorCode::OK && tmp == 1);
    ASSERT(pool.Get(tmp) == ErrorCode::OK && tmp == 2);
    ASSERT(pool.Get(tmp) == ErrorCode::OK && tmp == 3);
    ASSERT(pool.Get(tmp) == ErrorCode::EMPTY);
    ASSERT(pool.Put(5) == ErrorCode::OK);
    ASSERT(pool.Get(tmp) == ErrorCode::OK && tmp == 5);
  }

  // ---- 多线程并发完整性测试 ----
  {
    push_sum = pop_sum = push_cnt = pop_cnt = 0;
    for (int i = 0; i < N; ++i)
    {
      pop_taken[i] = 0;
    }

    // 创建 writer 线程
    LibXR::Thread writer[4];
    for (int i = 0; i < 4; ++i)
    {
      int start = N * i / 4, end = N * (i + 1) / 4;
      writer[i].Create<WriterArg>({start, end, &pool_mt}, write_task, "writer", 512,
                                  LibXR::Thread::Priority::REALTIME);
    }
    // 创建 reader 线程
    LibXR::Thread reader[4];
    for (int i = 0; i < 4; ++i)
    {
      reader[i].Create<ReaderArg>({&pool_mt}, read_task, "reader", 512,
                                  LibXR::Thread::Priority::REALTIME);
    }
    // 等待
    LibXR::Thread::Sleep(500);

    ASSERT(push_cnt == N);
    ASSERT(pop_cnt == N);
    ASSERT(push_sum == pop_sum);
    for (int i = 0; i < N; ++i)
    {
      ASSERT(pop_taken[i] == 1);
    }
  }

  // ---- 边界填满循环压力测试 ----
  {
    static Pool pool2(8);
    for (int round = 0; round < 200; ++round)
    {
      int pushed[8], popped[8];
      for (int i = 0; i < 8; ++i)
      {
        pushed[i] = i + round * 8;
        ASSERT(pool2.Put(pushed[i]) == ErrorCode::OK);
      }
      ASSERT(pool2.Put(9999) == ErrorCode::FULL);
      for (int i = 0; i < 8; ++i)
      {
        ASSERT(pool2.Get(popped[i]) == ErrorCode::OK);
      }
      std::sort(pushed, pushed + 8);
      std::sort(popped, popped + 8);
      for (int i = 0; i < 8; ++i)
      {
        ASSERT(pushed[i] == popped[i]);
      }
      int t = 0;
      ASSERT(pool2.Get(t) == ErrorCode::EMPTY);
    }
  }

  // ---- 槽复用极限测试 ----
  {
    static Pool pool3(2);
    for (int rep = 0; rep < 1000; ++rep)
    {
      ASSERT(pool3.Put(rep) == ErrorCode::OK);
      int z = 0;
      ASSERT(pool3.Get(z) == ErrorCode::OK && z == rep);
    }
  }
}
