/**
 * @file test_pipe_basic.cpp
 * @brief Pipe 读写顺序与分批传输测试 / Pipe ordering and chunked transfer tests.
 */
#include "rw_test_common.hpp"

/**
 * @brief 验证后续写入完成挂起读 / Verify that a later write completes a pending read.
 */
void test_pipe_basic()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 验证先写后读的数据一致性
 *        / Verify payload integrity when writing before reading.
 */
void test_pipe_write_then_read()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {1, 2, 3, 4, 5, 6, 7};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 验证两次写入按顺序满足一次读取
 *        / Verify that two writes satisfy one read in order.
 */
void test_pipe_chunked_rw()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX1[] = {'H', 'e', 'l'};
  static const uint8_t TX2[] = {'l', 'o', ' ', 'X', 'R'};
  uint8_t rx[sizeof(TX1) + sizeof(TX2)] = {0};

  ReadOperation rop;
  WriteOperation w1;
  WriteOperation w2;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX1, sizeof(TX1)}, w1);
  ASSERT(ec == ErrorCode::OK);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, w2);
  ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {'H', 'e', 'l', 'l', 'o', ' ', 'X', 'R'};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

/**
 * @brief 验证多次追加由一次提交完成读取
 *        / Verify that one stream commit completes a read.
 */
void test_pipe_stream_api()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();
  WriteOperation wop;

  uint8_t rx[8] = {0};

  ReadOperation rop;
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0xAA, 0xBB, 0xCC};
  static const uint8_t B[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};
  ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

/**
 * @brief 运行 Pipe 基础传输测试 / Run basic Pipe transfer tests.
 */
void RunBasePipeBasicTests()
{
  test_pipe_basic();
  test_pipe_write_then_read();
  test_pipe_chunked_rw();
  test_pipe_stream_api();
}
